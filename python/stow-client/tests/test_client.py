from __future__ import annotations

import asyncio
import dataclasses
from pathlib import Path

import cbor2
import pytest

from stow_client import (
    StowClient,
    StowError,
    StowNotLoadedError,
    StowRequestTooLargeError,
    StowVersionMismatch,
    UnexpectedResponseError,
)
from stow_client._receiver import Receiver
from stow_client.messages import (
    TAG_MAX,
    DescribeRequest,
    DescribeResponse,
    Error,
    ErrorCode,
    GetRequest,
    GetResponse,
    HelloKey,
    HelloRequest,
    HelloResponse,
    Ok,
    SetRequest,
    SubscribeRequest,
    UnsubscribeRequest,
    Update,
    decode,
)

from .conftest import (
    DESCRIBE_PAGES,
    HELLO,
    STOW_HASH,
    MockTransport,
    description_blob,
    description_items,
    hello_response,
    item_id,
)

TEST_INT = item_id("TestInt")
TEST_STRING = item_id("TestString")
TEST_FLOAT = item_id("TestFloat")
TEST_RECORD = item_id("TestRecord")


@pytest.fixture
async def client(transport: MockTransport, cache_dir: Path):
    c = StowClient(transport, cache_dir=cache_dir)
    yield c
    await c._receiver.stop()


async def test_load_happy_path(transport: MockTransport, cache_dir: Path):
    client = StowClient(transport, cache_dir=cache_dir)
    for message in (HELLO, *DESCRIBE_PAGES):
        transport.reply(message)

    await client.load()
    try:
        assert [it.name for it in client.get_all_items()] == [
            "StowHash",
            "TestInt",
            "TestString",
            "TestSessionInt",
            "TestDevInt",
            "TestBuffer",
            "TestTofu",
            "TestLongString",
            "TestFloat",
            "TestEnum",
            "TestBytes",
            "TestRecord",
            "TestNestedRecord",
        ]
        assert client.get_item_description("TestString").id == TEST_STRING
        cached_path = cache_dir / "stow-client" / f"{STOW_HASH}.cbor"
        assert cached_path.exists()
    finally:
        await client._receiver.stop()


async def test_load_uses_cache(transport: MockTransport, cache_dir: Path):
    (cache_dir / "stow-client").mkdir(parents=True)
    (cache_dir / "stow-client" / f"{STOW_HASH}.cbor").write_bytes(description_blob())

    client = StowClient(transport, cache_dir=cache_dir)
    transport.reply(hello_response())

    await client.load()
    try:
        assert len(client.get_all_items()) > 0
        decoded_requests = [decode(frame) for frame in transport.outgoing]
        types = [type(request).__name__ for request in decoded_requests]
        assert "DescribeRequest" not in types
    finally:
        await client._receiver.stop()


async def test_load_refetches_a_cached_list_format_description(
    transport: MockTransport, cache_dir: Path
):
    (cache_dir / "stow-client").mkdir(parents=True)
    stale = cbor2.dumps(description_items())
    (cache_dir / "stow-client" / f"{STOW_HASH}.cbor").write_bytes(stale)

    client = StowClient(transport, cache_dir=cache_dir)
    transport.reply(hello_response())
    transport.reply(DescribeResponse(next_item_id=None, chunk=description_blob()))

    await client.load()
    try:
        assert len(client.get_all_items()) == len(description_items())
        cached = (cache_dir / "stow-client" / f"{STOW_HASH}.cbor").read_bytes()
        assert cached == description_blob()
    finally:
        await client._receiver.stop()


async def test_describe_items_before_load_raises(client: StowClient):
    with pytest.raises(StowNotLoadedError):
        await client.describe_items(["TestString"], timeout=1.0)


async def test_load_version_mismatch_raises(transport: MockTransport, cache_dir: Path):
    client = StowClient(transport, cache_dir=cache_dir)
    transport.reply(dataclasses.replace(hello_response(), version=2))
    with pytest.raises(StowVersionMismatch):
        await client.load()
    await client._receiver.stop()


async def test_load_stops_when_describe_makes_no_progress(
    transport: MockTransport, cache_dir: Path
):
    client = StowClient(transport, cache_dir=cache_dir)
    transport.reply(hello_response())
    transport.reply(DescribeResponse(next_item_id=0, chunk=b""))

    with pytest.raises(UnexpectedResponseError, match="no progress"):
        await asyncio.wait_for(client.load(), timeout=1.0)
    await client._receiver.stop()


