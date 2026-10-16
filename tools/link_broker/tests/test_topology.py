from __future__ import annotations

from pathlib import Path

import pytest
from link_broker import Faults, describe, load
from link_broker.topology import parse

from stow_client.cobs import cobs_encode


def test_loads_nodes_links_and_faults(tmp_path: Path):
    path = tmp_path / "topology.yaml"
    path.write_text(
        """
seed: 3
nodes:
  controller: {app: controller}
  sensor: {app: ../sensor}
links:
  - ends: [controller, sensor]
    faults:
      controller->sensor: {drop: 0.5, delay_ms: 20}
"""
    )
    topo = load(path)
    assert topo.seed == 3
    assert topo.nodes["controller"].app == tmp_path / "controller"
    assert topo.nodes["sensor"].app == tmp_path / ".." / "sensor"
    link = topo.link("controller-sensor")
    assert link.faults == {("controller", "sensor"): Faults(drop=0.5, delay_ms=20)}


@pytest.mark.parametrize(
    ("links", "message"),
    [
        ([{"ends": ["a"]}], "exactly two"),
        ([{"ends": ["a", "x"]}], "not a node"),
        (
            [{"ends": ["a", "b"]}, {"ends": ["b", "a"]}],
            "more than one link",
        ),
        ([{"ends": ["a", "b"], "faults": {"a->a": {}}}], "direction"),
        ([{"ends": ["a", "b"], "faults": {"a->b": {"lose": 1}}}], "Unknown faults"),
    ],
)
def test_rejects_bad_links(links, message):
    with pytest.raises(ValueError, match=message):
        parse({"nodes": {"a": {"app": "a"}, "b": {"app": "b"}}, "links": links}, Path())


def test_rejects_nodes_without_an_app():
    with pytest.raises(ValueError, match="no app"):
        parse({"nodes": {"a": {}}}, Path())


def test_describes_frames():
    update = cobs_encode(bytes.fromhex("8409f6021837"))[:-1]
    assert describe(update) == "[9,null,2,55]"
    assert describe(b"\x03\x01").startswith("bad COBS")
    assert describe(cobs_encode(b"\xff")[:-1]) == "not CBOR: h'ff'"
