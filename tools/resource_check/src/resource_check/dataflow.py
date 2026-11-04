from collections.abc import Callable, Collection
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
    on_write: bool | None = None,
    skip_acquires: bool = False,
    skip_call: Node | None = None,
) -> bool:
    """Whether `key` is released after `nodes`, given whether it was before.

    `on_write` is the fact after an assignment to the identifier, or None to ignore assignments.
    """
    for node in nodes:
        if on_write is not None and text(written_identifier(node)) == key.identifier:
            fact = on_write
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


Edge = tuple[int, int]
FailureEdges = dict[Edge, Node]


def _edge_transfer(
    key: Key,
    events_of: EventsOf,
    failure_edges: FailureEdges,
    empty_edges: Collection[Edge],
) -> EdgeTransfer:
    """Nothing is held along an empty edge, and a failure edge skips the acquire that failed."""

    def edge(predecessor: BasicBlock, block: BasicBlock, fact: bool) -> bool | None:
        if (predecessor.id, block.id) in empty_edges:
            return True
        skipped = failure_edges.get((predecessor.id, block.id))
        if skipped is None:
            return None
        return _transfer(
            _operations(predecessor), fact, key, events_of, skip_call=skipped
        )

    return edge


def find_unmatched_exits(
    cfg: CFG,
    key: Key,
    events_of: EventsOf,
    failure_edges: FailureEdges,
    held_at_entry: bool,
    empty_edges: Collection[Edge],
) -> list[BasicBlock]:
    in_fact = _solve(
        cfg,
        not held_at_entry,
        lambda block, fact: _transfer(_operations(block), fact, key, events_of),
        _edge_transfer(key, events_of, failure_edges, empty_edges),
    )
    return [
        block
        for block in cfg.blocks
        if block.is_return
        and not _transfer(
            _operations(block), in_fact[block.id], key, events_of, skip_acquires=True
        )
    ]


def _operations(block: BasicBlock) -> list[Node]:
    # End offsets give evaluation order: arguments before their call, a call before the assignment of its result.
    nodes = [*block.calls, *block.writes]
    if block.return_node is not None:
        nodes.append(block.return_node)
    return sorted(
        nodes, key=lambda node: (node.end_byte, node.type != "call_expression")
    )


def find_released_calls(cfg: CFG, key: Key, events_of: EventsOf) -> list[Node]:
    """Calls reached only on paths where `key` is released and not acquired or assigned since."""
    in_fact = _solve(
        cfg,
        False,
        lambda block, fact: _transfer(
            _operations(block), fact, key, events_of, on_write=False
        ),
    )
    released: list[Node] = []
    for block in cfg.blocks:
        fact = in_fact[block.id]
        for node in _operations(block):
            if fact and node.type == "call_expression":
                released.append(node)
            fact = _transfer([node], fact, key, events_of, on_write=False)
    return released
