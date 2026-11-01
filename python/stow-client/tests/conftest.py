from __future__ import annotations

import asyncio
import dataclasses
import io
from collections import deque
from pathlib import Path

import cbor2
import pytest

from protocol_spec import load
from stow_client import StowClient, StowTransport
from stow_client.descriptions import DescribeKey
from stow_client.messages import (
    DescribeResponse,
    HelloKey,
    HelloResponse,
    Message,
    Response,
    decode,
)


class MockTransport(StowTransport):
    """In-memory transport: ``reply`` answers the next request with its tag, ``push`` sends as is."""

    def __init__(self) -> None:
        self._incoming: asyncio.Queue[bytes] = asyncio.Queue()
        self._replies: deque[Message] = deque()
        self.outgoing: list[bytes] = []
        self.closed = False

    async def send(self, data: bytes) -> None:
        self.outgoing.append(data)
        if not self._replies:
            return
        reply = self._replies.popleft()
        if isinstance(reply, Response) and reply.tag is not None:
            reply = dataclasses.replace(reply, tag=decode(data).tag)
        self._incoming.put_nowait(reply.encode())

    def reply(self, message: Message) -> None:
        self._replies.append(message)

    async def receive(self) -> bytes:
        return await self._incoming.get()

    async def close(self) -> None:
        self.closed = True

    def push(self, message) -> None:
        self._incoming.put_nowait(message.encode())

    def push_raw(self, frame: bytes) -> None:
        self._incoming.put_nowait(frame)


# ---------------------------------------------------------------------------
# The protocol fixture, as the connect vector describes it
# ---------------------------------------------------------------------------


VECTORS = load(Path(__file__).resolve().parents[3] / "protocols" / "stow")
CONNECT = [
    decode(step.data)
    for step in next(
        vector for vector in VECTORS.exchanges if vector.name == "connect"
    ).steps
]
HELLO = next(message for message in CONNECT if isinstance(message, HelloResponse))
DESCRIBE_PAGES = [
    message for message in CONNECT if isinstance(message, DescribeResponse)
]
ROLE_NAMES = HELLO.role_names
STOW_HASH = HELLO.stow_hash


def description_blob() -> bytes:
    return b"".join(page.chunk for page in DESCRIBE_PAGES)


def description_items() -> list[dict]:
    """Each description map the pages hold."""
    blob = io.BytesIO(description_blob())
    items = []
    while blob.tell() < len(blob.getvalue()):
        items.append(cbor2.load(blob))
    return items


def item_id(name: str) -> int:
    return next(
        item[DescribeKey.ID]
        for item in description_items()
        if item[DescribeKey.NAME] == name
    )


def hello_response(roles: list[str] | None = None) -> HelloResponse:
    """The connect vector's Hello Response, granting roles instead when given."""
    if roles is None:
        return HELLO
    mask = sum(1 << ROLE_NAMES.index(role) for role in roles)
    return dataclasses.replace(HELLO, fields={**HELLO.fields, HelloKey.ROLES: mask})


# ---------------------------------------------------------------------------
# Pytest fixtures
# ---------------------------------------------------------------------------


@pytest.fixture
def transport() -> MockTransport:
    return MockTransport()


@pytest.fixture
def cache_dir(tmp_path: Path) -> Path:
    return tmp_path / "cache"


async def _run_load(client: StowClient, transport: MockTransport) -> None:
    for message in (HELLO, *DESCRIBE_PAGES):
        transport.reply(message)
    await client.load()


@pytest.fixture
async def loaded_client(transport: MockTransport, cache_dir: Path):
    client = StowClient(transport, cache_dir=cache_dir)
    await _run_load(client, transport)
    try:
        yield client
    finally:
        await client._receiver.stop()


__all__ = [
    "DESCRIBE_PAGES",
    "HELLO",
    "ROLE_NAMES",
    "STOW_HASH",
    "VECTORS",
    "MockTransport",
    "description_blob",
    "description_items",
    "hello_response",
    "item_id",
]
