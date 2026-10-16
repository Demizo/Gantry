from __future__ import annotations

import re
from dataclasses import dataclass, field
from pathlib import Path

import pytest
from link_broker import Node


@dataclass
class Result:
    """A request's error and the values it returned."""

    err: int
    values: dict[str, str] = field(default_factory=dict)


class Console:
    """A node's console, as the sample's console commands print it."""

    def __init__(self, node: Node) -> None:
        self.node = node

    def mark(self) -> int:
        return self.node.mark()

    def command(self, line: str, pattern: str) -> re.Match[str]:
        """Send a console line and wait for its answer."""
        mark = self.node.mark()
        self.node.send(line)
        return self.node.expect(pattern, since=mark)

    def request(self, command: str) -> int:
        """Send a request to the peer; returns its request number."""
        return int(self.command(f"peer {command}", r"^@sent (\d+)$").group(1))

    def result(self, request: int, timeout: float = 5.0) -> Result:
        """Wait for a request's result."""
        pattern = rf"^@done {request} (-?\d+)(.*)$"
        return _result(self.node.expect(pattern, timeout=timeout))

    def call(self, command: str) -> Result:
        """Send a request to the peer and wait for its result."""
        return self.result(self.request(command))

    def local(self, command: str) -> Result:
        """Get or set an item of the node's own Stow."""
        return _result(self.command(f"local {command}", r"^@local (-?\d+)(.*)$"))

    def wait(self, pattern: str, since: int = 0, timeout: float = 5.0) -> int:
        """Wait for a console line, such as an event; returns its index."""
        self.node.expect(pattern, since=since, timeout=timeout)
        lines = self.node.lines
        return next(i for i in range(since, len(lines)) if re.search(pattern, lines[i]))

    def seen(self, pattern: str, since: int = 0) -> bool:
        """Whether a console line from ``since`` on matches."""
        return any(re.search(pattern, line) for line in self.node.lines[since:])


def _result(match: re.Match[str]) -> Result:
    """A result from a match of ``<err>`` and `` <item>=<value>...``."""
    values = dict(pair.split("=", 1) for pair in match.group(2).split())
    return Result(int(match.group(1)), values)


@pytest.fixture(scope="session")
def link_topology_path() -> Path:
    return Path(__file__).parent.parent / "topology.yaml"


READY = {
    "controller": r"Watching the sensor's battery",
    "sensor": r"Paired with controller \S+",
}


@pytest.fixture
def consoles(network) -> dict[str, Console]:
    """Each node's console, once both have connected and their apps are done connecting."""
    consoles = {name: Console(node) for name, node in network.nodes.items()}
    for name, console in consoles.items():
        console.wait(r"^@connected 0$")
        console.wait(READY[name])
    return consoles


@pytest.fixture
def controller(consoles) -> Console:
    return consoles["controller"]


@pytest.fixture
def sensor(consoles) -> Console:
    return consoles["sensor"]
