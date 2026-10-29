from __future__ import annotations

import math
import struct
from dataclasses import dataclass, field
from enum import IntEnum
from typing import Any, ClassVar

import cbor2

from .descriptions import role_names_from_mask

TAG_MAX = 23


class StowProtocolError(Exception):
    """Raised when a frame cannot be decoded as a known message."""


class ErrorCode(IntEnum):
    MALFORMED_MSG = 1
    UNKNOWN_MSG = 2
    INVALID_ITEM = 3
    OUT_OF_MEMORY = 4
    PERMISSION_DENIED = 5
    UNKNOWN = 6
    INVALID_VALUE = 7
    ALREADY_SET = 8
    TOO_LARGE = 9


class HelloKey(IntEnum):
    MAX_MESSAGE = 0
    STOW_HASH = 1
    ROLES = 2
    ROLE_NAMES = 3


class _Float32:
    __slots__ = ("value",)

    def __init__(self, value: float) -> None:
        self.value = value


def _fits_float32(value: float) -> bool:
    if math.isnan(value) or math.isinf(value):
        return True
    try:
        return struct.unpack(">f", struct.pack(">f", value))[0] == value
    except OverflowError:
        return False


def _mark_float32(obj: Any) -> Any:
    if isinstance(obj, float) and _fits_float32(obj):
        return _Float32(obj)
    if isinstance(obj, (list, tuple)):
        return [_mark_float32(item) for item in obj]
    if isinstance(obj, dict):
        return {key: _mark_float32(item) for key, item in obj.items()}
    return obj


def _encode_float32(encoder: cbor2.CBOREncoder, value: _Float32) -> None:
    encoder.write(b"\xfa" + struct.pack(">f", value.value))


@dataclass(frozen=True)
class Message:
    MSG_ID: ClassVar[int]

    def to_list(self) -> list[Any]:
        raise NotImplementedError

    def encode(self) -> bytes:
        """Encode as CBOR, sending floats that float32 holds exactly as float32."""
        return cbor2.dumps(_mark_float32(self.to_list()), default=_encode_float32)


@dataclass(frozen=True)
class Request(Message):
    """A message sent by a client."""

    tag: int = 0


@dataclass(frozen=True)
class Response(Message):
    """A message that answers a request."""

    tag: int | None = 0


@dataclass(frozen=True)
class _ItemListRequest(Request):
    """A request whose payload is a list of item IDs."""

    item_ids: tuple[int, ...] = ()

    def to_list(self) -> list[Any]:
        return [self.MSG_ID, self.tag, *self.item_ids]


def _flatten_pairs(
    msg_id: int, tag: int, items: tuple[tuple[int, Any], ...]
) -> list[Any]:
    flat: list[Any] = [msg_id, tag]
    for item_id, value in items:
        flat += [item_id, value]
    return flat


@dataclass(frozen=True)
class HelloRequest(Request):
    MSG_ID: ClassVar[int] = 0

    def to_list(self) -> list[Any]:
        return [self.MSG_ID, self.tag]


@dataclass(frozen=True)
class HelloResponse(Response):
    """The protocol version, the server's limit and the session's roles; ``fields`` keeps unknown keys."""

    MSG_ID: ClassVar[int] = 1
    version: int = 0
    fields: dict[int, Any] = field(default_factory=dict)

    def to_list(self) -> list[Any]:
        return [self.MSG_ID, self.tag, self.version, dict(self.fields)]

    @property
    def max_message(self) -> int | None:
        """Largest message the server can send; requests must fit within it too."""
        return self.fields.get(HelloKey.MAX_MESSAGE)

    @property
    def stow_hash(self) -> str | None:
        """StowHash of the server's description."""
        return self.fields.get(HelloKey.STOW_HASH)

    @property
    def role_names(self) -> list[str]:
        """Name of every role the server knows, indexed by its bit."""
        return list(self.fields.get(HelloKey.ROLE_NAMES, []))

    @property
    def role_mask(self) -> int:
        """Bitmask of the roles the session holds."""
        return int(self.fields.get(HelloKey.ROLES, 0))

    @property
    def roles(self) -> list[str]:
        """Names of the roles the session holds."""
        return role_names_from_mask(self.role_mask, self.role_names)


@dataclass(frozen=True)
class DescribeRequest(Request):
    MSG_ID: ClassVar[int] = 2
    start_item_id: int = 0
    names: tuple[str, ...] | None = None

    def to_list(self) -> list[Any]:
        out: list[Any] = [self.MSG_ID, self.tag, self.start_item_id]
        if self.names is not None:
            out.append(list(self.names))
        return out


@dataclass(frozen=True)
class DescribeResponse(Response):
    """A chunk of the description; ``next_item_id`` is None once every item is described."""

    MSG_ID: ClassVar[int] = 3
    next_item_id: int | None = None
    chunk: bytes = b""

    def to_list(self) -> list[Any]:
        return [self.MSG_ID, self.tag, self.next_item_id, self.chunk]


@dataclass(frozen=True)
class GetRequest(_ItemListRequest):
    MSG_ID: ClassVar[int] = 4


