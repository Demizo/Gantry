from __future__ import annotations

import cbor2
import pytest

from stow_client.messages import (
    TAG_MAX,
    DescribeRequest,
    DescribeResponse,
    Error,
    GetRequest,
    GetResponse,
    HelloRequest,
    HelloResponse,
    Ok,
    SetRequest,
    StowProtocolError,
    SubscribeRequest,
    UnsubscribeRequest,
    Update,
    decode,
)


@pytest.mark.parametrize(
    "message, expected_list",
    [
        (HelloRequest(tag=1), [0, 1]),
        (
            HelloResponse(tag=1, version=1, fields={0: 512, 3: ["Guest"]}),
            [1, 1, 1, {0: 512, 3: ["Guest"]}],
        ),
        (DescribeRequest(tag=2, start_item_id=0), [2, 2, 0]),
        (
            DescribeRequest(tag=2, start_item_id=5, names=("A", "B")),
            [2, 2, 5, ["A", "B"]],
        ),
        (
            DescribeResponse(tag=3, next_item_id=3, chunk=b"\x01\x02"),
            [3, 3, 3, b"\x01\x02"],
        ),
        (DescribeResponse(tag=3, next_item_id=None, chunk=b""), [3, 3, None, b""]),
        (GetRequest(tag=4, item_ids=(42,)), [4, 4, 42]),
        (GetRequest(tag=4, item_ids=(1, 2, 3)), [4, 4, 1, 2, 3]),
        (GetResponse(tag=4, items=((42, "hello"),)), [5, 4, 42, "hello"]),
        (GetResponse(tag=4, items=((1, 42), (2, 3.14))), [5, 4, 1, 42, 2, 3.14]),
        (SetRequest(tag=5, items=((7, 99),)), [6, 5, 7, 99]),
        (SetRequest(tag=5, items=((1, 99), (2, 1.5))), [6, 5, 1, 99, 2, 1.5]),
        (SubscribeRequest(tag=6, item_ids=(3,)), [7, 6, 3]),
        (SubscribeRequest(tag=6, item_ids=(3, 4)), [7, 6, 3, 4]),
        (UnsubscribeRequest(tag=7, item_ids=(3,)), [8, 7, 3]),
        (Update(item_id=3, value=1.5), [9, None, 3, 1.5]),
        (Ok(tag=8), [10, 8]),
        (Error(tag=9, code=2), [11, 9, 2]),
        (Error(tag=None, code=1), [11, None, 1]),
        (Error(tag=9, code=7, item=3), [11, 9, 7, 3]),
        (Error(tag=9, code=7, item=3, index=1), [11, 9, 7, 3, 1]),
        (Error(tag=9, code=4, index=4), [11, 9, 4, None, 4]),
    ],
)
def test_encode_matches_wire_layout(message, expected_list):
    assert cbor2.loads(message.encode()) == expected_list


@pytest.mark.parametrize(
    "message",
    [
        HelloRequest(tag=0),
        HelloResponse(
            tag=TAG_MAX, version=2, fields={0: 300, 1: "abc", 3: ["Dev"], 99: "future"}
        ),
        DescribeRequest(tag=1, start_item_id=7),
        DescribeRequest(tag=1, start_item_id=0, names=("TestInt",)),
        DescribeResponse(tag=2, next_item_id=None, chunk=b"last"),
        DescribeResponse(tag=2, next_item_id=4, chunk=b"more"),
        GetRequest(tag=3, item_ids=(99,)),
        GetRequest(tag=3, item_ids=(0, 1, 2)),
        GetResponse(tag=3, items=((12, [1, 2, 3]),)),
        GetResponse(tag=3, items=((0, "hash"), (1, "Gantry"))),
        SetRequest(tag=4, items=((1, b"\xff"),)),
        SetRequest(tag=4, items=((2, 10), (3, 22.5))),
        SubscribeRequest(tag=5, item_ids=(5, 6)),
        UnsubscribeRequest(tag=5, item_ids=(5,)),
        Update(item_id=5, value="updated"),
        Ok(tag=6),
        Error(tag=None, code=1),
        Error(tag=7, code=8, item=4),
        Error(tag=7, code=3, item=4, index=2),
    ],
)
def test_encode_decode_roundtrip(message):
    decoded = decode(message.encode())
    assert decoded == message


def test_decode_indefinite_length_response():
    frame = bytes.fromhex("9f0a07ff")
    assert decode(frame) == Ok(tag=7)


def test_hello_response_ignores_unknown_keys():
    info = decode(cbor2.dumps([1, 1, 1, {1: "abc", 42: "new"}]))
    assert isinstance(info, HelloResponse)
    assert info.stow_hash == "abc"
    assert info.roles == []
    assert info.max_message is None


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
    bogus = cbor2.dumps([3, 1, None, "not bytes"])
    with pytest.raises(StowProtocolError):
        decode(bogus)


def test_get_response_rejects_odd_payload():
    bogus = cbor2.dumps([5, 0, 1, 42, 3])
    with pytest.raises(StowProtocolError):
        decode(bogus)


def test_set_request_rejects_odd_payload():
    bogus = cbor2.dumps([6, 0, 1, 42, 3])
    with pytest.raises(StowProtocolError):
        decode(bogus)


def test_error_code_is_int():
    frame = cbor2.dumps([11, 1, 4])
    msg = decode(frame)
    assert isinstance(msg, Error)
    assert msg.code == 4


def test_truncated_message_is_a_protocol_error():
    with pytest.raises(StowProtocolError):
        decode(cbor2.dumps([1, 1]))


def test_floats_encode_as_float32():
    assert SetRequest(tag=1, items=((3, 21.5),)).encode() == bytes.fromhex(
        "84060103fa41ac0000"
    )
    assert (
        SetRequest(tag=1, items=((3, 0.1),))
        .encode()
        .endswith(bytes.fromhex("fb3fb999999999999a"))
    )


def test_update_rejects_a_tag():
    with pytest.raises(StowProtocolError):
        decode(cbor2.dumps([9, 1, 3, 1]))
