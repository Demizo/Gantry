"""The link broker: joins node UARTs by a topology, injects faults and logs every frame.

Frames are COBS frames, split on the 0x00 delimiter, each carrying one CBOR
item. An empty frame, a lone delimiter, is forwarded unless the link is cut,
but never faulted or logged.
"""

from __future__ import annotations

import asyncio
import logging
import os
import random
import threading
import time
import tty
from collections.abc import Callable
from dataclasses import dataclass, replace
from enum import StrEnum
from typing import Any

import cbor_diag

from stow_client.cobs import cobs_decode

from .topology import Faults, Link, Topology

log = logging.getLogger("link_broker")

_READ_SIZE = 4096


def describe(frame: bytes) -> str:
    """A frame's CBOR in diagnostic notation, or why it has none."""
    try:
        payload = cobs_decode(frame)
    except ValueError as e:
        return f"bad COBS: {e}"
    try:
        return cbor_diag.cbor2diag(payload, pretty=False)
    except ValueError:
        return f"not CBOR: h'{payload.hex()}'"


class Action(StrEnum):
    FORWARD = "forward"
    DROP = "drop"
    CUT = "cut"
    DUPLICATE = "duplicate"
    CORRUPT = "corrupt"
    DELAY = "delay"


@dataclass(frozen=True)
class FrameRecord:
    """One frame seen by the broker and what it did with it."""

    time: float
    link: str
    src: str
    dst: str
    action: Action
    frame: bytes
    summary: str

    def __str__(self) -> str:
        return f"{self.time:9.3f} {self.link} {self.src}->{self.dst} {self.action:<9} {self.summary}"


