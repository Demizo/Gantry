from __future__ import annotations

from dataclasses import dataclass, field
from enum import Enum
from typing import Any

import cbor2


class ItemType(str, Enum):
    ENUM = "Enum"
    INT = "Int"
    FLOAT = "Float"
    STRING = "String"
    BYTE_ARRAY = "Byte Array"
    BUFFER = "Buffer"
    STRUCT = "Struct"


class Storage(str, Enum):
    EPHEMERAL = "Ephemeral"
    PERSISTENT = "Persistent"
    TOFU = "TOFU"


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
            values=[EnumValue(value=int(e["value"]), name=str(e["name"])) for e in raw]
        )
    if item_type is ItemType.INT:
        return IntConstraints(min=int(raw["min"]), max=int(raw["max"]))
    if item_type is ItemType.FLOAT:
        return FloatConstraints(min=float(raw["min"]), max=float(raw["max"]))
    if item_type in (ItemType.STRING, ItemType.BYTE_ARRAY, ItemType.BUFFER):
        return LengthConstraints(min_len=int(raw["min_len"]), max_len=int(raw["max_len"]))
    if item_type is ItemType.STRUCT:
        if not isinstance(raw, list):
            raise ValueError("Struct constraints must be a list of fields")
        fields_out: list[StructField] = []
        for f in raw:
            ftype = ItemType(f["type"])
            fields_out.append(
                StructField(
                    name=str(f["name"]),
                    type=ftype,
                    constraints=_parse_constraints(ftype, f["constraints"]),
                )
            )
        return StructConstraints(fields=fields_out)
    raise ValueError(f"Unknown item type: {item_type}")


def _parse_item(raw: dict[str, Any]) -> ItemDescription:
    item_type = ItemType(raw["type"])
    constraints = _parse_constraints(item_type, raw["constraints"])
    return ItemDescription(
        id=int(raw["id"]),
        name=str(raw["name"]),
        categories=[str(c) for c in raw.get("categories", [])],
        storage=Storage(raw["storage"]),
        read_perm=list(raw.get("read_perm", [])),
        write_perm=list(raw.get("write_perm", [])),
        type=item_type,
        default=raw.get("default"),
        constraints=constraints,
    )


def parse_description(blob: bytes) -> list[ItemDescription]:
    """Parse the joined CBOR Describe payload into ItemDescriptions."""
    decoded = cbor2.loads(blob)
    if not isinstance(decoded, list):
        raise ValueError("Description blob must decode to a CBOR list")
    return [_parse_item(entry) for entry in decoded]


# ---------------------------------------------------------------------------
# Value codec
#
# CBOR already gives us Python-native types for primitive values. The codec is
# primarily a normalization layer that:
#   * coerces byte_array / buffer payloads to ``bytes``
#   * recursively decodes struct fields against their constraints
#   * encodes a Python value back into the CBOR-ready type
# ---------------------------------------------------------------------------


def decode_value(item_type: ItemType, constraints: Constraints | None, value: Any) -> Any:
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
        return round(float(value), 4)
    if item_type in (ItemType.INT, ItemType.ENUM):
        return int(value)
    raise ValueError(f"Unknown item type {item_type}")


def encode_value(item_type: ItemType, constraints: Constraints | None, value: Any) -> Any:
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
        return round(float(value), 4)
    if item_type in (ItemType.INT, ItemType.ENUM):
        return int(value)
    raise ValueError(f"Unknown item type {item_type}")
