from __future__ import annotations

import asyncio
from pathlib import Path

import pytest

from stow_client import (
    StowClient,
    StowError,
    StowNotLoadedError,
    StowVersionMismatch,
)
from stow_client.messages import (
    DescribeResponse,
    Error,
    GetResponse,
    MultiGetRequest,
    MultiGetResponse,
    MultiSetRequest,
    Ok,
    SetRequest,
    SubscribeRequest,
    UnsubscribeRequest,
    Update,
    VersionResponse,
    decode,
)

from .conftest import (
    MockTransport,
    sample_description_blob,
    sample_description_hash,
)


@pytest.fixture
async def client(transport: MockTransport, cache_dir: Path):
    c = StowClient(transport, cache_dir=cache_dir)
    yield c
    await c._receiver.stop()


async def test_load_happy_path(transport: MockTransport, cache_dir: Path):
    client = StowClient(transport, cache_dir=cache_dir)
    blob = sample_description_blob()
    digest = sample_description_hash()

    transport.push(VersionResponse(version=1))
    transport.push(GetResponse(item_id=0, value=digest))
    # Stateless describe: two chunks, last has has_more=False
    transport.push(DescribeResponse(next_item_id=1, has_more=True, chunk=blob[:32]))
    transport.push(DescribeResponse(next_item_id=0, has_more=False, chunk=blob[32:]))

    await client.load()
    try:
        assert [it.name for it in client.get_all_items()] == [
            "StowHash",
            "DeviceName",
            "TestInt",
            "TestFloat",
            "TestEnum",
            "TestBytes",
            "TestBuffer",
            "SpiTxBuffer",
        ]
        assert client.get_item_description("DeviceName").id == 1
        cached_path = cache_dir / "stow-client" / f"{digest}.cbor"
        assert cached_path.exists()
    finally:
        await client._receiver.stop()


async def test_load_uses_cache(transport: MockTransport, cache_dir: Path):
    digest = sample_description_hash()
    (cache_dir / "stow-client").mkdir(parents=True)
    (cache_dir / "stow-client" / f"{digest}.cbor").write_bytes(sample_description_blob())

    client = StowClient(transport, cache_dir=cache_dir)
    transport.push(VersionResponse(version=1))
    transport.push(GetResponse(item_id=0, value=digest))

    await client.load()
    try:
        assert len(client.get_all_items()) > 0
        decoded_requests = [decode(frame) for frame in transport.outgoing]
        types = [type(r).__name__ for r in decoded_requests]
        assert "DescribeRequest" not in types
    finally:
        await client._receiver.stop()


async def test_load_version_mismatch_raises(transport: MockTransport, cache_dir: Path):
    client = StowClient(transport, cache_dir=cache_dir)
    transport.push(VersionResponse(version=2))
    with pytest.raises(StowVersionMismatch):
        await client.load()
    await client._receiver.stop()


async def test_public_api_before_load_raises(transport: MockTransport, cache_dir: Path):
    client = StowClient(transport, cache_dir=cache_dir)
    with pytest.raises(StowNotLoadedError):
        client.get_all_items()
    with pytest.raises(StowNotLoadedError):
        await client.get("DeviceName")


async def test_get_sync_returns_decoded_value(loaded_client, transport: MockTransport):
    transport.push(GetResponse(item_id=1, value="MyDevice"))
    response = await loaded_client.get_sync("DeviceName", timeout=1.0)
    assert response.value == "MyDevice"
    assert response.item_id == 1


async def test_get_sync_struct_decodes_to_dict(loaded_client, transport: MockTransport):
    transport.push(GetResponse(item_id=7, value=[1, b"\x01", "abc", b""]))
    response = await loaded_client.get_sync("SpiTxBuffer", timeout=1.0)
    assert response.value == {
        "CS": 1,
        "Bytes": b"\x01",
        "Text": "abc",
        "Buffer": b"",
    }


async def test_set_sync_returns_ok(loaded_client, transport: MockTransport):
    transport.push(Ok())
    ok = await loaded_client.set_sync("TestInt", 42, timeout=1.0)
    assert isinstance(ok, Ok)
    sent = [decode(frame) for frame in transport.outgoing]
    set_req = next(r for r in sent if isinstance(r, SetRequest))
    assert set_req.item_id == 2
    assert set_req.value == 42


async def test_set_sync_error_raises(loaded_client, transport: MockTransport):
    transport.push(Error(code=2))
    with pytest.raises(StowError, match="Error code 2"):
        await loaded_client.set_sync("TestInt", 42, timeout=1.0)


async def test_subscribe_unsubscribe_sync(loaded_client, transport: MockTransport):
    transport.push(Ok())
    await loaded_client.subscribe_sync("TestInt", timeout=1.0)
    transport.push(Ok())
    await loaded_client.unsubscribe_sync("TestInt", timeout=1.0)

    sent = [decode(frame) for frame in transport.outgoing]
    assert any(isinstance(r, SubscribeRequest) and r.item_id == 2 for r in sent)
    assert any(isinstance(r, UnsubscribeRequest) and r.item_id == 2 for r in sent)


