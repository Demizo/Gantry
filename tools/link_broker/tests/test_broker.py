from __future__ import annotations

import os
import select
import time
from pathlib import Path

import pytest
from link_broker import Action, Broker, Faults, Link, Topology
from link_broker.topology import Node, parse

from stow_client import GetRequest
from stow_client.cobs import cobs_encode


class End:
    """The node side of a PTY the broker opens."""

    def __init__(self) -> None:
        self.master, self.slave = os.openpty()
        self.path = os.ttyname(self.slave)

    def send(self, data: bytes) -> None:
        os.write(self.master, data)

    def receive(self, timeout: float = 1.0) -> bytes:
        """Everything that arrives within ``timeout``."""
        data = b""
        deadline = time.monotonic() + timeout
        while (remaining := deadline - time.monotonic()) > 0:
            ready, _, _ = select.select([self.master], [], [], remaining)
            if ready:
                data += os.read(self.master, 4096)
        return data

    def close(self) -> None:
        os.close(self.master)
        os.close(self.slave)


FRAME = cobs_encode(GetRequest(tag=3, item_ids=(1,)).encode())


@pytest.fixture
def ends():
    made = {"a": End(), "b": End()}
    yield made
    for end in made.values():
        end.close()


@pytest.fixture
def broker(ends):
    def make(faults=None, seed=1):
        topo = Topology(
            {"a": Node("a", Path("a")), "b": Node("b", Path("b"))},
            [Link("a-b", ("a", "b"), faults or {})],
            seed,
        )
        made = Broker(topo, {name: end.path for name, end in ends.items()})
        made.start()
        brokers.append(made)
        return made

    brokers: list[Broker] = []
    yield make
    for made in brokers:
        made.stop()


def test_forwards_frames_both_ways(ends, broker):
    broker()
    ends["a"].send(FRAME)
    ends["b"].send(FRAME + FRAME)
    assert ends["b"].receive() == FRAME
    assert ends["a"].receive() == FRAME + FRAME


def test_frame_split_across_reads_is_forwarded_whole(ends, broker):
    b = broker()
    ends["a"].send(FRAME[:3])
    time.sleep(0.1)
    ends["a"].send(FRAME[3:])
    assert ends["b"].receive() == FRAME
    assert [r.action for r in b.frames] == [Action.FORWARD]


def test_logs_frames_as_diagnostic_notation(ends, broker):
    b = broker()
    ends["a"].send(FRAME)
    ends["b"].receive()
    (record,) = b.frames
    assert (record.link, record.src, record.dst) == ("a-b", "a", "b")
    assert record.summary == "[4,3,1]"


def test_idle_is_forwarded_and_not_logged(ends, broker):
    b = broker(faults={("a", "b"): Faults(drop=1.0)})
    ends["a"].send(b"\0\0")
    assert ends["b"].receive() == b"\0\0"
    assert b.frames == []


def test_drop_next_drops_only_that_many(ends, broker):
    b = broker()
    b.drop_next("a", "b", 2)
    ends["a"].send(FRAME * 3)
    assert ends["b"].receive() == FRAME
    assert [r.action for r in b.frames] == [Action.DROP, Action.DROP, Action.FORWARD]
    ends["b"].send(FRAME)
    assert ends["a"].receive() == FRAME


def test_cut_drops_everything_until_restored(ends, broker):
    b = broker()
    b.cut("a-b")
    ends["a"].send(FRAME + b"\0")
    ends["b"].send(FRAME)
    assert ends["b"].receive() == b""
    assert ends["a"].receive() == b""
    assert [r.action for r in b.frames] == [Action.CUT, Action.CUT]
    b.restore("a-b")
    ends["a"].send(FRAME)
    assert ends["b"].receive() == FRAME


def test_cut_drops_delayed_frames_in_flight(ends, broker):
    b = broker(faults={("a", "b"): Faults(delay_ms=300)})
    ends["a"].send(FRAME)
    time.sleep(0.1)
    b.cut("a-b")
    assert ends["b"].receive(0.5) == b""
    assert [r.action for r in b.frames] == [Action.DELAY, Action.CUT]


def test_delay_holds_frames(ends, broker):
    b = broker()
    b.set_faults("a", "b", delay_ms=300)
    ends["a"].send(FRAME)
    assert ends["b"].receive(0.15) == b""
    assert ends["b"].receive(0.5) == FRAME
    ends["b"].send(FRAME)
    assert ends["a"].receive(0.15) == FRAME


def test_duplicate_sends_frames_twice(ends, broker):
    broker(faults={("a", "b"): Faults(duplicate=1.0)})
    ends["a"].send(FRAME)
    assert ends["b"].receive() == FRAME + FRAME


def test_corrupt_flips_one_bit_and_keeps_framing(ends, broker):
    broker(faults={("a", "b"): Faults(corrupt=1.0)})
    ends["a"].send(FRAME * 20)
    received = ends["b"].receive()
    frames = received.split(b"\0")[:-1]
    assert len(frames) == 20
    for frame in frames:
        diff = [x ^ y for x, y in zip(frame, FRAME[:-1], strict=True) if x != y]
        assert len(diff) == 1 and bin(diff[0]).count("1") == 1


def test_seeded_drops_repeat(ends, broker):
    def run():
        b = broker(faults={("a", "b"): Faults(drop=0.5)}, seed=7)
        ends["a"].send(FRAME * 16)
        ends["b"].receive(0.5)
        actions = [r.action for r in b.frames]
        b.stop()
        return actions

    first = run()
    assert Action.DROP in first and Action.FORWARD in first
    assert run() == first


def test_restoring_within_a_delay_still_drops_frames_in_flight(ends, broker):
    b = broker(faults={("a", "b"): Faults(delay_ms=300)})
    ends["a"].send(FRAME)
    time.sleep(0.1)
    b.cut("a-b")
    b.restore("a-b")
    assert ends["b"].receive(0.5) == b""
    assert [r.action for r in b.frames] == [Action.DELAY, Action.CUT]


def test_topology_file_faults_apply(ends):
    topo = parse(
        {
            "nodes": {"a": {"app": "a"}, "b": {"app": "b"}},
            "links": [{"ends": ["a", "b"], "faults": {"a->b": {"drop": 1}}}],
        },
        Path(),
    )
    with Broker(topo, {name: end.path for name, end in ends.items()}) as b:
        ends["a"].send(FRAME)
        ends["b"].send(FRAME)
        assert ends["b"].receive() == b""
        assert ends["a"].receive() == FRAME
        actions = {(r.src, r.action) for r in b.frames}
        assert actions == {("a", Action.DROP), ("b", Action.FORWARD)}
