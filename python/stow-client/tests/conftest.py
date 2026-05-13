from __future__ import annotations

import asyncio
import hashlib
from pathlib import Path

import cbor2
import pytest

from stow_client import StowClient, StowTransport
from stow_client.messages import (
    DescribeResponse,
    Error,
    GetResponse,
    Ok,
    Update,
    VersionResponse,
)


class MockTransport(StowTransport):
    """In-memory async transport with a scripted incoming-frame queue."""

    def __init__(self) -> None:
        self._incoming: asyncio.Queue[bytes] = asyncio.Queue()
        self.outgoing: list[bytes] = []
        self.closed = False

    async def send(self, data: bytes) -> None:
        self.outgoing.append(data)

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


def sample_description_payload() -> list[dict]:
    """A description that exercises every supported item type."""
    return [
        {
            "id": 0,
            "name": "StowHash",
            "categories": [],
            "storage": "Ephemeral",
            "read_perm": ["Any"],
            "write_perm": [],
            "type": "String",
            "default": "0" * 64,
            "constraints": {"min_len": 64, "max_len": 64},
        },
        {
            "id": 1,
            "name": "DeviceName",
            "categories": ["System"],
            "storage": "Persistent",
            "read_perm": ["Any"],
            "write_perm": ["Session"],
            "type": "String",
            "default": "Gantry",
            "constraints": {"min_len": 3, "max_len": 25},
        },
        {
            "id": 2,
            "name": "TestInt",
            "categories": ["Test"],
            "storage": "Ephemeral",
            "read_perm": ["Any"],
            "write_perm": ["Any"],
            "type": "Int",
            "default": 0,
            "constraints": {"min": -100, "max": 100},
        },
        {
            "id": 3,
            "name": "TestFloat",
            "categories": ["Test"],
            "storage": "Ephemeral",
            "read_perm": ["Any"],
            "write_perm": ["Any"],
            "type": "Float",
            "default": 21.5,
            "constraints": {"min": 15.0, "max": 30.0},
        },
        {
            "id": 4,
            "name": "TestEnum",
            "categories": ["Test"],
            "storage": "Ephemeral",
            "read_perm": ["Any"],
            "write_perm": ["Any"],
            "type": "Enum",
            "default": 0,
            "constraints": [
                {"value": 0, "name": "DISABLED"},
                {"value": 1, "name": "ENABLED"},
            ],
        },
        {
            "id": 5,
            "name": "TestBytes",
            "categories": ["Test"],
            "storage": "Ephemeral",
            "read_perm": ["Any"],
            "write_perm": ["Any"],
            "type": "Byte Array",
            "default": b"\xde\xad\xbe\xef",
            "constraints": {"min_len": 0, "max_len": 12},
        },
        {
            "id": 6,
            "name": "TestBuffer",
            "categories": ["Test"],
            "storage": "Ephemeral",
            "read_perm": ["Any"],
            "write_perm": ["Any"],
            "type": "Buffer",
            "default": b"",
            "constraints": {"min_len": 0, "max_len": 20},
        },
        {
            "id": 7,
            "name": "SpiTxBuffer",
            "categories": ["SPI"],
            "storage": "Ephemeral",
            "read_perm": ["Any"],
            "write_perm": ["Any"],
            "type": "Struct",
            "default": [0, b"\x01\x02\x03", "Test", b""],
            "constraints": [
                {
                    "name": "CS",
                    "type": "Enum",
                    "constraints": [
                        {"value": 0, "name": "SPI_CS0"},
                        {"value": 1, "name": "SPI_CS1"},
                    ],
                },
                {
                    "name": "Bytes",
                    "type": "Byte Array",
                    "constraints": {"min_len": 0, "max_len": 6},
                },
                {
                    "name": "Text",
                    "type": "String",
                    "constraints": {"min_len": 3, "max_len": 12},
                },
                {
                    "name": "Buffer",
                    "type": "Buffer",
                    "constraints": {"min_len": 0, "max_len": 512},
                },
            ],
        },
    ]


def sample_description_blob() -> bytes:
    return cbor2.dumps(sample_description_payload())


def sample_description_hash() -> str:
    return hashlib.sha256(sample_description_blob()).hexdigest()


# ---------------------------------------------------------------------------
# Pytest fixtures
# ---------------------------------------------------------------------------


@pytest.fixture
def transport() -> MockTransport:
    return MockTransport()


@pytest.fixture
def cache_dir(tmp_path: Path) -> Path:
    return tmp_path / "cache"


async def _run_load(client: StowClient, transport: MockTransport, *, chunk_size: int = 64) -> None:
    blob = sample_description_blob()
    digest = sample_description_hash()

    transport.push(VersionResponse(version=1))
    transport.push(GetResponse(item_id=0, value=digest))

    # Stateless describe: each DescribeResponse carries next_item_id and has_more
    chunks = [blob[i : i + chunk_size] for i in range(0, len(blob), chunk_size)]
    for idx, chunk in enumerate(chunks):
        has_more = idx < len(chunks) - 1
        next_item_id = idx + 1 if has_more else 0
        transport.push(DescribeResponse(next_item_id=next_item_id, has_more=has_more, chunk=chunk))

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
    "sample_description_blob",
    "sample_description_hash",
    "sample_description_payload",
]
