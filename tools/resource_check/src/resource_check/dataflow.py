from collections.abc import Callable
from dataclasses import dataclass
from typing import Literal, NamedTuple

from tree_sitter import Node

from .annotations import ANY_CLASS
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
    return event.resource_class == resource_class


def _transfer(
    block: BasicBlock,
    fact: bool,
    key: Key,
    events_of: EventsOf,
    skip_acquires: bool = False,
    skip_call: Node | None = None,
) -> bool:
    for call in block.calls:
        for event in events_of(call):
            if not affects(event, key):
                continue
            if event.kind == "release":
                fact = True
            elif not skip_acquires and call != skip_call:
                fact = False
    return fact


FailureEdges = dict[tuple[int, int], Node]


def find_unmatched_exits(
    cfg: CFG, key: Key, events_of: EventsOf, failure_edges: FailureEdges | None = None
) -> list[BasicBlock]:
    failure_edges = failure_edges or {}
    in_fact = {block.id: True for block in cfg.blocks}
    out_fact = {block.id: True for block in cfg.blocks}

    def edge_fact(predecessor: BasicBlock, block: BasicBlock) -> bool:
        skipped = failure_edges.get((predecessor.id, block.id))
        if skipped is None:
            return out_fact.get(predecessor.id, True)
        return _transfer(
            predecessor,
            in_fact.get(predecessor.id, True),
            key,
            events_of,
            skip_call=skipped,
        )

    changed = True
    while changed:
        changed = False
        for block in cfg.blocks:
            in_fact[block.id] = all(
                edge_fact(predecessor, block) for predecessor in block.predecessors
            )
            new_out = _transfer(block, in_fact[block.id], key, events_of)
            if new_out != out_fact[block.id]:
                out_fact[block.id] = new_out
                changed = True

    return [
        block
        for block in cfg.blocks
        if block.is_return
        and not _transfer(block, in_fact[block.id], key, events_of, skip_acquires=True)
    ]
