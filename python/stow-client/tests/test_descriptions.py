from __future__ import annotations

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

from .conftest import sample_description_blob


def test_parse_description_returns_all_items():
    items = parse_description(sample_description_blob())
    assert [i.name for i in items] == [
        "StowHash",
        "DeviceName",
        "TestInt",
        "TestFloat",
        "TestEnum",
        "TestBytes",
        "TestBuffer",
        "SpiTxBuffer",
    ]

    by_name = {i.name: i for i in items}
    assert by_name["DeviceName"].storage is Storage.PERSISTENT
    assert by_name["DeviceName"].read_perm == ["Any"]
    assert by_name["DeviceName"].write_perm == ["Session"]
    assert by_name["DeviceName"].type is ItemType.STRING
    assert isinstance(by_name["DeviceName"].constraints, LengthConstraints)

    assert by_name["StowHash"].read_perm == ["Any"]
    assert by_name["StowHash"].write_perm == []

    assert isinstance(by_name["TestInt"].constraints, IntConstraints)
    assert by_name["TestInt"].constraints.max == 100

    assert isinstance(by_name["TestFloat"].constraints, FloatConstraints)

    enum_constraints = by_name["TestEnum"].constraints
    assert isinstance(enum_constraints, EnumConstraints)
    assert {v.name for v in enum_constraints.values} == {"DISABLED", "ENABLED"}

    struct = by_name["SpiTxBuffer"]
    assert struct.type is ItemType.STRUCT
    assert isinstance(struct.constraints, StructConstraints)
    assert [f.name for f in struct.constraints.fields] == ["CS", "Bytes", "Text", "Buffer"]
    nested_enum = struct.constraints.fields[0].constraints
    assert isinstance(nested_enum, EnumConstraints)


def test_can_write_specific_role():
    items = parse_description(sample_description_blob())
    by_name = {i.name: i for i in items}

    # DeviceName writable by Session only
    assert by_name["DeviceName"].can_write("Session")
    assert not by_name["DeviceName"].can_write("Admin")
    assert not by_name["DeviceName"].can_write("User")


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
    items = parse_description(sample_description_blob())
    struct = next(i for i in items if i.name == "SpiTxBuffer")

    value = {
        "CS": 1,
        "Bytes": b"\x0a\x0b",
        "Text": "abc",
        "Buffer": b"\xaa\xbb\xcc",
    }
    wire = encode_value(struct.type, struct.constraints, value)
    assert isinstance(wire, list)
    assert wire == [1, b"\x0a\x0b", "abc", b"\xaa\xbb\xcc"]
    decoded = decode_value(struct.type, struct.constraints, wire)
    assert decoded == value


def test_encode_byte_array_accepts_list_of_ints():
    c = LengthConstraints(min_len=0, max_len=4)
    wire = encode_value(ItemType.BYTE_ARRAY, c, [1, 2, 3])
    assert wire == b"\x01\x02\x03"


def test_decode_struct_field_count_mismatch_raises():
    items = parse_description(sample_description_blob())
    struct = next(i for i in items if i.name == "SpiTxBuffer")
    with pytest.raises(ValueError):
        decode_value(struct.type, struct.constraints, [0, b""])