async def test_public_api_before_load_raises(transport: MockTransport, cache_dir: Path):
    client = StowClient(transport, cache_dir=cache_dir)
    with pytest.raises(StowNotLoadedError):
        client.get_all_items()
    with pytest.raises(StowNotLoadedError):
        await client.get("TestString")


async def test_get_returns_decoded_value(loaded_client, transport: MockTransport):
    transport.reply(GetResponse(items=((TEST_STRING, "MyDevice"),)))
    assert await loaded_client.get("TestString", timeout=1.0) == "MyDevice"


async def test_get_struct_decodes_to_dict(loaded_client, transport: MockTransport):
    transport.reply(GetResponse(items=((TEST_RECORD, [1, b"\x01"]),)))
    value = await loaded_client.get("TestRecord", timeout=1.0)
    assert value == {"Count": 1, "Data": b"\x01"}


async def test_set_sends_the_item(loaded_client, transport: MockTransport):
    transport.reply(Ok())
    await loaded_client.set("TestInt", 42, timeout=1.0)
    sent = [decode(frame) for frame in transport.outgoing]
    set_req = next(request for request in sent if isinstance(request, SetRequest))
    assert set_req.items == ((TEST_INT, 42),)


async def test_set_error_raises(loaded_client, transport: MockTransport):
    transport.reply(Error(code=2))
    with pytest.raises(StowError, match="Error code 2"):
        await loaded_client.set("TestInt", 42, timeout=1.0)


async def test_subscribe_unsubscribe(loaded_client, transport: MockTransport):
    transport.reply(Ok())
    await loaded_client.subscribe("TestInt", timeout=1.0)
    transport.reply(Ok())
    await loaded_client.unsubscribe("TestInt", timeout=1.0)

    sent = [decode(frame) for frame in transport.outgoing]
    assert any(
        isinstance(request, SubscribeRequest) and request.item_ids == (TEST_INT,)
        for request in sent
    )
    assert any(
        isinstance(request, UnsubscribeRequest) and request.item_ids == (TEST_INT,)
        for request in sent
    )


async def test_timeout_cleans_up_waiter(loaded_client, transport: MockTransport):
    with pytest.raises(asyncio.TimeoutError):
        await loaded_client.get("TestInt", timeout=0.05)

    transport.reply(GetResponse(items=((TEST_INT, 5),)))
    assert await loaded_client.get("TestInt", timeout=1.0) == 5


async def test_update_values_are_decoded_with_the_description(
    loaded_client, transport: MockTransport
):
    received: list = []
    transport.reply(Ok())
    await loaded_client.subscribe("TestRecord", timeout=1.0, listener=received.append)

    transport.push(Update(item_id=TEST_RECORD, value=[1, b"\x01"]))
    for _ in range(50):
        await asyncio.sleep(0.01)
        if received:
            break

    assert received[0].value == {"Count": 1, "Data": b"\x01"}


async def test_listener_exception_does_not_block_others(
    loaded_client, transport: MockTransport
):
    other_calls: list = []

    def bad(msg):
        raise RuntimeError("boom")

    transport.reply(Ok())
    transport.reply(Ok())
    await loaded_client.subscribe("TestInt", timeout=1.0, listener=bad)
    await loaded_client.subscribe("TestInt", timeout=1.0, listener=other_calls.append)

    transport.push(Update(item_id=TEST_INT, value=1))
    for _ in range(50):
        await asyncio.sleep(0.01)
        if other_calls:
            break

    assert len(other_calls) == 1


async def test_context_manager_loads_and_closes(
    transport: MockTransport, cache_dir: Path
):
    transport.reply(hello_response())
    transport.reply(DescribeResponse(next_item_id=None, chunk=description_blob()))

    async with StowClient(transport, cache_dir=cache_dir) as client:
        assert client.get_item_description("TestString").id == TEST_STRING

    assert transport.closed is True


async def test_get_several_items_returns_a_dict(
    loaded_client, transport: MockTransport
):
    transport.reply(GetResponse(items=((TEST_INT, 42), (TEST_FLOAT, 21.5))))
    values = await loaded_client.get(["TestInt", "TestFloat"], timeout=1.0)
    assert values == {"TestInt": 42, "TestFloat": pytest.approx(21.5)}

    sent = [decode(frame) for frame in transport.outgoing]
    req = next(request for request in sent if isinstance(request, GetRequest))
    assert req.item_ids == (TEST_INT, TEST_FLOAT)


