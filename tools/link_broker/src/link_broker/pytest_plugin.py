"""Pytest plugin that builds a topology's nodes and starts a fresh network for each test."""

from __future__ import annotations

import logging
import subprocess
from collections.abc import Callable, Iterator
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
    link_changed: LinkChanged

    def __getitem__(self, name: str) -> Node:
        return self.nodes[name]

    def link_down(self, link: str) -> None:
        """Cut a link in the broker, then tell the nodes on it."""
        self.broker.cut(link)
        self.link_changed(self, link, False)

    def link_up(self, link: str) -> None:
        """Restore a link in the broker, then tell the nodes on it."""
        self.broker.restore(link)
        self.link_changed(self, link, True)


LinkChanged = Callable[[Network, str, bool], None]


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
def link_changed() -> LinkChanged:
    """A callable ``(network, link, up)`` that tells the nodes on a link it went up or down; a suite overrides this."""
    pytest.fail(
        "Override the link_changed fixture to tell the nodes on a link that it went up or down"
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
    link_changed: LinkChanged,
    link_topology: topology.Topology,
    node_images: dict[str, Path],
    tmp_path: Path,
) -> Iterator[Network]:
    """Start every node and the broker with every link up, logging to the test's ``tmp_path``."""
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
        network = Network(link_topology, nodes, broker, link_changed)
        for link in link_topology.links:
            link_changed(network, link.name, True)
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