class _Port:
    """The broker's side of one node UART."""

    def __init__(self, name: str, path: str, loop: asyncio.AbstractEventLoop) -> None:
        self.name = name
        self.fd = os.open(path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        tty.setraw(self.fd)
        self._loop = loop
        self._pending = bytearray()
        self._closed = False

    def write(self, data: bytes) -> None:
        """Write to the node, queueing what its PTY cannot take yet."""
        if self._closed:
            return
        was_empty = not self._pending
        self._pending += data
        if was_empty:
            self._flush()
            if self._pending:
                self._loop.add_writer(self.fd, self._flush)

    def discard(self) -> None:
        """Drop whatever is still queued for the node."""
        self._pending.clear()
        self._loop.remove_writer(self.fd)

    def _flush(self) -> None:
        try:
            written = os.write(self.fd, self._pending)
        except BlockingIOError:
            return
        except OSError as e:
            log.warning(
                "%s: write failed, dropping %d bytes: %s",
                self.name,
                len(self._pending),
                e,
            )
            written = len(self._pending)
        del self._pending[:written]
        if not self._pending:
            self._loop.remove_writer(self.fd)

    def close(self) -> None:
        self._closed = True
        self._loop.remove_reader(self.fd)
        self._loop.remove_writer(self.fd)
        os.close(self.fd)


class _Direction:
    """One direction of a link, with its faults."""

    def __init__(
        self, link: _Link, src: _Port, dst: _Port, faults: Faults, seed: int | None
    ) -> None:
        self.link = link
        self.rng = random.Random(
            None if seed is None else f"{seed}:{src.name}->{dst.name}"
        )
        self.src = src
        self.dst = dst
        self.faults = faults
        self.drop_next = 0
        self.buffer = bytearray()


class _Link:
    def __init__(self, spec: Link, ports: dict[str, _Port], seed: int | None) -> None:
        self.name = spec.name
        self.cut = False
        self.epoch = 0
        """Bumped by each cut and restore, so frames delayed across either are dropped."""
        a, b = (ports[end] for end in spec.ends)
        self.directions = {
            (x.name, y.name): _Direction(
                self, x, y, replace(spec.faults.get((x.name, y.name), Faults())), seed
            )
            for x, y in ((a, b), (b, a))
        }


class Broker:
    """Runs the links of a topology on a background event loop.

    ``ports`` maps each link end to the path of its node's UART, a PTY.
    Control calls are thread safe and take effect before they return.
    """

    def __init__(self, topology: Topology, ports: dict[str, str]) -> None:
        self._topology = topology
        self._paths = ports
        self._records: list[FrameRecord] = []
        self._records_lock = threading.Lock()
        self._loop = asyncio.new_event_loop()
        self._thread = threading.Thread(
            target=self._loop.run_forever, name="link-broker", daemon=True
        )
        self._ports: dict[str, _Port] = {}
        self._links: dict[str, _Link] = {}
        self._directions: dict[tuple[str, str], _Direction] = {}
        self._start = time.monotonic()

    def __enter__(self) -> Broker:
        self.start()
        return self

    def __exit__(self, *exc: object) -> None:
        self.stop()

    def start(self) -> None:
        """Open every port and start forwarding."""
        self._thread.start()
        self._call(self._open)

    def stop(self) -> None:
        """Stop forwarding and close every port."""
        if not self._thread.is_alive():
            if not self._loop.is_closed():
                self._loop.close()
            return
        self._call(self._close)
        self._loop.call_soon_threadsafe(self._loop.stop)
        self._thread.join()
        self._loop.close()

    @property
    def frames(self) -> list[FrameRecord]:
        """Every frame logged so far."""
        with self._records_lock:
            return list(self._records)

    def cut(self, link: str) -> None:
        """Cut a link: drop everything in both directions, including frames in flight."""
        self._call(self._set_cut, link, True)

    def restore(self, link: str) -> None:
        """Restore a cut link."""
        self._call(self._set_cut, link, False)

    def set_faults(self, src: str, dst: str, **faults: float) -> None:
        """Change the faults from port ``src`` to port ``dst``; unnamed faults keep their value."""
        self._call(self._set_faults, src, dst, faults)

    def drop_next(self, src: str, dst: str, count: int = 1) -> None:
        """Drop the next ``count`` frames from port ``src`` to port ``dst``."""
        self._call(self._add_drops, src, dst, count)

    def _call(self, fn: Callable[..., Any], *args: Any) -> Any:
        async def run() -> Any:
            return fn(*args)

        return asyncio.run_coroutine_threadsafe(run(), self._loop).result()

    def _open(self) -> None:
        for name, path in self._paths.items():
            self._ports[name] = _Port(name, path, self._loop)
        for spec in self._topology.links:
            link = _Link(spec, self._ports, self._topology.seed)
            self._links[link.name] = link
            self._directions.update(link.directions)
        for direction in self._directions.values():
            self._loop.add_reader(direction.src.fd, self._read, direction)

    def _close(self) -> None:
        for port in self._ports.values():
            port.close()

    def _set_cut(self, name: str, cut: bool) -> None:
        link = self._links[name]
        link.cut = cut
        link.epoch += 1
        for direction in link.directions.values():
            direction.buffer.clear()
            direction.dst.discard()
        log.info("%9.3f %s %s", self._now(), name, "cut" if cut else "restored")

    def _set_faults(self, src: str, dst: str, faults: dict[str, float]) -> None:
        direction = self._directions[(src, dst)]
        direction.faults = replace(direction.faults, **faults)

    def _add_drops(self, src: str, dst: str, count: int) -> None:
        self._directions[(src, dst)].drop_next += count

    def _now(self) -> float:
        return time.monotonic() - self._start

    def _read(self, direction: _Direction) -> None:
        try:
            data = os.read(direction.src.fd, _READ_SIZE)
        except BlockingIOError:
            return
        except OSError:
            data = b""
        if not data:
            self._loop.remove_reader(direction.src.fd)
            return
        direction.buffer += data
        while (end := direction.buffer.find(0)) >= 0:
            frame = bytes(direction.buffer[:end])
            del direction.buffer[: end + 1]
            if direction.link.cut:
                if frame:
                    self._record(direction, Action.CUT, frame)
            elif frame:
                self._frame(direction, frame)
            else:
                direction.dst.write(b"\0")

    def _frame(self, direction: _Direction, frame: bytes) -> None:
        faults = direction.faults
        if direction.drop_next > 0:
            direction.drop_next -= 1
            self._record(direction, Action.DROP, frame)
            return
        if direction.rng.random() < faults.drop:
            self._record(direction, Action.DROP, frame)
            return
        if direction.rng.random() < faults.corrupt:
            frame = _corrupt(direction.rng, frame)
            self._record(direction, Action.CORRUPT, frame)
        copies = 2 if direction.rng.random() < faults.duplicate else 1
        if copies > 1:
            self._record(direction, Action.DUPLICATE, frame)
        if faults.delay_ms > 0:
            self._record(direction, Action.DELAY, frame)
            self._loop.call_later(
                faults.delay_ms / 1000,
                self._deliver,
                direction,
                frame,
                copies,
                direction.link.epoch,
            )
        else:
            self._deliver(direction, frame, copies, direction.link.epoch)

    def _deliver(
        self, direction: _Direction, frame: bytes, copies: int, epoch: int
    ) -> None:
        if direction.link.epoch != epoch:
            self._record(direction, Action.CUT, frame)
            return
        self._record(direction, Action.FORWARD, frame)
        direction.dst.write((frame + b"\0") * copies)

    def _record(self, direction: _Direction, action: Action, frame: bytes) -> None:
        record = FrameRecord(
            self._now(),
            direction.link.name,
            direction.src.name,
            direction.dst.name,
            action,
            frame,
            describe(frame),
        )
        with self._records_lock:
            self._records.append(record)
        log.info("%s", record)


def _corrupt(rng: random.Random, frame: bytes) -> bytes:
    """Flip one bit of a frame, never making a 0x00."""
    index = rng.randrange(len(frame))
    bits = [1 << bit for bit in range(8) if frame[index] != 1 << bit]
    corrupted = bytearray(frame)
    corrupted[index] ^= rng.choice(bits)
    return bytes(corrupted)