async def test_get_rejects_a_response_for_other_items(
    loaded_client, transport: MockTransport
):
    transport.reply(GetResponse(items=((TEST_FLOAT, 21.5),)))
    with pytest.raises(UnexpectedResponseError):
        await loaded_client.get("TestInt", timeout=1.0)


async def test_get_error_raises(loaded_client, transport: MockTransport):
    transport.reply(Error(code=4))
    with pytest.raises(StowError, match="Error code 4"):
        await loaded_client.get(["TestInt", "TestFloat"], timeout=1.0)


async def test_set_several_items_sends_encoded_values(
    loaded_client, transport: MockTransport
):
    transport.reply(Ok())
    await loaded_client.set({"TestInt": 10, "TestFloat": 22.5}, timeout=1.0)

    sent = [decode(frame) for frame in transport.outgoing]
    req = next(request for request in sent if isinstance(request, SetRequest))
    assert req.items == ((TEST_INT, 10), (TEST_FLOAT, 22.5))


async def test_load_sends_hello_once(transport: MockTransport, cache_dir: Path):
    client = StowClient(transport, cache_dir=cache_dir)
    transport.reply(hello_response(roles=["Dev"]))
    transport.reply(DescribeResponse(next_item_id=None, chunk=description_blob()))

    await client.load()
    try:
        sent = [decode(frame) for frame in transport.outgoing]
        assert isinstance(sent[0], HelloRequest)
        assert sum(isinstance(request, HelloRequest) for request in sent) == 1
        assert client.hello_response.roles == ["Dev"]
        assert client.hello_response.max_message == HELLO.max_message
    finally:
        await client._receiver.stop()


async def test_requests_carry_distinct_tags(loaded_client, transport: MockTransport):
    transport.reply(Ok())
    transport.reply(Ok())
    await loaded_client.set("TestInt", 1, timeout=1.0)
    await loaded_client.set("TestInt", 2, timeout=1.0)

    tags = [decode(frame).tag for frame in transport.outgoing]
    assert len(set(tags)) == len(tags)


async def test_concurrent_requests_match_responses_by_tag(
    loaded_client, transport: MockTransport
):
    first = asyncio.create_task(loaded_client.get("TestInt", timeout=1.0))
    second = asyncio.create_task(loaded_client.get("TestString", timeout=1.0))
    for _ in range(50):
        await asyncio.sleep(0.01)
        gets = [m for m in map(decode, transport.outgoing) if isinstance(m, GetRequest)]
        if len(gets) == 2:
            break
    tag_by_item = {m.item_ids[0]: m.tag for m in gets}

    transport.push(
        GetResponse(tag=tag_by_item[TEST_STRING], items=((TEST_STRING, "Second"),))
    )
    transport.push(GetResponse(tag=tag_by_item[TEST_INT], items=((TEST_INT, 7),)))

    assert await first == 7
    assert await second == "Second"


async def test_error_reports_item_and_index(loaded_client, transport: MockTransport):
    transport.reply(Error(code=ErrorCode.INVALID_VALUE, item=TEST_FLOAT, index=1))
    with pytest.raises(StowError) as excinfo:
        await loaded_client.set({"TestInt": 1, "TestFloat": 99.0}, timeout=1.0)
    assert excinfo.value.code == ErrorCode.INVALID_VALUE
    assert excinfo.value.item == TEST_FLOAT
    assert excinfo.value.index == 1


async def test_untagged_error_does_not_resolve_a_request(
    loaded_client, transport: MockTransport
):
    transport.push(Error(tag=None, code=ErrorCode.MALFORMED_MSG))

    with pytest.raises(asyncio.TimeoutError):
        await loaded_client.get("TestInt", timeout=0.1)


async def test_subscribe_to_several_items(loaded_client, transport: MockTransport):
    transport.reply(Ok())
    await loaded_client.subscribe(["TestInt", "TestFloat"], timeout=1.0)

    sent = [decode(frame) for frame in transport.outgoing]
    assert any(
        isinstance(request, SubscribeRequest)
        and request.item_ids == (TEST_INT, TEST_FLOAT)
        for request in sent
    )


