from __future__ import annotations

import io
import struct
from collections.abc import Sequence
from dataclasses import dataclass, field
from enum import IntEnum
from typing import Any

import cbor2


class ItemType(IntEnum):
    ENUM = 0
    INT = 1
    FLOAT = 2
    STRING = 3
    BYTE_ARRAY = 4
    BUFFER = 5
    STRUCT = 6


class Storage(IntEnum):
    EPHEMERAL = 0
    PERSISTENT = 1
    TOFU = 2


class ConstraintKey(IntEnum):
    """Keys of the map that bounds a value or a length."""

    MIN = 0
    MAX = 1


class EnumConstraintKey(IntEnum):
    """Keys of the map that describes one value of an Enum."""

    VALUE = 0
    NAME = 1


class FieldConstraintKey(IntEnum):
    """Keys of the map that describes one field of a Struct."""

    NAME = 0
    TYPE = 1
    CONSTRAINTS = 2


class DescribeKey(IntEnum):
    """Keys of the map that describes one item."""

    ID = 0
    NAME = 1
    CATEGORIES = 2
    STORAGE = 3
    READ_PERM = 4
    WRITE_PERM = 5
    TYPE = 6
    DEFAULT = 7
    CONSTRAINTS = 8


@dataclass(frozen=True)
class EnumValue:
    value: int
    name: str


@dataclass(frozen=True)
class EnumConstraints:
    values: list[EnumValue]


@dataclass(frozen=True)
class IntConstraints:
    min: int
    max: int


@dataclass(frozen=True)
class FloatConstraints:
    min: float
    max: float


@dataclass(frozen=True)
class LengthConstraints:
    min_len: int
    max_len: int


@dataclass(frozen=True)
class StructField:
    name: str
    type: ItemType
    constraints: "Constraints"


@dataclass(frozen=True)
class StructConstraints:
    fields: list[StructField]


Constraints = (
    EnumConstraints
    | IntConstraints
    | FloatConstraints
    | LengthConstraints
    | StructConstraints
)


@dataclass(frozen=True)
class ItemDescription:
    id: int
    name: str
    categories: list[str] = field(default_factory=list)
    storage: Storage = Storage.EPHEMERAL
    read_perm: list[str] = field(default_factory=list)
    write_perm: list[str] = field(default_factory=list)
    type: ItemType = ItemType.INT
    default: Any = None
    constraints: Constraints | None = None

    def can_read(self, role: str) -> bool:
        """Return True if the given role name is permitted to read this item."""
        return role in self.read_perm

    def can_write(self, role: str) -> bool:
        """Return True if the given role name is permitted to write this item."""
        return role in self.write_perm


# ---------------------------------------------------------------------------
# Description parsing
# ---------------------------------------------------------------------------


def _parse_constraints(item_type: ItemType, raw: Any) -> Constraints:
    if item_type is ItemType.ENUM:
        if not isinstance(raw, list):
            raise ValueError("Enum constraints must be a list")
        return EnumConstraints(
            values=[
                EnumValue(
                    value=int(e[EnumConstraintKey.VALUE]),
                    name=str(e[EnumConstraintKey.NAME]),
                )
                for e in raw
            ]
        )
    if item_type is ItemType.INT:
        return IntConstraints(
            min=int(raw[ConstraintKey.MIN]), max=int(raw[ConstraintKey.MAX])
        )
    if item_type is ItemType.FLOAT:
        return FloatConstraints(
            min=float(raw[ConstraintKey.MIN]), max=float(raw[ConstraintKey.MAX])
        )
    if item_type in (ItemType.STRING, ItemType.BYTE_ARRAY, ItemType.BUFFER):
        return LengthConstraints(
            min_len=int(raw[ConstraintKey.MIN]), max_len=int(raw[ConstraintKey.MAX])
        )
    if item_type is ItemType.STRUCT:
        if not isinstance(raw, list):
            raise ValueError("Struct constraints must be a list of fields")
        fields_out: list[StructField] = []
        for f in raw:
            ftype = ItemType(f[FieldConstraintKey.TYPE])
            fields_out.append(
                StructField(
                    name=str(f[FieldConstraintKey.NAME]),
                    type=ftype,
                    constraints=_parse_constraints(
                        ftype, f[FieldConstraintKey.CONSTRAINTS]
                    ),
                )
            )
        return StructConstraints(fields=fields_out)
    raise ValueError(f"Unknown item type: {item_type}")


def role_names_from_mask(mask: int, role_names: Sequence[str]) -> list[str]:
    """Name the roles set in *mask*, using the server's role table."""
    return [name for bit, name in enumerate(role_names) if mask & (1 << bit)]


