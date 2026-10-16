from __future__ import annotations

import asyncio

import pytest

from stow_client import ErrorCode, StowClient, StowError

pytestmark = pytest.mark.integration

# ---------------------------------------------------------------------------
# Connect & Describe
# ---------------------------------------------------------------------------


async def test_hello_handshake(uart_client: StowClient):
    """Client connects and completes the Hello handshake without error."""
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
    await uart_client.set("TestInt", 42, timeout=2.0)
    value = await uart_client.get("TestInt", timeout=2.0)
    assert value == 42


async def test_set_and_get_int_min_boundary(uart_client: StowClient):
    await uart_client.set("TestInt", -100, timeout=2.0)
    value = await uart_client.get("TestInt", timeout=2.0)
    assert value == -100


async def test_set_and_get_int_max_boundary(uart_client: StowClient):
    await uart_client.set("TestInt", 100, timeout=2.0)
    value = await uart_client.get("TestInt", timeout=2.0)
    assert value == 100


async def test_set_int_above_max_raises(uart_client: StowClient):
    with pytest.raises(StowError):
        await uart_client.set("TestInt", 101, timeout=2.0)


async def test_set_int_below_min_raises(uart_client: StowClient):
    with pytest.raises(StowError):
        await uart_client.set("TestInt", -101, timeout=2.0)


# ---------------------------------------------------------------------------
# Float
# ---------------------------------------------------------------------------


async def test_set_and_get_float(uart_client: StowClient):
    await uart_client.set("TestFloat", 20.0, timeout=2.0)
    value = await uart_client.get("TestFloat", timeout=2.0)
    assert abs(value - 20.0) < 0.01


async def test_set_and_get_float_min_boundary(uart_client: StowClient):
    await uart_client.set("TestFloat", 15.0, timeout=2.0)
    value = await uart_client.get("TestFloat", timeout=2.0)
    assert abs(value - 15.0) < 0.01


async def test_set_and_get_float_max_boundary(uart_client: StowClient):
    await uart_client.set("TestFloat", 30.0, timeout=2.0)
    value = await uart_client.get("TestFloat", timeout=2.0)
    assert abs(value - 30.0) < 0.01


async def test_set_float_above_max_raises(uart_client: StowClient):
    with pytest.raises(StowError):
        await uart_client.set("TestFloat", 30.1, timeout=2.0)


async def test_set_float_below_min_raises(uart_client: StowClient):
    with pytest.raises(StowError):
        await uart_client.set("TestFloat", 14.9, timeout=2.0)


# ---------------------------------------------------------------------------
# Enum
# ---------------------------------------------------------------------------


async def test_set_and_get_enum_disabled(uart_client: StowClient):
    await uart_client.set("TestEnum", 0, timeout=2.0)
    value = await uart_client.get("TestEnum", timeout=2.0)
    assert value == 0


async def test_set_and_get_enum_enabled(uart_client: StowClient):
    await uart_client.set("TestEnum", 1, timeout=2.0)
    value = await uart_client.get("TestEnum", timeout=2.0)
    assert value == 1


async def test_set_enum_invalid_value_raises(uart_client: StowClient):
    with pytest.raises(StowError):
        await uart_client.set("TestEnum", 2, timeout=2.0)


# ---------------------------------------------------------------------------
# String
# ---------------------------------------------------------------------------


async def test_set_and_get_string(uart_client: StowClient):
    await uart_client.set("DeviceName", "TestDevice", timeout=2.0)
    value = await uart_client.get("DeviceName", timeout=2.0)
    assert value == "TestDevice"


async def test_set_and_get_string_min_length(uart_client: StowClient):
    await uart_client.set("DeviceName", "abc", timeout=2.0)
    value = await uart_client.get("DeviceName", timeout=2.0)
    assert value == "abc"


async def test_set_string_too_long_raises(uart_client: StowClient):
    with pytest.raises(StowError):
        await uart_client.set("DeviceName", "x" * 200, timeout=2.0)


# ---------------------------------------------------------------------------
# Byte Array
# ---------------------------------------------------------------------------


async def test_set_and_get_byte_array(uart_client: StowClient):
    payload = bytes(range(12))
    await uart_client.set("TestBytes", payload, timeout=2.0)
    value = await uart_client.get("TestBytes", timeout=2.0)
    assert value == payload


async def test_get_byte_array_returns_bytes(uart_client: StowClient):
    value = await uart_client.get("TestBytes", timeout=2.0)
    assert isinstance(value, bytes)
    assert len(value) == 12


