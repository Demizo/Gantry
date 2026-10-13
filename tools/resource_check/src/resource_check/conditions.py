from collections.abc import Callable

from tree_sitter import Node

from .ast_nodes import (
    declarator_identifier,
    iter_nodes,
    result_identifier,
    strip_parentheses,
    text,
)
from .cfg import CFG, BasicBlock
from .dataflow import Event, EventsOf, Key, affects

Subject = Callable[[Node], bool]

_REPEATING_STATEMENTS = (
    "for_statement",
    "while_statement",
    "do_statement",
    "goto_statement",
)

# A bare argument counts as a write because an unexpanded macro may assign to it.
_WRITE_PARENTS = (
    "update_expression",
    "argument_list",
    "pointer_declarator",
    "array_declarator",
    "declaration",
    "parameter_declaration",
)


def _same(candidate: Node | None, reference: Node) -> bool:
    return (
        candidate is not None
        and candidate.start_byte == reference.start_byte
        and candidate.end_byte == reference.end_byte
        and candidate.type == reference.type
    )


def _flip(arm: str | None) -> str | None:
    return {"true": "false", "false": "true"}.get(arm) if arm is not None else None


def _failing_arm(condition: Node | None, is_subject: Subject, mode: str) -> str | None:
    condition = strip_parentheses(condition)
    if condition is None:
        return None
    if is_subject(condition):
        return "true" if mode == "on-success" else "false"
    if condition.type == "unary_expression":
        if text(condition.child_by_field_name("operator")) != "!":
            return None
        return _flip(
            _failing_arm(condition.child_by_field_name("argument"), is_subject, mode)
        )
    if condition.type != "binary_expression":
        return None

    left = condition.child_by_field_name("left")
    if not is_subject(strip_parentheses(left)):
        return None
    operator = text(condition.child_by_field_name("operator"))
    right = text(strip_parentheses(condition.child_by_field_name("right")))
    if mode == "on-success":
        if operator in ("==", "!=") and right in ("0", "SUCCESS"):
            return "false" if operator == "==" else "true"
        if operator in ("<", ">=") and right == "0":
            return "true" if operator == "<" else "false"
    elif operator in ("==", "!=") and right in ("0", "NULL"):
        return "true" if operator == "==" else "false"
    return None


def _declared_names(function: Node) -> set[str]:
    names = set()
    for node in iter_nodes(function):
        if node.type not in ("declaration", "parameter_declaration"):
            continue
        for declarator in node.children_by_field_name("declarator"):
            identifier = declarator_identifier(declarator)
            if identifier is not None:
                names.add(text(identifier))
    return names


def _is_write(identifier: Node) -> bool:
    node = identifier
    while node.parent is not None and node.parent.type == "parenthesized_expression":
        node = node.parent
    parent = node.parent
    if parent is None:
        return False
    if parent.type == "assignment_expression":
        return parent.child_by_field_name("left") == node
    if parent.type == "init_declarator":
        return parent.child_by_field_name("declarator") == node
    return parent.type in _WRITE_PARENTS


def _address_escapes(function: Node, name: str, after: int) -> bool:
    """Whether a pointer to `name` may be held somewhere that could change it before the test.

    Passing `&name` straight to a call after the test cannot affect it, unless a loop (or goto) lets the callee's
    stored pointer be used on a later pass.
    """
    nodes = list(iter_nodes(function))
    may_repeat = any(n.type in _REPEATING_STATEMENTS for n in nodes)
    for node in nodes:
        if (
            node.type != "pointer_expression"
            or text(node.child_by_field_name("operator")) != "&"
        ):
            continue
        argument = strip_parentheses(node.child_by_field_name("argument"))
        if argument is None or argument.type != "identifier" or text(argument) != name:
            continue
        is_call_argument = (
            node.parent is not None and node.parent.type == "argument_list"
        )
        if not (is_call_argument and node.start_byte >= after and not may_repeat):
            return True
    return False


def _written_between(function: Node, name: str, start: int, end: int) -> bool:
    return any(
        node.type == "identifier"
        and text(node) == name
        and start <= node.start_byte < end
        and _is_write(node)
        for node in iter_nodes(function)
    )


def _contains(outer: Node, inner: Node) -> bool:
    return outer.start_byte <= inner.start_byte and inner.end_byte <= outer.end_byte


def _result_variable(call: Node) -> tuple[str, Node | None] | None:
    identifier = result_identifier(call)
    if identifier is None:
        return None
    scope = None
    if call.parent.type == "init_declarator" and call.parent.parent is not None:
        scope = call.parent.parent.parent
    return text(identifier), scope


def _subject_of(call: Node, condition: Node, function: Node) -> Subject | None:
    def is_call_value(node: Node | None) -> bool:
        node = strip_parentheses(node)
        if node is None:
            return False
        if _same(node, call):
            return True
        if (
            node.type == "assignment_expression"
            and text(node.child_by_field_name("operator")) == "="
        ):
            return _same(strip_parentheses(node.child_by_field_name("right")), call)
        return False

    if _contains(condition, call):
        return is_call_value

    result = _result_variable(call)
    if result is None:
        return None
    name, scope = result
    stable = (
        call.end_byte <= condition.start_byte
        and (scope is None or _contains(scope, condition))
        and name in _declared_names(function)
        and not _address_escapes(function, name, condition.end_byte)
        and not _written_between(function, name, call.end_byte, condition.end_byte)
    )
    if not stable:
        return None

    def is_result(node: Node | None) -> bool:
        return is_call_value(node) or (
            node is not None and node.type == "identifier" and text(node) == name
        )

    return is_result


def _tested_acquire(block: BasicBlock, key: Key, events_of: EventsOf) -> Event | None:
    last = None
    for call in block.calls:
        for event in events_of(call):
            if affects(event, key):
                last = event
    if last is None or last.kind != "acquire" or last.condition is None:
        return None
    return last


def find_failure_edges(
    cfg: CFG, key: Key, events_of: EventsOf
) -> dict[tuple[int, int], Node]:
    edges: dict[tuple[int, int], Node] = {}
    for block in cfg.blocks:
        branch = block.branch
        if branch is None or branch.false_target is None:
            continue
        acquire = _tested_acquire(block, key, events_of)
        if acquire is None:
            continue
        subject = _subject_of(acquire.call, branch.condition, cfg.function)
        if subject is None:
            continue
        arm = _failing_arm(branch.condition, subject, acquire.condition)
        if arm == "true":
            edges[(block.id, branch.true_target.id)] = acquire.call
        elif arm == "false":
            edges[(block.id, branch.false_target.id)] = acquire.call
    return edges