async def test_subscription_listener_receives_its_items_events(
    loaded_client, transport: MockTransport
):
    events: list = []
    transport.reply(Ok())
    await loaded_client.subscribe("TestInt", timeout=1.0, listener=events.append)

    transport.push(Update(item_id=TEST_FLOAT, value=1.0))
    transport.push(Update(item_id=TEST_INT, value=5))
    transport.push(Update(item_id=TEST_INT, value=6))
    for _ in range(50):
        await asyncio.sleep(0.01)
        if len(events) >= 2:
            break
    assert events == [
        Update(item_id=TEST_INT, value=5),
        Update(item_id=TEST_INT, value=6),
    ]

    transport.reply(Ok())
    await loaded_client.unsubscribe("TestInt", timeout=1.0)
    transport.push(Update(item_id=TEST_INT, value=7))
    await asyncio.sleep(0.05)
    assert len(events) == 2


async def test_partial_subscribe_listens_to_the_active_items(
    loaded_client, transport: MockTransport
):
    events: list = []
    transport.reply(Error(code=ErrorCode.PERMISSION_DENIED, item=TEST_FLOAT, index=1))
    with pytest.raises(StowError):
        await loaded_client.subscribe(
            ["TestInt", "TestFloat"], timeout=1.0, listener=events.append
        )

    transport.push(Update(item_id=TEST_INT, value=1))
    transport.push(Update(item_id=TEST_FLOAT, value=1.0))
    for _ in range(50):
        await asyncio.sleep(0.01)
        if events:
            break
    await asyncio.sleep(0.05)
    assert events == [Update(item_id=TEST_INT, value=1)]


async def test_async_listener_runs(loaded_client, transport: MockTransport):
    events: list = []

    async def listener(msg):
        events.append(msg)

    transport.reply(Ok())
    await loaded_client.subscribe("TestInt", timeout=1.0, listener=listener)
    transport.push(Update(item_id=TEST_INT, value=4))
    for _ in range(50):
        await asyncio.sleep(0.01)
        if events:
            break
    assert events == [Update(item_id=TEST_INT, value=4)]


async def test_listener_sees_update_right_after_subscribe_ok(
    loaded_client, transport: MockTransport
):
    events: list = []
    original_send = transport.send

    async def send_and_update(data: bytes) -> None:
        await original_send(data)
        transport.push(Update(item_id=TEST_INT, value=3))

    transport.reply(Ok())
    transport.send = send_and_update
    await loaded_client.subscribe("TestInt", timeout=1.0, listener=events.append)
    transport.send = original_send

    for _ in range(50):
        await asyncio.sleep(0.01)
        if events:
            break
    assert events == [Update(item_id=TEST_INT, value=3)]


async def test_failed_subscribe_removes_listeners(
    loaded_client, transport: MockTransport
):
    events: list = []
    with pytest.raises(asyncio.TimeoutError):
        await loaded_client.subscribe("TestInt", timeout=0.05, listener=events.append)

    transport.push(Update(item_id=TEST_INT, value=1))
    await asyncio.sleep(0.05)
    assert events == []


async def test_partial_subscribe_keeps_a_repeated_item_before_the_failure(
    loaded_client, transport: MockTransport
):
    events: list = []
    transport.reply(Error(code=ErrorCode.INVALID_ITEM, item=TEST_FLOAT, index=1))
    with pytest.raises(StowError):
        await loaded_client.subscribe(
            ["TestInt", "TestFloat", "TestInt"], timeout=1.0, listener=events.append
        )

    transport.push(Update(item_id=TEST_INT, value=4))
    for _ in range(50):
        await asyncio.sleep(0.01)
        if events:
            break
    assert events == [Update(item_id=TEST_INT, value=4)]


async def test_resubscribing_does_not_duplicate_the_listener(
    loaded_client, transport: MockTransport
):
    events: list = []
    transport.reply(Ok())
    transport.reply(Ok())
    await loaded_client.subscribe("TestInt", timeout=1.0, listener=events.append)
    await loaded_client.subscribe("TestInt", timeout=1.0, listener=events.append)

    transport.push(Update(item_id=TEST_INT, value=1))
    for _ in range(50):
        await asyncio.sleep(0.01)
        if events:
            break
    await asyncio.sleep(0.05)
    assert events == [Update(item_id=TEST_INT, value=1)]