async def test_set_byte_array_wrong_length_raises(uart_client: StowClient):
    with pytest.raises(StowError):
        await uart_client.set("TestBytes", bytes(range(11)), timeout=2.0)


# ---------------------------------------------------------------------------
# Buffer
# ---------------------------------------------------------------------------


async def test_set_and_get_buffer(uart_client: StowClient):
    payload = b"\x01\x02\x03\x04\x05"
    await uart_client.set("TestBuffer", payload, timeout=2.0)
    value = await uart_client.get("TestBuffer", timeout=2.0)
    assert value == payload


async def test_set_and_get_buffer_empty(uart_client: StowClient):
    await uart_client.set("TestBuffer", b"", timeout=2.0)
    value = await uart_client.get("TestBuffer", timeout=2.0)
    assert value == b""


async def test_set_buffer_too_long_raises(uart_client: StowClient):
    with pytest.raises(StowError):
        await uart_client.set("TestBuffer", b"x" * 513, timeout=2.0)


# ---------------------------------------------------------------------------
# Struct
# ---------------------------------------------------------------------------


async def test_get_struct_returns_dict(uart_client: StowClient):
    value = await uart_client.get("TestStruct", timeout=2.0)
    assert isinstance(value, dict)
    assert set(value.keys()) == {"Text", "Number"}


async def test_set_and_get_struct(uart_client: StowClient):
    value = {
        "Text": "abc",
        "Number": 10,
    }
    await uart_client.set("TestStruct", value, timeout=2.0)
    stored = await uart_client.get("TestStruct", timeout=2.0)
    assert stored == value


# ---------------------------------------------------------------------------
# Several items at once
# ---------------------------------------------------------------------------


async def test_get_several_items(uart_client: StowClient):
    values = await uart_client.get(["TestInt", "TestFloat"], timeout=2.0)
    assert set(values) == {"TestInt", "TestFloat"}


async def test_set_several_items(uart_client: StowClient):
    await uart_client.set({"TestInt": 7, "TestFloat": 20.0}, timeout=2.0)

    values = await uart_client.get(["TestInt", "TestFloat"], timeout=2.0)
    assert values["TestInt"] == 7
    assert abs(values["TestFloat"] - 20.0) < 0.01


# ---------------------------------------------------------------------------
# Subscribe / Unsubscribe
# ---------------------------------------------------------------------------


async def test_subscribe_receives_update(uart_client: StowClient):
    updates: list = []
    await uart_client.subscribe("TestInt", timeout=2.0, listener=updates.append)

    await uart_client.set("TestInt", 99, timeout=2.0)

    for _ in range(50):
        await asyncio.sleep(0.05)
        if updates:
            break

    await uart_client.unsubscribe("TestInt", timeout=2.0)

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


async def test_hello_reports_limits_and_roles(uart_client: StowClient):
    info = uart_client.hello_response
    assert info is not None
    assert info.max_message > 0
    assert "Session" in info.roles


async def test_rejected_value_names_the_item(uart_client: StowClient):
    with pytest.raises(StowError) as excinfo:
        await uart_client.set("TestInt", 101, timeout=2.0)
    assert excinfo.value.code == ErrorCode.INVALID_VALUE
    assert excinfo.value.item == uart_client.get_item_description("TestInt").id


async def test_set_failure_names_the_entry(uart_client: StowClient):
    with pytest.raises(StowError) as excinfo:
        await uart_client.set({"TestInt": 5, "TestFloat": 99.0}, timeout=2.0)
    assert excinfo.value.code == ErrorCode.INVALID_VALUE
    assert excinfo.value.index == 1

    value = await uart_client.get("TestInt", timeout=2.0)
    assert value == 5


async def test_describe_selected_items(uart_client: StowClient):
    items = await uart_client.describe_items(["TestInt", "NoSuchItem"], timeout=2.0)
    assert [item.name for item in items] == ["TestInt"]


async def test_subscribe_to_several_items(uart_client: StowClient):
    updates: list = []
    await uart_client.subscribe(
        ["TestInt", "TestEnum"], timeout=2.0, listener=updates.append
    )
    await uart_client.set("TestInt", 12, timeout=2.0)
    await uart_client.set("TestEnum", 1, timeout=2.0)

    for _ in range(50):
        await asyncio.sleep(0.05)
        if len(updates) >= 2:
            break

    await uart_client.unsubscribe(["TestInt", "TestEnum"], timeout=2.0)

    ids = {update.item_id for update in updates}
    assert uart_client.get_item_description("TestInt").id in ids
    assert uart_client.get_item_description("TestEnum").id in ids
