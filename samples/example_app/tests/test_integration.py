from __future__ import annotations

import asyncio

import pytest
from stow_client import StowClient, StowError
from stow_client.messages import MultiGetResponse, Ok, Update

pytestmark = pytest.mark.integration

# ---------------------------------------------------------------------------
# Connect & Describe
# ---------------------------------------------------------------------------


async def test_version_handshake(uart_client: StowClient):
    """Client connects and completes the version handshake without error."""
    assert uart_client._loaded


expected_item_names = {
    "DeviceName",
    "SerialNumber",
    "BleConnectionState",
    "TestEnum",
    "TestInt",
    "TestFloat",
    "TestBytes",
    "TestBuffer",
    "TestStruct",
}


async def test_describe_loads_expected_items(uart_client: StowClient):
    """All expected items are present after load."""
    names = {it.name for it in uart_client.get_all_items()}
    assert expected_item_names.issubset(names)


async def test_describe_fresh_load(uart_transport, tmp_path):
    """A client with an empty local cache performs a full Describe exchange."""
    client = StowClient(uart_transport, cache_dir=tmp_path / "cache")
    await client.load()
    try:
        names = {it.name for it in client.get_all_items()}
        assert expected_item_names.issubset(names)
    finally:
        await client.close()


# ---------------------------------------------------------------------------
# Int
# ---------------------------------------------------------------------------


async def test_set_and_get_int(uart_client: StowClient):
    await uart_client.set_sync("TestInt", 42, timeout=2.0)
    response = await uart_client.get_sync("TestInt", timeout=2.0)
    assert response.value == 42


async def test_set_and_get_int_min_boundary(uart_client: StowClient):
    await uart_client.set_sync("TestInt", -100, timeout=2.0)
    response = await uart_client.get_sync("TestInt", timeout=2.0)
    assert response.value == -100


async def test_set_and_get_int_max_boundary(uart_client: StowClient):
    await uart_client.set_sync("TestInt", 100, timeout=2.0)
    response = await uart_client.get_sync("TestInt", timeout=2.0)
    assert response.value == 100


async def test_set_int_above_max_raises(uart_client: StowClient):
    with pytest.raises(StowError):
        await uart_client.set_sync("TestInt", 101, timeout=2.0)


async def test_set_int_below_min_raises(uart_client: StowClient):
    with pytest.raises(StowError):
        await uart_client.set_sync("TestInt", -101, timeout=2.0)


# ---------------------------------------------------------------------------
# Float
# ---------------------------------------------------------------------------


async def test_set_and_get_float(uart_client: StowClient):
    await uart_client.set_sync("TestFloat", 20.0, timeout=2.0)
    response = await uart_client.get_sync("TestFloat", timeout=2.0)
    assert abs(response.value - 20.0) < 0.01


async def test_set_and_get_float_min_boundary(uart_client: StowClient):
    await uart_client.set_sync("TestFloat", 15.0, timeout=2.0)
    response = await uart_client.get_sync("TestFloat", timeout=2.0)
    assert abs(response.value - 15.0) < 0.01


async def test_set_and_get_float_max_boundary(uart_client: StowClient):
    await uart_client.set_sync("TestFloat", 30.0, timeout=2.0)
    response = await uart_client.get_sync("TestFloat", timeout=2.0)
    assert abs(response.value - 30.0) < 0.01


async def test_set_float_above_max_raises(uart_client: StowClient):
    with pytest.raises(StowError):
        await uart_client.set_sync("TestFloat", 30.1, timeout=2.0)


async def test_set_float_below_min_raises(uart_client: StowClient):
    with pytest.raises(StowError):
        await uart_client.set_sync("TestFloat", 14.9, timeout=2.0)


# ---------------------------------------------------------------------------
# Enum
# ---------------------------------------------------------------------------


async def test_set_and_get_enum_disabled(uart_client: StowClient):
    await uart_client.set_sync("TestEnum", 0, timeout=2.0)
    response = await uart_client.get_sync("TestEnum", timeout=2.0)
    assert response.value == 0


async def test_set_and_get_enum_enabled(uart_client: StowClient):
    await uart_client.set_sync("TestEnum", 1, timeout=2.0)
    response = await uart_client.get_sync("TestEnum", timeout=2.0)
    assert response.value == 1


async def test_set_enum_invalid_value_raises(uart_client: StowClient):
    with pytest.raises(StowError):
        await uart_client.set_sync("TestEnum", 2, timeout=2.0)


# ---------------------------------------------------------------------------
# String
# ---------------------------------------------------------------------------


async def test_set_and_get_string(uart_client: StowClient):
    await uart_client.set_sync("DeviceName", "TestDevice", timeout=2.0)
    response = await uart_client.get_sync("DeviceName", timeout=2.0)
    assert response.value == "TestDevice"


async def test_set_and_get_string_min_length(uart_client: StowClient):
    await uart_client.set_sync("DeviceName", "abc", timeout=2.0)
    response = await uart_client.get_sync("DeviceName", timeout=2.0)
    assert response.value == "abc"


