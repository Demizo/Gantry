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


EventsOf = Callable[[Node], list[Event]]


def _transfer(
    block: BasicBlock,
    fact: bool,
    key: Key,
    events_of: EventsOf,
    skip_acquires: bool = False,
) -> bool:
    identifier, resource_class = key
    for call in block.calls:
        for event in events_of(call):
            if event.identifier != identifier:
                continue
            if event.kind == "release" and event.resource_class in (
                resource_class,
                ANY_CLASS,
            ):
                fact = True
            elif (
                event.kind == "acquire"
                and event.resource_class == resource_class
                and not skip_acquires
            ):
                fact = False
    return fact


def _in_fact(block: BasicBlock, out_fact: dict[int, bool]) -> bool:
    return all(out_fact.get(predecessor.id, True) for predecessor in block.predecessors)


def find_unmatched_exits(cfg: CFG, key: Key, events_of: EventsOf) -> list[BasicBlock]:
    out_fact = {block.id: True for block in cfg.blocks}

    changed = True
    while changed:
        changed = False
        for block in cfg.blocks:
            new_out = _transfer(block, _in_fact(block, out_fact), key, events_of)
            if new_out != out_fact[block.id]:
                out_fact[block.id] = new_out
                changed = True

    return [
        block
        for block in cfg.blocks
        if block.is_return
        and not _transfer(
            block, _in_fact(block, out_fact), key, events_of, skip_acquires=True
        )
    ]
