"""Topology files: the nodes of a simulated network and the links between their ports."""

from __future__ import annotations

from dataclasses import dataclass, field, fields
from pathlib import Path

import yaml


@dataclass
class Faults:
    """Faults applied to every frame in one direction of a link."""

    drop: float = 0.0
    duplicate: float = 0.0
    corrupt: float = 0.0
    delay_ms: float = 0.0

    @classmethod
    def from_dict(cls, data: dict) -> Faults:
        """Faults from a topology file's mapping of fault names to values."""
        known = {f.name for f in fields(cls)}
        unknown = set(data) - known
        if unknown:
            raise ValueError(f"Unknown faults: {', '.join(sorted(unknown))}")
        return cls(**{key: float(value) for key, value in data.items()})


@dataclass
class Node:
    """One firmware image, built from the Zephyr app at ``app``."""

    name: str
    app: Path


@dataclass
class Link:
    """A point-to-point link between two nodes, named ``<a>-<b>`` after its ends."""

    name: str
    ends: tuple[str, str]
    faults: dict[tuple[str, str], Faults] = field(default_factory=dict)


@dataclass
class Topology:
    """The nodes and links of a network; ``seed`` makes random faults repeatable."""

    nodes: dict[str, Node]
    links: list[Link]
    seed: int | None = None

    def link(self, name: str) -> Link:
        for link in self.links:
            if link.name == name:
                return link
        raise KeyError(f"No link named {name}")


def _parse_direction(key: str, ends: tuple[str, str]) -> tuple[str, str]:
    src, sep, dst = (part.strip() for part in key.partition("->"))
    if not sep or {src, dst} != set(ends):
        raise ValueError(
            f"Fault direction {key!r} must be one of the link's ends, as 'a->b'"
        )
    return src, dst


def parse(data: dict, base: Path) -> Topology:
    """Build a topology from parsed YAML; app paths are relative to ``base``."""
    nodes = {}
    for name, spec in (data.get("nodes") or {}).items():
        if not spec or "app" not in spec:
            raise ValueError(f"Node {name!r} has no app")
        nodes[name] = Node(name, base / spec["app"])

    links = []
    used: set[str] = set()
    for spec in data.get("links") or []:
        ends = tuple(spec["ends"])
        if len(ends) != 2:
            raise ValueError(f"Link {ends} must join exactly two ports")
        for port in ends:
            if port not in nodes:
                raise ValueError(f"Link end {port!r} is not a node")
            if port in used:
                raise ValueError(f"Port {port!r} is on more than one link")
            used.add(port)
        faults = {
            _parse_direction(key, ends): Faults.from_dict(value or {})
            for key, value in (spec.get("faults") or {}).items()
        }
        links.append(Link("-".join(ends), ends, faults))

    return Topology(nodes, links, data.get("seed"))


def load(path: Path) -> Topology:
    """Load a topology file."""
    return parse(yaml.safe_load(Path(path).read_text()) or {}, Path(path).parent)