async def test_set_string_too_long_raises(uart_client: StowClient):
    with pytest.raises(StowError):
        await uart_client.set_sync("DeviceName", "x" * 200, timeout=2.0)


# ---------------------------------------------------------------------------
# Byte Array
# ---------------------------------------------------------------------------


async def test_set_and_get_byte_array(uart_client: StowClient):
    payload = bytes(range(12))
    await uart_client.set_sync("TestBytes", payload, timeout=2.0)
    response = await uart_client.get_sync("TestBytes", timeout=2.0)
    assert response.value == payload


async def test_get_byte_array_returns_bytes(uart_client: StowClient):
    response = await uart_client.get_sync("TestBytes", timeout=2.0)
    assert isinstance(response.value, bytes)
    assert len(response.value) == 12


async def test_set_byte_array_wrong_length_raises(uart_client: StowClient):
    with pytest.raises(StowError):
        await uart_client.set_sync("TestBytes", bytes(range(11)), timeout=2.0)


# ---------------------------------------------------------------------------
# Buffer
# ---------------------------------------------------------------------------


async def test_set_and_get_buffer(uart_client: StowClient):
    payload = b"\x01\x02\x03\x04\x05"
    await uart_client.set_sync("TestBuffer", payload, timeout=2.0)
    response = await uart_client.get_sync("TestBuffer", timeout=2.0)
    assert response.value == payload


async def test_set_and_get_buffer_empty(uart_client: StowClient):
    await uart_client.set_sync("TestBuffer", b"", timeout=2.0)
    response = await uart_client.get_sync("TestBuffer", timeout=2.0)
    assert response.value == b""


async def test_set_buffer_too_long_raises(uart_client: StowClient):
    with pytest.raises(StowError):
        await uart_client.set_sync("TestBuffer", b"x" * 513, timeout=2.0)


# ---------------------------------------------------------------------------
# Struct
# ---------------------------------------------------------------------------


async def test_get_struct_returns_dict(uart_client: StowClient):
    response = await uart_client.get_sync("TestStruct", timeout=2.0)
    assert isinstance(response.value, dict)
    assert set(response.value.keys()) == {"Text", "Number"}


async def test_set_and_get_struct(uart_client: StowClient):
    value = {
        "Text": "abc",
        "Number": 10,
    }
    await uart_client.set_sync("TestStruct", value, timeout=2.0)
    response = await uart_client.get_sync("TestStruct", timeout=2.0)
    assert response.value["Text"] == "abc"
    assert response.value["Number"] == 10


# ---------------------------------------------------------------------------
# Multi-Get / Multi-Set
# ---------------------------------------------------------------------------


async def test_multi_get(uart_client: StowClient):
    response = await uart_client.multi_get_sync(["TestInt", "TestFloat"], timeout=2.0)
    assert isinstance(response, MultiGetResponse)
    assert len(response.items) == 2


async def test_multi_set(uart_client: StowClient):
    ok = await uart_client.multi_set_sync(
        {"TestInt": 7, "TestFloat": 20.0}, timeout=2.0
    )
    assert isinstance(ok, Ok)

    response = await uart_client.multi_get_sync(["TestInt", "TestFloat"], timeout=2.0)
    items_by_name = {}
    for item_id, value in response.items:
        desc = uart_client._items_by_id.get(item_id)
        if desc:
            items_by_name[desc.name] = value

    assert items_by_name.get("TestInt") == 7
    assert abs(items_by_name.get("TestFloat", 0) - 20.0) < 0.01


# ---------------------------------------------------------------------------
# Subscribe / Unsubscribe
# ---------------------------------------------------------------------------


async def test_subscribe_receives_update(uart_client: StowClient):
    updates: list = []

    def on_update(msg):
        if isinstance(msg, Update):
            updates.append(msg)

    uart_client.add_response_callback(on_update)

    ok = await uart_client.subscribe_sync("TestInt", timeout=2.0)
    assert isinstance(ok, Ok)

    await uart_client.set_sync("TestInt", 99, timeout=2.0)

    for _ in range(50):
        await asyncio.sleep(0.05)
        if updates:
            break

    uart_client.remove_response_callback(on_update)
    await uart_client.unsubscribe_sync("TestInt", timeout=2.0)

    assert updates, "Expected at least one Update notification"
    test_int_desc = uart_client.get_item_description("TestInt")
    update = next(u for u in updates if u.item_id == test_int_desc.id)
    assert update.value == 99


# ---------------------------------------------------------------------------
# Permissions
# ---------------------------------------------------------------------------


async def test_permission_check_helpers(uart_client: StowClient):
    """can_read / can_write reflect Stow permissions."""
    device_name = uart_client.get_item_description("DeviceName")
    assert device_name.can_read("Session")
    assert device_name.can_write("Session")

    ble_state = uart_client.get_item_description("BleConnectionState")
    assert ble_state.can_read("Session")
    assert ble_state.can_write("Session")
