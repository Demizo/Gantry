from __future__ import annotations

import cbor2
import pytest

from stow_client.messages import (
    DescribeRequest,
    DescribeResponse,
    Error,
    GetRequest,
    GetResponse,
    MultiGetRequest,
    MultiGetResponse,
    MultiSetRequest,
    Ok,
    SetRequest,
    StowProtocolError,
    SubscribeRequest,
    UnsubscribeRequest,
    Update,
    VersionRequest,
    VersionResponse,
    decode,
)


@pytest.mark.parametrize(
    "message, expected_list",
    [
        (VersionRequest(), [0]),
        (VersionResponse(version=1), [1, 1]),
        (DescribeRequest(start_item_id=0), [2, 0]),
        (DescribeRequest(start_item_id=5), [2, 5]),
        (DescribeResponse(next_item_id=3, has_more=True, chunk=b"\x01\x02"), [3, 3, True, b"\x01\x02"]),
        (DescribeResponse(next_item_id=0, has_more=False, chunk=b"\x03"), [3, 0, False, b"\x03"]),
        (GetRequest(item_id=42), [4, 42]),
        (GetResponse(item_id=42, value="hello"), [5, 42, "hello"]),
        (SetRequest(item_id=7, value=99), [6, 7, 99]),
        (SubscribeRequest(item_id=3), [10, 3]),
        (UnsubscribeRequest(item_id=3), [11, 3]),
        (Update(item_id=3, value=1.5), [12, 3, 1.5]),
        (Ok(), [13]),
        (Error(code=2), [14, 2]),
        (MultiGetRequest(item_ids=(1, 2, 3)), [7, 1, 2, 3]),
        (MultiGetResponse(items=((1, 42), (2, 3.14))), [8, 1, 42, 2, 3.14]),
        (MultiSetRequest(items=((1, 99), (2, 1.5))), [9, 1, 99, 2, 1.5]),
    ],
)
def test_encode_matches_wire_layout(message, expected_list):
    assert cbor2.loads(message.encode()) == expected_list


@pytest.mark.parametrize(
    "message",
    [
        VersionRequest(),
        VersionResponse(version=2),
        DescribeRequest(start_item_id=0),
        DescribeRequest(start_item_id=7),
        DescribeResponse(next_item_id=3, has_more=True, chunk=b"chunk-data"),
        DescribeResponse(next_item_id=0, has_more=False, chunk=b"last"),
        GetRequest(item_id=99),
        GetResponse(item_id=12, value=[1, 2, 3]),
        SetRequest(item_id=1, value=b"\xff"),
        SubscribeRequest(item_id=5),
        UnsubscribeRequest(item_id=5),
        Update(item_id=5, value="updated"),
        Ok(),
        Error(code=4),
        MultiGetRequest(item_ids=(0, 1, 2)),
        MultiGetResponse(items=((0, "hash"), (1, "Gantry"))),
        MultiSetRequest(items=((2, 10), (3, 22.5))),
    ],
)
def test_encode_decode_roundtrip(message):
    decoded = decode(message.encode())
    assert decoded == message


def test_decode_rejects_unknown_message_id():
    bogus = cbor2.dumps([99, 1, 2])
    with pytest.raises(StowProtocolError):
        decode(bogus)


def test_decode_rejects_non_list():
    bogus = cbor2.dumps({"not": "a list"})
    with pytest.raises(StowProtocolError):
        decode(bogus)


def test_decode_rejects_empty_list():
    bogus = cbor2.dumps([])
    with pytest.raises(StowProtocolError):
        decode(bogus)


def test_describe_response_requires_byte_string():
    bogus = cbor2.dumps([3, 0, False, "not bytes"])
    with pytest.raises(StowProtocolError):
        decode(bogus)


def test_multi_get_response_rejects_odd_payload():
    bogus = cbor2.dumps([8, 1, 42, 3])  # 3 elements after msg id = odd
    with pytest.raises(StowProtocolError):
        decode(bogus)


def test_multi_set_request_rejects_odd_payload():
    bogus = cbor2.dumps([9, 1, 42, 3])
    with pytest.raises(StowProtocolError):
        decode(bogus)


def test_error_code_is_int():
    frame = cbor2.dumps([14, 4])
    msg = decode(frame)
    assert isinstance(msg, Error)
    assert msg.code == 4
