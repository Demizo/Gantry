from __future__ import annotations

import asyncio
import dataclasses
import hashlib
from collections import deque
from pathlib import Path

import cbor2
import pytest

from stow_client import StowClient, StowTransport
from stow_client.descriptions import ConstraintKey as K
from stow_client.descriptions import DescribeKey, ItemType, Storage
from stow_client.descriptions import EnumConstraintKey as E
from stow_client.descriptions import FieldConstraintKey as F
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
# Description fixture
# ---------------------------------------------------------------------------


ROLE_NAMES = ["Any", "Session", "Dev", "Guest"]


def _mask(*roles: str) -> int:
    return sum(1 << ROLE_NAMES.index(role) for role in roles)


def _item(
    item_id: int,
    name: str,
    categories: list[str],
    storage: Storage,
    read_perm: int,
    write_perm: int,
    item_type: ItemType,
    default,
    constraints,
) -> dict:
    return {
        DescribeKey.ID: item_id,
        DescribeKey.NAME: name,
        DescribeKey.CATEGORIES: categories,
        DescribeKey.STORAGE: storage,
        DescribeKey.READ_PERM: read_perm,
        DescribeKey.WRITE_PERM: write_perm,
        DescribeKey.TYPE: item_type,
        DescribeKey.DEFAULT: default,
        DescribeKey.CONSTRAINTS: constraints,
    }


def sample_description_payload() -> list[dict]:
    """A description that exercises every supported item type."""
    e, p = Storage.EPHEMERAL, Storage.PERSISTENT
    anyone = _mask("Any")
    return [
        _item(
            0,
            "StowHash",
            [],
            e,
            anyone,
            0,
            ItemType.STRING,
            "0" * 64,
            {K.MIN: 64, K.MAX: 64},
        ),
        _item(
            1,
            "DeviceName",
            ["System"],
            p,
            anyone,
            _mask("Session"),
            ItemType.STRING,
            "Gantry",
            {K.MIN: 3, K.MAX: 25},
        ),
        _item(
            2,
            "TestInt",
            ["Test"],
            e,
            anyone,
            anyone,
            ItemType.INT,
            0,
            {K.MIN: -100, K.MAX: 100},
        ),
        _item(
            3,
            "TestFloat",
            ["Test"],
            e,
            anyone,
            anyone,
            ItemType.FLOAT,
            21.5,
            {K.MIN: 15.0, K.MAX: 30.0},
        ),
        _item(
            4,
            "TestEnum",
            ["Test"],
            e,
            anyone,
            anyone,
            ItemType.ENUM,
            0,
            [
                {E.VALUE: 0, E.NAME: "DISABLED"},
                {E.VALUE: 1, E.NAME: "ENABLED"},
            ],
        ),
        _item(
            5,
            "TestBytes",
            ["Test"],
            e,
            anyone,
            anyone,
            ItemType.BYTE_ARRAY,
            b"\xde\xad\xbe\xef",
            {K.MIN: 0, K.MAX: 12},
        ),
        _item(
            6,
            "TestBuffer",
            ["Test"],
            e,
            anyone,
            anyone,
            ItemType.BUFFER,
            b"",
            {K.MIN: 0, K.MAX: 20},
        ),
        _item(
            7,
            "SpiTxBuffer",
            ["SPI"],
            e,
            anyone,
            anyone,
            ItemType.STRUCT,
            [0, b"\x01\x02\x03", "Test", b""],
            [
                {
                    F.NAME: "CS",
                    F.TYPE: ItemType.ENUM,
                    F.CONSTRAINTS: [
                        {E.VALUE: 0, E.NAME: "SPI_CS0"},
                        {E.VALUE: 1, E.NAME: "SPI_CS1"},
                    ],
                },
                {
                    F.NAME: "Bytes",
                    F.TYPE: ItemType.BYTE_ARRAY,
                    F.CONSTRAINTS: {K.MIN: 0, K.MAX: 6},
                },
                {
                    F.NAME: "Text",
                    F.TYPE: ItemType.STRING,
                    F.CONSTRAINTS: {K.MIN: 3, K.MAX: 12},
                },
                {
                    F.NAME: "Buffer",
                    F.TYPE: ItemType.BUFFER,
                    F.CONSTRAINTS: {K.MIN: 0, K.MAX: 512},
                },
            ],
        ),
    ]


def sample_description_blob() -> bytes:
    return b"".join(cbor2.dumps(item) for item in sample_description_payload())


def sample_description_hash() -> str:
    return hashlib.sha256(sample_description_blob()).hexdigest()


def hello_response(digest: str, roles: list[str] | None = None) -> HelloResponse:
    return HelloResponse(
        version=1,
        fields={
            HelloKey.MAX_MESSAGE: 1024,
            HelloKey.STOW_HASH: digest,
            HelloKey.ROLES: _mask(*(roles if roles is not None else ["Session"])),
            HelloKey.ROLE_NAMES: ROLE_NAMES,
        },
    )


# ---------------------------------------------------------------------------
# Pytest fixtures
# ---------------------------------------------------------------------------


@pytest.fixture
def transport() -> MockTransport:
    return MockTransport()


@pytest.fixture
def cache_dir(tmp_path: Path) -> Path:
    return tmp_path / "cache"


async def _run_load(
    client: StowClient, transport: MockTransport, *, chunk_size: int = 64
) -> None:
    blob = sample_description_blob()
    digest = sample_description_hash()

    transport.reply(hello_response(digest))

    chunks = [blob[i : i + chunk_size] for i in range(0, len(blob), chunk_size)]
    for idx, chunk in enumerate(chunks):
        next_item_id = idx + 1 if idx < len(chunks) - 1 else None
        transport.reply(DescribeResponse(next_item_id=next_item_id, chunk=chunk))

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
    "MockTransport",
    "hello_response",
    "sample_description_blob",
    "sample_description_hash",
    "sample_description_payload",
]