def _parse_item(raw: dict[int, Any], role_names: Sequence[str]) -> ItemDescription:
    item_type = ItemType(raw[DescribeKey.TYPE])
    constraints = _parse_constraints(item_type, raw[DescribeKey.CONSTRAINTS])
    return ItemDescription(
        id=int(raw[DescribeKey.ID]),
        name=str(raw[DescribeKey.NAME]),
        categories=[str(c) for c in raw.get(DescribeKey.CATEGORIES, [])],
        storage=Storage(raw[DescribeKey.STORAGE]),
        read_perm=role_names_from_mask(raw.get(DescribeKey.READ_PERM, 0), role_names),
        write_perm=role_names_from_mask(raw.get(DescribeKey.WRITE_PERM, 0), role_names),
        type=item_type,
        default=raw.get(DescribeKey.DEFAULT),
        constraints=constraints,
    )


def _decode_sequence(blob: bytes) -> list[Any]:
    stream = io.BytesIO(blob)
    decoder = cbor2.CBORDecoder(stream)
    entries: list[Any] = []
    while stream.tell() < len(blob):
        entries.append(decoder.decode())
    return entries


def parse_description(
    blob: bytes, role_names: Sequence[str] = ()
) -> list[ItemDescription]:
    """Parse joined Describe chunks, a CBOR sequence of item maps, into ItemDescriptions.

    *role_names* is the server's role table from Hello, used to name the roles
    in each item's permission bitmasks.
    """
    items = []
    for entry in _decode_sequence(blob):
        if not isinstance(entry, dict):
            raise ValueError("A description entry must be a CBOR map")
        items.append(_parse_item(entry, role_names))
    return items


# ---------------------------------------------------------------------------
# Value codec
#
# CBOR already gives us Python-native types for primitive values. The codec is
# primarily a normalization layer that:
#   * coerces byte_array / buffer payloads to ``bytes``
#   * recursively decodes struct fields against their constraints
#   * encodes a Python value back into the CBOR-ready type
# ---------------------------------------------------------------------------


def decode_value(
    item_type: ItemType, constraints: Constraints | None, value: Any
) -> Any:
    if item_type is ItemType.STRUCT:
        if not isinstance(constraints, StructConstraints):
            raise ValueError("STRUCT decode requires StructConstraints")
        if not isinstance(value, list):
            raise ValueError("STRUCT value must be a CBOR list")
        if len(value) != len(constraints.fields):
            raise ValueError(
                f"STRUCT field count mismatch: got {len(value)}, expected {len(constraints.fields)}"
            )
        return {
            f.name: decode_value(f.type, f.constraints, v)
            for f, v in zip(constraints.fields, value)
        }
    if item_type in (ItemType.BYTE_ARRAY, ItemType.BUFFER):
        if isinstance(value, (bytes, bytearray)):
            return bytes(value)
        raise ValueError(f"{item_type.value} value must be a byte string")
    if item_type is ItemType.STRING:
        return str(value)
    if item_type is ItemType.FLOAT:
        return float(value)
    if item_type in (ItemType.INT, ItemType.ENUM):
        return int(value)
    raise ValueError(f"Unknown item type {item_type}")


def encode_value(
    item_type: ItemType, constraints: Constraints | None, value: Any
) -> Any:
    if item_type is ItemType.STRUCT:
        if not isinstance(constraints, StructConstraints):
            raise ValueError("STRUCT encode requires StructConstraints")
        if isinstance(value, dict):
            ordered = [value[f.name] for f in constraints.fields]
        elif isinstance(value, list):
            if len(value) != len(constraints.fields):
                raise ValueError("STRUCT list value must match field count")
            ordered = value
        else:
            raise ValueError("STRUCT value must be a dict or list")
        return [
            encode_value(f.type, f.constraints, v)
            for f, v in zip(constraints.fields, ordered)
        ]
    if item_type in (ItemType.BYTE_ARRAY, ItemType.BUFFER):
        if isinstance(value, (bytes, bytearray)):
            return bytes(value)
        if isinstance(value, list):
            return bytes(value)
        raise ValueError(f"{item_type.value} value must be bytes or a list of ints")
    if item_type is ItemType.STRING:
        return str(value)
    if item_type is ItemType.FLOAT:
        try:
            return struct.unpack("<f", struct.pack("<f", float(value)))[0]
        except OverflowError as exc:
            raise ValueError(f"{value} is outside the float32 range") from exc
    if item_type in (ItemType.INT, ItemType.ENUM):
        return int(value)
    raise ValueError(f"Unknown item type {item_type}")
