from collections.abc import Iterable
from typing import Protocol

from tree_sitter import Node

from .annotations import RETURN_TARGET, Annotations, Spec, Tag
from .ast_nodes import (
    call_args,
    call_name,
    line,
    result_identifier,
    text,
    unwrap_identifier_reference,
)
from .cfg import CFG
from .conditions import find_failure_edges
from .dataflow import Event, Key, find_unmatched_exits
from .findings import Finding


class RuleKind(Protocol):
    def check(self, cfg: CFG, file_path: str) -> Iterable[Finding]: ...


def _tracked_argument(call_node: Node, spec: Spec, target: str) -> Node | None:
    index = spec.arg_index(target)
    args = call_args(call_node)
    return args[index] if index is not None and index < len(args) else None


def _tracked_identifier(call_node: Node, spec: Spec, tag: Tag) -> Node | None:
    if tag.target == RETURN_TARGET:
        return result_identifier(call_node)
    arg = _tracked_argument(call_node, spec, tag.target)
    return unwrap_identifier_reference(arg) if arg is not None else None


class LeakRuleKind:
    def __init__(self, annotations: Annotations):
        self.annotations = annotations

    def events_of(self, call_node: Node) -> list[Event]:
        spec = self.annotations.get(call_name(call_node))
        if spec is None:
            return []
        events = []
        for kind, tags in (("release", spec.releases), ("acquire", spec.acquires)):
            for tag in tags:
                identifier = _tracked_identifier(call_node, spec, tag)
                if identifier is not None:
                    events.append(
                        Event(
                            kind,
                            text(identifier),
                            tag.resource_class,
                            call_node,
                            tag.condition,
                        )
                    )
        return events

    def check(self, cfg: CFG, file_path: str) -> Iterable[Finding]:
        starts: dict[Key, Node] = {}
        for block in cfg.blocks:
            for call in block.calls:
                for event in self.events_of(call):
                    if event.kind != "acquire":
                        continue
                    key = Key(event.identifier, event.resource_class)
                    if key not in starts or line(call) < line(starts[key]):
                        starts[key] = call

        for key, start_call in starts.items():
            failure_edges = find_failure_edges(cfg, key, self.events_of)
            for exit_block in find_unmatched_exits(
                cfg, key, self.events_of, failure_edges
            ):
                leak_line = line(exit_block.return_node)
                yield Finding(
                    file=file_path,
                    line=leak_line,
                    text=(
                        f"[{file_path}:{leak_line}] ERROR: Variable '{key.identifier}' "
                        f"allocated/referenced on line {line(start_call)} was not released. "
                        f"Leaked at line {leak_line}"
                    ),
                )


class IdentifierArgumentRuleKind:
    def __init__(self, annotations: Annotations):
        self.annotations = annotations

    def check(self, cfg: CFG, file_path: str) -> Iterable[Finding]:
        for block in cfg.blocks:
            for call in block.calls:
                spec = self.annotations.get(call_name(call))
                if spec is None:
                    continue
                targets = dict.fromkeys(
                    t.target
                    for t in [*spec.acquires, *spec.releases]
                    if t.target != RETURN_TARGET
                )
                for target in targets:
                    arg = _tracked_argument(call, spec, target)
                    if arg is None or unwrap_identifier_reference(arg) is not None:
                        continue
                    call_line = line(call)
                    yield Finding(
                        file=file_path,
                        line=call_line,
                        text=(
                            f"[{file_path}:{call_line}] POLICY ERROR: Used an expression "
                            f"({text(arg)}) instead of an identifier. Static analysis requires "
                            f"the use of identifiers!"
                        ),
                    )


class UncapturedResultRuleKind:
    def __init__(self, annotations: Annotations):
        self.annotations = annotations

    def check(self, cfg: CFG, file_path: str) -> Iterable[Finding]:
        for block in cfg.blocks:
            for call in block.calls:
                spec = self.annotations.get(call_name(call))
                if spec is None or not any(
                    t.target == RETURN_TARGET for t in spec.acquires
                ):
                    continue
                if result_identifier(call) is not None:
                    continue
                call_line = line(call)
                yield Finding(
                    file=file_path,
                    line=call_line,
                    text=(
                        f"[{file_path}:{call_line}] POLICY ERROR: The result of {spec.name} must be "
                        f"assigned to an identifier. Static analysis requires the use of identifiers!"
                    ),
                )


def build_rule_kinds(annotations: Annotations) -> list[RuleKind]:
    return [
        LeakRuleKind(annotations),
        IdentifierArgumentRuleKind(annotations),
        UncapturedResultRuleKind(annotations),
    ]
