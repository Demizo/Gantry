"""Pytest plugin: builds the nodes of a topology, then runs each test's network.

A test suite names its topology by overriding the ``link_topology_path``
fixture. Each test that uses ``network`` gets freshly started nodes and a
broker joining them, with every link up.

Nodes stand in for their link manager through console commands: ``link up``
answers ``@link up <err>``, ``link down`` answers ``@link down``, and
``peer connect`` opens the client session once both ends are up.
"""

from __future__ import annotations

import logging
import subprocess
from collections.abc import Iterator
from dataclasses import dataclass
from pathlib import Path

import pytest

from . import topology
from .broker import Broker
from .node import Node

BOARD = "native_sim/native/64"


@dataclass
class Network:
    """The running nodes of a topology and the broker joining them."""

    topology: topology.Topology
    nodes: dict[str, Node]
    broker: Broker

    def __getitem__(self, name: str) -> Node:
        return self.nodes[name]

    def link_down(self, link: str) -> None:
        """Cut a link in the broker, then tell each node on it."""
        self.broker.cut(link)
        for node in self._ends(link):
            _tell(node, "link down", r"^@link down$")

    def link_up(self, link: str) -> None:
        """Restore a link in the broker, then bring it up on each node."""
        self.broker.restore(link)
        self.bring_up(link)

    def bring_up(self, link: str) -> None:
        """Tell each node on a link that it is up, then open their client sessions."""
        ends = self._ends(link)
        for node in ends:
            _tell(node, "link up", r"^@link up (-?\d+)$")
        for node in ends:
            _tell(node, "peer connect", r"^@connect (-?\d+)$")

    def _ends(self, link: str) -> list[Node]:
        return [self.nodes[end] for end in self.topology.link(link).ends]


def _tell(node: Node, line: str, answer: str) -> None:
    """Send a console line and wait for its answer, failing if it captures a non-zero error."""
    mark = node.mark()
    node.send(line)
    match = node.expect(answer, since=mark)
    if match.re.groups and match.group(1) != "0":
        raise RuntimeError(f"{node.name}: {line!r} failed with {match.group(1)}")


def pytest_addoption(parser: pytest.Parser) -> None:
    parser.addoption(
        "--build-dir",
        type=Path,
        help="Directory for node builds, one subdirectory per node; default: build/ next to the topology",
    )


@pytest.fixture(scope="session")
def link_topology_path() -> Path:
    """Path of the topology file; a suite overrides this."""
    pytest.fail(
        "Override the link_topology_path fixture with the suite's topology file"
    )


@pytest.fixture(scope="session")
def link_topology(link_topology_path: Path) -> topology.Topology:
    return topology.load(link_topology_path)


@pytest.fixture(scope="session")
def node_images(
    request: pytest.FixtureRequest,
    link_topology: topology.Topology,
    link_topology_path: Path,
) -> dict[str, Path]:
    """Build each node's app for native_sim with west, once per session."""
    build_dir = (
        request.config.getoption("--build-dir") or link_topology_path.parent / "build"
    )
    images = {}
    for name, node in link_topology.nodes.items():
        node_dir = build_dir / name
        command = [
            "west",
            "build",
            "-p",
            "auto",
            "-b",
            BOARD,
            "-d",
            str(node_dir),
            str(node.app),
        ]
        result = subprocess.run(command, capture_output=True, text=True)
        if result.returncode != 0:
            pytest.fail(
                f"Building node {name} failed:\n{result.stdout[-4000:]}{result.stderr[-4000:]}"
            )
        images[name] = node_dir / "zephyr" / "zephyr.exe"
    return images


@pytest.fixture
def network(
    link_topology: topology.Topology, node_images: dict[str, Path], tmp_path: Path
) -> Iterator[Network]:
    """Start every node, then the broker; stop them all after the test.

    The frame log and each node's console are kept in the test's temporary directory.
    """
    handler = logging.FileHandler(tmp_path / "frames.log")
    logger = logging.getLogger("link_broker")
    logger.addHandler(handler)
    level = logger.level
    logger.setLevel(logging.INFO)
    nodes = {
        name: Node(name, node_images[name], tmp_path / name)
        for name in link_topology.nodes
    }
    broker = None
    try:
        for node in nodes.values():
            node.start()
        broker = Broker(link_topology, {name: node.pty for name, node in nodes.items()})
        broker.start()
        network = Network(link_topology, nodes, broker)
        for link in link_topology.links:
            network.bring_up(link.name)
        yield network
    finally:
        if broker is not None:
            broker.stop()
        for node in nodes.values():
            node.stop()
            (tmp_path / f"{node.name}.log").write_text("\n".join(node.lines) + "\n")
        logger.removeHandler(handler)
        logger.setLevel(level)
        handler.close()