@dataclass(frozen=True)
class GetResponse(Response):
    MSG_ID: ClassVar[int] = 5
    items: tuple[tuple[int, Any], ...] = ()

    def to_list(self) -> list[Any]:
        return _flatten_pairs(self.MSG_ID, self.tag, self.items)


@dataclass(frozen=True)
class SetRequest(Request):
    MSG_ID: ClassVar[int] = 6
    items: tuple[tuple[int, Any], ...] = ()

    def to_list(self) -> list[Any]:
        return _flatten_pairs(self.MSG_ID, self.tag, self.items)


@dataclass(frozen=True)
class SubscribeRequest(_ItemListRequest):
    MSG_ID: ClassVar[int] = 7


@dataclass(frozen=True)
class UnsubscribeRequest(_ItemListRequest):
    MSG_ID: ClassVar[int] = 8


@dataclass(frozen=True)
class Update(Message):
    """The new value of a subscribed item."""

    MSG_ID: ClassVar[int] = 9
    item_id: int = 0
    value: Any = None

    def to_list(self) -> list[Any]:
        return [self.MSG_ID, None, self.item_id, self.value]


@dataclass(frozen=True)
class Ok(Response):
    MSG_ID: ClassVar[int] = 10

    def to_list(self) -> list[Any]:
        return [self.MSG_ID, self.tag]


@dataclass(frozen=True)
class Error(Response):
    """A failed request.

    ``tag`` is None when the request could not be decoded. ``item`` and ``index``
    name the failing item and entry, or are None.
    """

    MSG_ID: ClassVar[int] = 11
    code: int = 0
    item: int | None = None
    index: int | None = None

    def to_list(self) -> list[Any]:
        out: list[Any] = [self.MSG_ID, self.tag, self.code]
        if self.item is not None or self.index is not None:
            out.append(self.item)
        if self.index is not None:
            out.append(self.index)
        return out


_MESSAGE_TYPES: dict[int, type[Message]] = {
    cls.MSG_ID: cls
    for cls in (
        HelloRequest,
        HelloResponse,
        DescribeRequest,
        DescribeResponse,
        GetRequest,
        GetResponse,
        SetRequest,
        SubscribeRequest,
        UnsubscribeRequest,
        Update,
        Ok,
        Error,
    )
}


def _pairs(payload: list[Any], name: str) -> tuple[tuple[int, Any], ...]:
    if len(payload) % 2 != 0:
        raise StowProtocolError(f"{name} payload must have even length")
    return tuple((int(payload[i]), payload[i + 1]) for i in range(0, len(payload), 2))


def _decode_fields(cls: type[Message], payload: list[Any]) -> Message:
    raw_tag = payload[0]
    rest = payload[1:]
    if cls is Update:
        if raw_tag is not None:
            raise StowProtocolError("Update must carry a null tag")
        return Update(item_id=int(rest[0]), value=rest[1])
    if cls is Error:
        tag = None if raw_tag is None else int(raw_tag)
        item = rest[1] if len(rest) > 1 else None
        index = rest[2] if len(rest) > 2 else None
        return Error(
            tag=tag,
            code=int(rest[0]),
            item=None if item is None else int(item),
            index=None if index is None else int(index),
        )

    tag = int(raw_tag)
    if cls in (HelloRequest, Ok):
        return cls(tag=tag)
    if cls is HelloResponse:
        if not isinstance(rest[1], dict):
            raise StowProtocolError("HelloResponse must carry a map")
        return HelloResponse(tag=tag, version=int(rest[0]), fields=dict(rest[1]))
    if cls is DescribeRequest:
        names = tuple(str(n) for n in rest[1]) if len(rest) > 1 else None
        return DescribeRequest(tag=tag, start_item_id=int(rest[0]), names=names)
    if cls is DescribeResponse:
        chunk = rest[1]
        if not isinstance(chunk, (bytes, bytearray)):
            raise StowProtocolError("DescribeResponse chunk must be a byte string")
        next_item_id = None if rest[0] is None else int(rest[0])
        return DescribeResponse(tag=tag, next_item_id=next_item_id, chunk=bytes(chunk))
    if cls in (GetRequest, SubscribeRequest, UnsubscribeRequest):
        return cls(tag=tag, item_ids=tuple(int(i) for i in rest))
    if cls is GetResponse:
        return GetResponse(tag=tag, items=_pairs(rest, "GetResponse"))
    if cls is SetRequest:
        return SetRequest(tag=tag, items=_pairs(rest, "SetRequest"))
    raise StowProtocolError(f"Unhandled message class {cls}")


def decode(frame: bytes) -> Message:
    data = cbor2.loads(frame)
    if not isinstance(data, list) or not data:
        raise StowProtocolError(f"Expected non-empty CBOR list, got {data!r}")

    msg_id = data[0]
    cls = _MESSAGE_TYPES.get(msg_id)
    if cls is None:
        raise StowProtocolError(f"Unknown message id {msg_id}")

    try:
        return _decode_fields(cls, data[1:])
    except (IndexError, TypeError, ValueError) as e:
        raise StowProtocolError(f"Malformed {cls.__name__}: {data!r}") from e