async def test_failed_resubscribe_keeps_the_earlier_listener(
    loaded_client, transport: MockTransport
):
    events: list = []
    transport.reply(Ok())
    await loaded_client.subscribe("TestInt", timeout=1.0, listener=events.append)

    transport.reply(Error(code=ErrorCode.INVALID_ITEM))
    with pytest.raises(StowError):
        await loaded_client.subscribe("TestInt", timeout=1.0, listener=events.append)
    with pytest.raises(asyncio.TimeoutError):
        await loaded_client.subscribe("TestInt", timeout=0.05, listener=events.append)

    transport.push(Update(item_id=TEST_INT, value=8))
    for _ in range(50):
        await asyncio.sleep(0.01)
        if events:
            break
    assert events == [Update(item_id=TEST_INT, value=8)]


async def test_tags_rotate_through_the_tag_range(
    loaded_client, transport: MockTransport
):
    transport.outgoing.clear()
    for _ in range(TAG_MAX + 2):
        transport.reply(GetResponse(items=((TEST_INT, 0),)))
        await loaded_client.get("TestInt", timeout=1.0)
    tags = [decode(frame).tag for frame in transport.outgoing]
    assert all(
        (b - a) % (TAG_MAX + 1) == 1 for a, b in zip(tags, tags[1:], strict=False)
    )
    assert all(0 <= tag <= TAG_MAX for tag in tags)


async def _time_out_a_get(client: StowClient, transport: MockTransport) -> int:
    with pytest.raises(asyncio.TimeoutError):
        await client.get("TestInt", timeout=0.01)
    return decode(transport.outgoing[-1]).tag


async def test_timed_out_tag_is_not_reused_until_its_response_arrives(
    loaded_client, transport: MockTransport
):
    stale = await _time_out_a_get(loaded_client, transport)
    for _ in range(TAG_MAX):
        transport.reply(GetResponse(items=((TEST_INT, 0),)))
        await loaded_client.get("TestInt", timeout=1.0)
        assert decode(transport.outgoing[-1]).tag != stale

    transport.push(GetResponse(tag=stale, items=((TEST_INT, 9),)))
    for _ in range(50):
        await asyncio.sleep(0.01)
        if not loaded_client._receiver.tag_in_use(stale):
            break
    assert not loaded_client._receiver.tag_in_use(stale)


async def test_request_waits_for_a_free_tag(transport: MockTransport, cache_dir: Path):
    client = StowClient(transport, cache_dir=cache_dir)
    await _load(client, transport)
    try:
        for _ in range(TAG_MAX):
            await _time_out_a_get(client, transport)
        first = asyncio.create_task(client.get("TestInt", timeout=1.0))
        second = asyncio.create_task(client.get("TestInt", timeout=1.0))
        await asyncio.sleep(0.05)
        gets = [m for m in map(decode, transport.outgoing) if isinstance(m, GetRequest)]
        assert len(gets) == TAG_MAX + 1

        transport.reply(GetResponse(items=((TEST_INT, 2),)))
        transport.push(GetResponse(tag=gets[-1].tag, items=((TEST_INT, 1),)))
        assert await first == 1
        assert await second == 2
    finally:
        await client._receiver.stop()


async def test_waiting_for_a_tag_is_bounded_by_the_call_timeout(
    transport: MockTransport, cache_dir: Path
):
    client = StowClient(transport, cache_dir=cache_dir)
    await _load(client, transport)
    try:
        for _ in range(TAG_MAX):
            await _time_out_a_get(client, transport)
        in_flight = asyncio.create_task(client.get("TestInt", timeout=1.0))
        await asyncio.sleep(0.01)
        loop = asyncio.get_running_loop()
        started = loop.time()
        with pytest.raises(asyncio.TimeoutError):
            await client.get("TestInt", timeout=0.05)
        assert loop.time() - started < 0.5
        in_flight.cancel()
    finally:
        await client._receiver.stop()


