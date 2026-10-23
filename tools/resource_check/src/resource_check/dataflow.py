from collections.abc import Callable
from dataclasses import dataclass
from typing import Literal, NamedTuple

from tree_sitter import Node

from .annotations import ANY_CLASS
from .ast_nodes import text, written_identifier
from .cfg import CFG, BasicBlock


class Key(NamedTuple):
    identifier: str
    resource_class: str


@dataclass(frozen=True)
class Event:
    kind: Literal["acquire", "release"]
    identifier: str
    resource_class: str
    call: Node
    condition: str | None = None


EventsOf = Callable[[Node], list[Event]]


def affects(event: Event, key: Key) -> bool:
    identifier, resource_class = key
    if event.identifier != identifier:
        return False
    if event.kind == "release":
        return event.resource_class in (resource_class, ANY_CLASS)
    # A `*` key comes from a release of any class, so an acquire of any class ends it.
    return resource_class in (event.resource_class, ANY_CLASS)


def _transfer(
    nodes: list[Node],
    fact: bool,
    key: Key,
    events_of: EventsOf,
    skip_acquires: bool = False,
    skip_call: Node | None = None,
) -> bool:
    """Whether `key` is released after `nodes`, given whether it was before."""
    for node in nodes:
        if node.type != "call_expression":
            if text(written_identifier(node)) == key.identifier:
                fact = False
            continue
        for event in events_of(node):
            if not affects(event, key):
                continue
            if event.kind == "release":
                fact = True
            elif not skip_acquires and node != skip_call:
                fact = False
    return fact


Transfer = Callable[[BasicBlock, bool], bool]
EdgeTransfer = Callable[[BasicBlock, BasicBlock, bool], bool | None]


def _solve(
    cfg: CFG, entry_fact: bool, transfer: Transfer, edge: EdgeTransfer | None = None
) -> dict[int, bool]:
    """Each block's in-fact under a must-analysis: true only when true on every incoming edge.

    `edge` may override the fact on an edge, from its predecessor's in-fact.
    """
    in_fact = {block.id: True for block in cfg.blocks}
    out_fact = {block.id: True for block in cfg.blocks}

    def edge_fact(predecessor: BasicBlock, block: BasicBlock) -> bool:
        if predecessor.id not in out_fact:
            return True
        if edge is not None:
            fact = edge(predecessor, block, in_fact[predecessor.id])
            if fact is not None:
                return fact
        return out_fact[predecessor.id]

    changed = True
    while changed:
        changed = False
        for block in cfg.blocks:
            in_fact[block.id] = (
                entry_fact
                if block is cfg.entry
                else all(edge_fact(p, block) for p in block.predecessors)
            )
            new_out = transfer(block, in_fact[block.id])
            if new_out != out_fact[block.id]:
                out_fact[block.id] = new_out
                changed = True
    return in_fact


FailureEdges = dict[tuple[int, int], Node]


def find_unmatched_exits(
    cfg: CFG, key: Key, events_of: EventsOf, failure_edges: FailureEdges | None = None
) -> list[BasicBlock]:
    failure_edges = failure_edges or {}

    def failure_edge(predecessor: BasicBlock, block: BasicBlock, fact: bool):
        skipped = failure_edges.get((predecessor.id, block.id))
        if skipped is None:
            return None
        return _transfer(predecessor.calls, fact, key, events_of, skip_call=skipped)

    in_fact = _solve(
        cfg,
        True,
        lambda block, fact: _transfer(block.calls, fact, key, events_of),
        failure_edge,
    )
    return [
        block
        for block in cfg.blocks
        if block.is_return
        and not _transfer(
            block.calls, in_fact[block.id], key, events_of, skip_acquires=True
        )
    ]


def _operations(block: BasicBlock) -> list[Node]:
    # End offsets give evaluation order: arguments before their call, a call before the assignment of its result.
    return sorted(
        [*block.calls, *block.writes],
        key=lambda node: (node.end_byte, node.type != "call_expression"),
    )


def find_released_calls(cfg: CFG, key: Key, events_of: EventsOf) -> list[Node]:
    """Calls reached only on paths where `key` is released and not acquired or assigned since."""
    in_fact = _solve(
        cfg,
        False,
        lambda block, fact: _transfer(_operations(block), fact, key, events_of),
    )
    released: list[Node] = []
    for block in cfg.blocks:
        fact = in_fact[block.id]
        for node in _operations(block):
            if fact and node.type == "call_expression":
                released.append(node)
            fact = _transfer([node], fact, key, events_of)
    return released
