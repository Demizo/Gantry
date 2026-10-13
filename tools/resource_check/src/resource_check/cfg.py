from dataclasses import dataclass, field
from typing import Literal

from tree_sitter import Node

from .ast_nodes import contains_label, find_calls


@dataclass
class BasicBlock:
    id: int
    calls: list[Node] = field(default_factory=list)
    is_return: bool = False
    return_node: Node | None = None
    successors: list["BasicBlock"] = field(default_factory=list)
    predecessors: list["BasicBlock"] = field(default_factory=list)


@dataclass
class CFG:
    entry: BasicBlock
    blocks: list[BasicBlock]


@dataclass
class _Frame:
    kind: Literal["loop", "switch"]
    break_target: BasicBlock
    continue_target: BasicBlock | None = None


class _CFGBuilder:
    def __init__(self):
        self.blocks: list[BasicBlock] = []
        self.label_blocks: dict[str, BasicBlock] = {}
        self.pending_gotos: list[tuple[BasicBlock, str]] = []
        self.frames: list[_Frame] = []

    def new_block(self) -> BasicBlock:
        block = BasicBlock(id=len(self.blocks))
        self.blocks.append(block)
        return block

    @staticmethod
    def add_edge(a: BasicBlock | None, b: BasicBlock | None) -> None:
        if a is None or b is None:
            return
        a.successors.append(b)
        b.predecessors.append(a)

    def build(self, function_node: Node) -> CFG:
        entry = self.new_block()
        body = function_node.child_by_field_name("body")
        self.build_compound(body, entry)
        for goto_block, label in self.pending_gotos:
            self.add_edge(goto_block, self.label_blocks.get(label))
        return CFG(entry=entry, blocks=self.blocks)

    def build_compound(
        self, node: Node, current: BasicBlock | None
    ) -> BasicBlock | None:
        for statement in node.named_children:
            current = self.build_stmt(statement, current)
        return current

    def build_stmt(self, node: Node, current: BasicBlock | None) -> BasicBlock | None:
        handler = _STMT_HANDLERS.get(node.type)
        if handler is not None:
            return handler(self, node, current)
        if current is None:
            return None
        current.calls.extend(find_calls(node))
        return current

    def build_return(self, node: Node, current: BasicBlock | None) -> BasicBlock | None:
        if current is None:
            return None
        return_block = self.new_block()
        self.add_edge(current, return_block)
        return_block.calls.extend(find_calls(node))
        return_block.is_return = True
        return_block.return_node = node
        return None

    def build_break(self, node: Node, current: BasicBlock | None) -> BasicBlock | None:
        if current is None or not self.frames:
            return None
        self.add_edge(current, self.frames[-1].break_target)
        return None

    def build_continue(
        self, node: Node, current: BasicBlock | None
    ) -> BasicBlock | None:
        if current is None:
            return None
        for frame in reversed(self.frames):
            if frame.kind == "loop":
                self.add_edge(current, frame.continue_target)
                break
        return None

    def build_goto(self, node: Node, current: BasicBlock | None) -> BasicBlock | None:
        if current is None:
            return None
        label = node.child_by_field_name("label")
        label_name = label.text.decode("utf-8") if label is not None else ""
        target = self.label_blocks.get(label_name)
        if target is not None:
            self.add_edge(current, target)
        else:
            self.pending_gotos.append((current, label_name))
        return None

    def build_labeled(
        self, node: Node, current: BasicBlock | None
    ) -> BasicBlock | None:
        label = node.child_by_field_name("label")
        label_name = label.text.decode("utf-8") if label is not None else ""
        block = self.new_block()
        self.add_edge(current, block)
        self.label_blocks[label_name] = block
        inner = node.named_children[-1]
        return self.build_stmt(inner, block)

    def build_if(self, node: Node, current: BasicBlock | None) -> BasicBlock | None:
        if current is None:
            if not contains_label(node):
                return None
            current = self.new_block()
        condition = node.child_by_field_name("condition")
        current.calls.extend(find_calls(condition))

        then_entry = self.new_block()
        self.add_edge(current, then_entry)
        then_exit = self.build_stmt(node.child_by_field_name("consequence"), then_entry)

        alternative = node.child_by_field_name("alternative")
        if alternative is not None:
            else_entry = self.new_block()
            self.add_edge(current, else_entry)
            else_stmt = alternative.named_children[0]
            else_exit = self.build_stmt(else_stmt, else_entry)
        else:
            else_exit = current

        join = None
        if then_exit is not None:
            join = self.new_block()
            self.add_edge(then_exit, join)
        if else_exit is not None:
            join = join or self.new_block()
            self.add_edge(else_exit, join)
        return join

    def build_while(self, node: Node, current: BasicBlock | None) -> BasicBlock | None:
        if current is None and not contains_label(node):
            return None
        condition_block = self.new_block()
        self.add_edge(current, condition_block)
        condition_block.calls.extend(find_calls(node.child_by_field_name("condition")))

        after = self.new_block()
        self.frames.append(
            _Frame(kind="loop", break_target=after, continue_target=condition_block)
        )
        body_entry = self.new_block()
        self.add_edge(condition_block, body_entry)
        body_exit = self.build_stmt(node.child_by_field_name("body"), body_entry)
        self.frames.pop()

        self.add_edge(body_exit, condition_block)
        self.add_edge(condition_block, after)
        return after

    def build_do(self, node: Node, current: BasicBlock | None) -> BasicBlock | None:
        if current is None and not contains_label(node):
            return None
        body_entry = self.new_block()
        self.add_edge(current, body_entry)

        condition_block = self.new_block()
        after = self.new_block()
        self.frames.append(
            _Frame(kind="loop", break_target=after, continue_target=condition_block)
        )
        body_exit = self.build_stmt(node.child_by_field_name("body"), body_entry)
        self.frames.pop()

        self.add_edge(body_exit, condition_block)
        condition_block.calls.extend(find_calls(node.child_by_field_name("condition")))
        self.add_edge(condition_block, body_entry)
        self.add_edge(condition_block, after)
        return after

    def build_for(self, node: Node, current: BasicBlock | None) -> BasicBlock | None:
        if current is None and not contains_label(node):
            return None
        if current is None:
            current = self.new_block()
        initializer = node.child_by_field_name("initializer")
        current.calls.extend(find_calls(initializer))

        condition_block = self.new_block()
        self.add_edge(current, condition_block)
        condition = node.child_by_field_name("condition")
        condition_block.calls.extend(find_calls(condition))

        after = self.new_block()
        update_block = self.new_block()
        update_block.calls.extend(find_calls(node.child_by_field_name("update")))
        self.add_edge(update_block, condition_block)

        self.frames.append(
            _Frame(kind="loop", break_target=after, continue_target=update_block)
        )
        body_entry = self.new_block()
        self.add_edge(condition_block, body_entry)
        body_exit = self.build_stmt(node.child_by_field_name("body"), body_entry)
        self.frames.pop()

        self.add_edge(body_exit, update_block)
        if condition is not None:
            self.add_edge(condition_block, after)
        return after

    def build_switch(self, node: Node, current: BasicBlock | None) -> BasicBlock | None:
        if current is None and not contains_label(node):
            return None
        if current is None:
            current = self.new_block()
        current.calls.extend(find_calls(node.child_by_field_name("condition")))
        body = node.child_by_field_name("body")
        case_nodes = [c for c in body.named_children if c.type == "case_statement"]

        after = self.new_block()
        self.frames.append(_Frame(kind="switch", break_target=after))

        case_blocks = []
        has_default = False
        for case in case_nodes:
            block = self.new_block()
            case_blocks.append(block)
            self.add_edge(current, block)
            if case.child_by_field_name("value") is None:
                has_default = True
        if not has_default:
            self.add_edge(current, after)

        previous_exit = None
        for case, block in zip(case_nodes, case_blocks, strict=True):
            if previous_exit is not None:
                self.add_edge(previous_exit, block)
            value_node = case.child_by_field_name("value")
            statement_nodes = [
                c
                for c in case.named_children
                if value_node is None or c.id != value_node.id
            ]
            case_current = block
            for statement in statement_nodes:
                case_current = self.build_stmt(statement, case_current)
            previous_exit = case_current
        self.add_edge(previous_exit, after)

        self.frames.pop()
        return after


_STMT_HANDLERS = {
    "compound_statement": _CFGBuilder.build_compound,
    "return_statement": _CFGBuilder.build_return,
    "break_statement": _CFGBuilder.build_break,
    "continue_statement": _CFGBuilder.build_continue,
    "goto_statement": _CFGBuilder.build_goto,
    "labeled_statement": _CFGBuilder.build_labeled,
    "if_statement": _CFGBuilder.build_if,
    "while_statement": _CFGBuilder.build_while,
    "do_statement": _CFGBuilder.build_do,
    "for_statement": _CFGBuilder.build_for,
    "switch_statement": _CFGBuilder.build_switch,
}


def _reachable_blocks(cfg: CFG) -> set[int]:
    seen = {cfg.entry.id}
    stack = [cfg.entry]
    while stack:
        block = stack.pop()
        for successor in block.successors:
            if successor.id not in seen:
                seen.add(successor.id)
                stack.append(successor)
    return seen


def build_cfg(function_node: Node) -> CFG:
    cfg = _CFGBuilder().build(function_node)
    reachable = _reachable_blocks(cfg)
    cfg.blocks = [b for b in cfg.blocks if b.id in reachable]
    return cfg
