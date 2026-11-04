from collections.abc import Iterable
from functools import partial
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
from .contract import (
    Contract,
    contract_of,
    failed_condition,
    hand_off_events,
    passes_through,
)
from .dataflow import (
    Event,
    EventsOf,
    Key,
    affects,
    find_released_calls,
    find_unmatched_exits,
)
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


def call_events(annotations: Annotations, call_node: Node) -> list[Event]:
    if call_node.type != "call_expression":
        return []
    spec = annotations.get(call_name(call_node))
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


def _with_hand_offs(
    contract: Contract, events_of: EventsOf, failed: str | None, node: Node
) -> list[Event]:
    """`events_of` plus the hand-offs to the function's own targets, except those a return of `failed` undoes."""
    hand_offs = hand_off_events(contract, events_of, node)
    return [
        *events_of(node),
        *(
            hand_off
            for hand_off in hand_offs
            if failed is None or hand_off.condition != failed
        ),
    ]


def _exits_by_failure(cfg: CFG, contract: Contract) -> dict[str | None, set[int]]:
    """Return blocks grouped by the own condition whose failure they return, None for the rest."""
    exits: dict[str | None, set[int]] = {}
    for block in cfg.blocks:
        if block.is_return:
            failed = failed_condition(block.return_node)
            failed = failed if failed in contract.conditions else None
            exits.setdefault(failed, set()).add(block.id)
    return exits


def _failing_calls(cfg: CFG, contract: Contract) -> dict[int, str]:
    """Calls from which every path returns a literal failure of one own condition, by that condition."""
    reached: dict[int, set[str | None]] = {block.id: set() for block in cfg.blocks}
    for failed, exit_ids in _exits_by_failure(cfg, contract).items():
        for exit_id in exit_ids:
            reached[exit_id].add(failed)
    changed = True
    while changed:
        changed = False
        for block in cfg.blocks:
            for successor in block.successors:
                if not reached[successor.id] <= reached[block.id]:
                    reached[block.id] |= reached[successor.id]
                    changed = True
    failing: dict[int, str] = {}
    for block in cfg.blocks:
        if len(reached[block.id]) != 1:
            continue
        (failed,) = reached[block.id]
        if failed is not None:
            for call in block.calls:
                failing[call.id] = failed
    return failing


class LeakRuleKind:
    def __init__(self, annotations: Annotations):
        self.annotations = annotations
        self.events_of = partial(call_events, annotations)

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

        contract = contract_of(cfg.function, self.annotations)
        variants = [
            (partial(_with_hand_offs, contract, self.events_of, failed), exit_ids)
            for failed, exit_ids in _exits_by_failure(cfg, contract).items()
        ]
        for key, start_call in starts.items():
            failure_edges = find_failure_edges(cfg, key, self.events_of)
            for events_of, exit_ids in variants:
                for exit_block in find_unmatched_exits(
                    cfg, key, events_of, failure_edges
                ):
                    if exit_block.id not in exit_ids:
                        continue
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


class ReleasedRuleKind:
    """Reports a release, or a call taking the identifier, after it is released on every path.

    A hand-off to the function's own target counts as a release for a second release, but not for a use: the function
    may still read what it handed off. It doesn't count at a call from which every path returns a literal failure of
    the hand-off's condition, because the caller then owns nothing.
    """

    def __init__(self, annotations: Annotations):
        self.annotations = annotations
        self.events_of = partial(call_events, annotations)

    def check(self, cfg: CFG, file_path: str) -> Iterable[Finding]:
        contract = contract_of(cfg.function, self.annotations)
        failing = _failing_calls(cfg, contract)
        keys = dict.fromkeys(
            Key(event.identifier, event.resource_class)
            for block in cfg.blocks
            for call in block.calls
            for event in self.events_of(call)
            if event.kind == "release"
        )
        findings: dict[tuple[int, str], Finding] = {}
        for key in keys:
            for failed in dict.fromkeys([None, *failing.values()]):
                events_of = partial(_with_hand_offs, contract, self.events_of, failed)
                for call in find_released_calls(cfg, key, events_of):
                    if failing.get(call.id) == failed and any(
                        event.kind == "release" and affects(event, key)
                        for event in self.events_of(call)
                    ):
                        call_line = line(call)
                        findings[(call_line, key.identifier)] = _released_finding(
                            file_path, call_line, key, "released twice"
                        )
            for call in find_released_calls(cfg, key, self.events_of):
                if not any(
                    affects(event, key) for event in self.events_of(call)
                ) and key.identifier in _argument_identifiers(call):
                    call_line = line(call)
                    findings.setdefault(
                        (call_line, key.identifier),
                        _released_finding(
                            file_path, call_line, key, "used after release"
                        ),
                    )
        return findings.values()


def _released_finding(file_path: str, call_line: int, key: Key, problem: str):
    return Finding(
        file=file_path,
        line=call_line,
        text=(
            f"[{file_path}:{call_line}] ERROR: Variable '{key.identifier}' "
            f"{problem} at line {call_line}"
        ),
    )


def _argument_identifiers(call_node: Node) -> set[str]:
    return {
        text(identifier)
        for arg in call_args(call_node)
        if (identifier := unwrap_identifier_reference(arg)) is not None
    }


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
        contract = contract_of(cfg.function, self.annotations)
        for block in cfg.blocks:
            for call in block.calls:
                spec = self.annotations.get(call_name(call))
                if spec is None or result_identifier(call) is not None:
                    continue
                returns = [tag for tag in spec.acquires if tag.target == RETURN_TARGET]
                if not returns or passes_through(contract, returns, call):
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
        ReleasedRuleKind(annotations),
        IdentifierArgumentRuleKind(annotations),
        UncapturedResultRuleKind(annotations),
    ]