async def test_quarantined_tag_is_released_after_the_grace_period(
    transport: MockTransport, cache_dir: Path
):
    client = StowClient(transport, cache_dir=cache_dir, quarantine_grace=0.1)
    await _load(client, transport)
    try:
        stale = await _time_out_a_get(client, transport)
        assert client._receiver.tag_in_use(stale)
        await asyncio.sleep(0.2)
        assert not client._receiver.tag_in_use(stale)

        transport.push(GetResponse(tag=stale, items=((TEST_INT, 9),)))
        await asyncio.sleep(0.02)

        start = len(transport.outgoing)
        for _ in range(TAG_MAX + 1):
            transport.reply(GetResponse(items=((TEST_INT, 0),)))
            assert await client.get("TestInt", timeout=1.0) == 0
        tags = [decode(frame).tag for frame in transport.outgoing[start:]]
        assert stale in tags
    finally:
        await client._receiver.stop()


async def test_requests_dropped_over_time_never_kill_the_link(
    transport: MockTransport, cache_dir: Path
):
    client = StowClient(transport, cache_dir=cache_dir, quarantine_grace=0.05)
    await _load(client, transport)
    try:
        for _ in range(30):
            await _time_out_a_get(client, transport)
            await asyncio.sleep(0.02)
        transport.reply(GetResponse(items=((TEST_INT, 3),)))
        assert await client.get("TestInt", timeout=1.0) == 3
    finally:
        await client._receiver.stop()


async def test_failed_send_frees_its_tag(loaded_client, transport: MockTransport):
    original_send = transport.send

    async def failing_send(data: bytes) -> None:
        raise OSError("link down")

    transport.send = failing_send
    with pytest.raises(OSError):
        await loaded_client.set("TestInt", 1, timeout=1.0)
    transport.send = original_send
    assert not any(loaded_client._receiver.tag_in_use(t) for t in range(TAG_MAX + 1))

    transport.reply(Ok())
    await loaded_client.set("TestInt", 2, timeout=1.0)


async def test_answered_tag_is_not_reused_before_its_request_finishes(
    transport: MockTransport,
):
    receiver = Receiver(transport, lambda update: None, quarantine_grace=1.0)
    first = receiver.expect(0)
    receiver._dispatch(Ok(tag=0))
    assert first.result() == Ok(tag=0)
    assert receiver.tag_in_use(0)

    receiver.finish(0, True)
    second = receiver.expect(0)
    receiver._dispatch(Ok(tag=0))
    assert second.result() == Ok(tag=0)
    receiver.finish(0, True)
    assert not receiver.tag_in_use(0)


async def test_tag_timed_out_while_sending_is_quarantined(
    loaded_client, transport: MockTransport
):
    async def stalled_send(data: bytes) -> None:
        await asyncio.sleep(1.0)

    transport.send = stalled_send
    with pytest.raises(asyncio.TimeoutError):
        await loaded_client.set("TestInt", 1, timeout=0.05)
    assert any(loaded_client._receiver.tag_in_use(t) for t in range(TAG_MAX + 1))


async def test_request_larger_than_max_message_is_refused_locally(
    loaded_client, transport: MockTransport
):
    fields = dict(loaded_client.hello_response.fields)
    fields[HelloKey.MAX_MESSAGE] = 4
    loaded_client._hello = HelloResponse(version=1, fields=fields)
    sent = len(transport.outgoing)

    with pytest.raises(StowRequestTooLargeError, match="max_message is 4"):
        await loaded_client.set("TestInt", 1, timeout=1.0)
    assert len(transport.outgoing) == sent
    assert not any(loaded_client._receiver.tag_in_use(t) for t in range(TAG_MAX + 1))


async def test_hello(loaded_client, transport: MockTransport):
    transport.reply(hello_response(roles=["Guest", "Dev"]))
    info = await loaded_client.hello(timeout=1.0)
    assert info.stow_hash == STOW_HASH
    assert info.roles == ["Guest", "Dev"]
    assert info.role_names == ["Guest", "Session", "Dev"]


async def test_describe_items_sends_names(loaded_client, transport: MockTransport):
    one_item = cbor2.dumps(description_items()[1])
    transport.reply(DescribeResponse(next_item_id=None, chunk=one_item))

    items = await loaded_client.describe_items(["TestInt", "Missing"], timeout=1.0)
    assert [i.name for i in items] == ["TestInt"]

    sent = [decode(frame) for frame in transport.outgoing]
    request = next(
        request
        for request in sent
        if isinstance(request, DescribeRequest) and request.names
    )
    assert request.names == ("TestInt", "Missing")


async def _load(client: StowClient, transport: MockTransport) -> None:
    transport.reply(hello_response())
    transport.reply(DescribeResponse(next_item_id=None, chunk=description_blob()))
    await client.load()