async def test_sync_timeout_cleans_up_waiter(loaded_client, transport: MockTransport):
    with pytest.raises(asyncio.TimeoutError):
        await loaded_client.get_sync("TestInt", timeout=0.05)

    transport.push(GetResponse(item_id=2, value=5))
    response = await loaded_client.get_sync("TestInt", timeout=1.0)
    assert response.value == 5


async def test_callbacks_receive_updates(loaded_client, transport: MockTransport):
    received: list = []

    def sync_cb(msg):
        received.append(("sync", msg))

    async def async_cb(msg):
        received.append(("async", msg))

    loaded_client.add_response_callback(sync_cb)
    loaded_client.add_response_callback(async_cb)

    transport.push(Update(item_id=2, value=11))
    for _ in range(50):
        await asyncio.sleep(0.01)
        if len(received) >= 2:
            break

    kinds = sorted(k for k, _ in received)
    assert kinds == ["async", "sync"]
    assert all(isinstance(m, Update) for _, m in received)


async def test_callback_exception_does_not_block_others(
    loaded_client, transport: MockTransport
):
    other_calls: list = []

    def bad(msg):
        raise RuntimeError("boom")

    def good(msg):
        other_calls.append(msg)

    loaded_client.add_response_callback(bad)
    loaded_client.add_response_callback(good)

    transport.push(Update(item_id=2, value=1))
    for _ in range(50):
        await asyncio.sleep(0.01)
        if other_calls:
            break

    assert len(other_calls) == 1


async def test_remove_response_callback(loaded_client, transport: MockTransport):
    calls: list = []

    def cb(msg):
        calls.append(msg)

    loaded_client.add_response_callback(cb)
    loaded_client.remove_response_callback(cb)
    transport.push(Update(item_id=2, value=1))
    await asyncio.sleep(0.05)
    assert calls == []


async def test_context_manager_loads_and_closes(transport: MockTransport, cache_dir: Path):
    blob = sample_description_blob()
    digest = sample_description_hash()
    transport.push(VersionResponse(version=1))
    transport.push(GetResponse(item_id=0, value=digest))
    transport.push(DescribeResponse(next_item_id=0, has_more=False, chunk=blob))

    async with StowClient(transport, cache_dir=cache_dir) as client:
        assert client.get_item_description("DeviceName").id == 1

    assert transport.closed is True


async def test_fire_and_forget_get_does_not_wait(loaded_client, transport: MockTransport):
    await asyncio.wait_for(loaded_client.get("DeviceName"), timeout=0.5)
    sent = [decode(frame) for frame in transport.outgoing]
    assert any(
        getattr(r, "item_id", None) == 1 and type(r).__name__ == "GetRequest" for r in sent
    )


async def test_multi_get_sync_returns_decoded_values(
    loaded_client, transport: MockTransport
):
    # TestInt=id2, TestFloat=id3
    transport.push(MultiGetResponse(items=((2, 42), (3, 21.5))))
    response = await loaded_client.multi_get_sync(["TestInt", "TestFloat"], timeout=1.0)
    assert isinstance(response, MultiGetResponse)
    items_by_id = dict(response.items)
    assert items_by_id[2] == 42
    assert items_by_id[3] == pytest.approx(21.5)

    sent = [decode(frame) for frame in transport.outgoing]
    req = next(r for r in sent if isinstance(r, MultiGetRequest))
    assert set(req.item_ids) == {2, 3}


async def test_multi_get_sync_error_raises(loaded_client, transport: MockTransport):
    transport.push(Error(code=4))
    with pytest.raises(StowError, match="Error code 4"):
        await loaded_client.multi_get_sync(["TestInt", "TestFloat"], timeout=1.0)


async def test_multi_set_sync_sends_encoded_values(
    loaded_client, transport: MockTransport
):
    transport.push(Ok())
    ok = await loaded_client.multi_set_sync(
        {"TestInt": 10, "TestFloat": 22.5}, timeout=1.0
    )
    assert isinstance(ok, Ok)

    sent = [decode(frame) for frame in transport.outgoing]
    req = next(r for r in sent if isinstance(r, MultiSetRequest))
    items_by_id = dict(req.items)
    assert items_by_id[2] == 10
    assert items_by_id[3] == pytest.approx(22.5)


async def test_multi_set_sync_error_raises(loaded_client, transport: MockTransport):
    transport.push(Error(code=4))
    with pytest.raises(StowError, match="Error code 4"):
        await loaded_client.multi_set_sync({"TestInt": 10}, timeout=1.0)


async def test_fire_and_forget_multi_get(loaded_client, transport: MockTransport):
    await asyncio.wait_for(
        loaded_client.multi_get(["TestInt", "TestFloat"]), timeout=0.5
    )
    sent = [decode(frame) for frame in transport.outgoing]
    assert any(isinstance(r, MultiGetRequest) for r in sent)


async def test_fire_and_forget_multi_set(loaded_client, transport: MockTransport):
    await asyncio.wait_for(
        loaded_client.multi_set({"TestInt": 5}), timeout=0.5
    )
    sent = [decode(frame) for frame in transport.outgoing]
    assert any(isinstance(r, MultiSetRequest) for r in sent)
