from __future__ import annotations

from struct import pack, unpack

import pytest

from stow_client.descriptions import (
    EnumConstraints,
    FloatConstraints,
    IntConstraints,
    ItemType,
    LengthConstraints,
    Storage,
    StructConstraints,
    decode_value,
    encode_value,
    parse_description,
)

from .conftest import ROLE_NAMES, description_blob


def test_parse_description_returns_all_items():
    items = parse_description(description_blob(), ROLE_NAMES)
    assert [item.name for item in items] == [
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

    by_name = {item.name: item for item in items}
    assert by_name["TestTofu"].storage is Storage.TOFU
    assert by_name["TestString"].read_perm == ROLE_NAMES
    assert by_name["TestString"].write_perm == ["Session"]
    assert by_name["TestString"].type is ItemType.STRING
    assert isinstance(by_name["TestString"].constraints, LengthConstraints)

    assert by_name["StowHash"].read_perm == ROLE_NAMES
    assert by_name["StowHash"].write_perm == []

    assert isinstance(by_name["TestInt"].constraints, IntConstraints)
    assert by_name["TestInt"].constraints.max == 100

    assert isinstance(by_name["TestFloat"].constraints, FloatConstraints)

    enum_constraints = by_name["TestEnum"].constraints
    assert isinstance(enum_constraints, EnumConstraints)
    assert {value.name for value in enum_constraints.values} == {"OFF", "ON"}

    struct = by_name["TestNestedRecord"]
    assert struct.type is ItemType.STRUCT
    assert isinstance(struct.constraints, StructConstraints)
    assert [field.name for field in struct.constraints.fields] == ["Pair", "Flag"]
    nested_struct = struct.constraints.fields[0].constraints
    assert isinstance(nested_struct, StructConstraints)


def test_can_write_specific_role():
    items = parse_description(description_blob(), ROLE_NAMES)
    by_name = {item.name: item for item in items}

    assert by_name["TestString"].can_write("Session")
    assert not by_name["TestString"].can_write("Guest")
    assert not by_name["TestString"].can_write("Dev")


def test_value_codec_roundtrip_primitives():
    int_c = IntConstraints(min=0, max=10)
    float_c = FloatConstraints(min=0.0, max=1.0)
    len_c = LengthConstraints(min_len=0, max_len=16)

    cases = [
        (ItemType.INT, int_c, 5),
        (ItemType.FLOAT, float_c, 0.25),
        (ItemType.STRING, len_c, "hello"),
        (ItemType.BYTE_ARRAY, len_c, b"\x01\x02\x03"),
        (ItemType.BUFFER, len_c, b"\xff"),
        (ItemType.ENUM, EnumConstraints(values=[]), 1),
    ]
    for itype, constraints, value in cases:
        wire = encode_value(itype, constraints, value)
        assert decode_value(itype, constraints, wire) == value


def test_value_codec_struct_roundtrip():
    items = parse_description(description_blob(), ROLE_NAMES)
    struct = next(item for item in items if item.name == "TestRecord")

    value = {"Count": 3, "Data": b"\x0a\x0b"}
    wire = encode_value(struct.type, struct.constraints, value)
    assert isinstance(wire, list)
    assert wire == [3, b"\x0a\x0b"]
    decoded = decode_value(struct.type, struct.constraints, wire)
    assert decoded == value


def test_encode_byte_array_accepts_list_of_ints():
    c = LengthConstraints(min_len=0, max_len=4)
    wire = encode_value(ItemType.BYTE_ARRAY, c, [1, 2, 3])
    assert wire == b"\x01\x02\x03"


def test_decode_struct_field_count_mismatch_raises():
    items = parse_description(description_blob(), ROLE_NAMES)
    struct = next(item for item in items if item.name == "TestRecord")
    with pytest.raises(ValueError):
        decode_value(struct.type, struct.constraints, [0])


def test_float_values_round_trip_exactly():
    c = FloatConstraints(min=0.0, max=100.0)
    device_value = unpack("<f", pack("<f", 21.123456))[0]
    assert decode_value(ItemType.FLOAT, c, device_value) == device_value
    assert encode_value(ItemType.FLOAT, c, device_value) == device_value


def test_float_outside_float32_range_is_a_value_error():
    with pytest.raises(ValueError):
        encode_value(ItemType.FLOAT, FloatConstraints(min=-1e40, max=1e40), 1e40)
