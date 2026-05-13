from __future__ import annotations

from dataclasses import dataclass
from typing import Any, ClassVar

import cbor2


class StowProtocolError(Exception):
    """Raised when a frame cannot be decoded as a known message."""


@dataclass(frozen=True)
class Message:
    MSG_ID: ClassVar[int]

    def to_list(self) -> list[Any]:
        raise NotImplementedError

    def encode(self) -> bytes:
        return cbor2.dumps(self.to_list())


@dataclass(frozen=True)
class VersionRequest(Message):
    MSG_ID: ClassVar[int] = 0

    def to_list(self) -> list[Any]:
        return [self.MSG_ID]


@dataclass(frozen=True)
class VersionResponse(Message):
    MSG_ID: ClassVar[int] = 1
    version: int = 0

    def to_list(self) -> list[Any]:
        return [self.MSG_ID, self.version]


@dataclass(frozen=True)
class DescribeRequest(Message):
    MSG_ID: ClassVar[int] = 2
    start_item_id: int = 0

    def to_list(self) -> list[Any]:
        return [self.MSG_ID, self.start_item_id]


@dataclass(frozen=True)
class DescribeResponse(Message):
    MSG_ID: ClassVar[int] = 3
    next_item_id: int = 0
    has_more: bool = False
    chunk: bytes = b""

    def to_list(self) -> list[Any]:
        return [self.MSG_ID, self.next_item_id, self.has_more, self.chunk]


@dataclass(frozen=True)
class GetRequest(Message):
    MSG_ID: ClassVar[int] = 4
    item_id: int = 0

    def to_list(self) -> list[Any]:
        return [self.MSG_ID, self.item_id]


@dataclass(frozen=True)
class GetResponse(Message):
    MSG_ID: ClassVar[int] = 5
    item_id: int = 0
    value: Any = None

    def to_list(self) -> list[Any]:
        return [self.MSG_ID, self.item_id, self.value]


@dataclass(frozen=True)
class SetRequest(Message):
    MSG_ID: ClassVar[int] = 6
    item_id: int = 0
    value: Any = None

    def to_list(self) -> list[Any]:
        return [self.MSG_ID, self.item_id, self.value]


@dataclass(frozen=True)
class SubscribeRequest(Message):
    MSG_ID: ClassVar[int] = 10
    item_id: int = 0

    def to_list(self) -> list[Any]:
        return [self.MSG_ID, self.item_id]


@dataclass(frozen=True)
class UnsubscribeRequest(Message):
    MSG_ID: ClassVar[int] = 11
    item_id: int = 0

    def to_list(self) -> list[Any]:
        return [self.MSG_ID, self.item_id]


@dataclass(frozen=True)
class Update(Message):
    MSG_ID: ClassVar[int] = 12
    item_id: int = 0
    value: Any = None

    def to_list(self) -> list[Any]:
        return [self.MSG_ID, self.item_id, self.value]


@dataclass(frozen=True)
class Ok(Message):
    MSG_ID: ClassVar[int] = 13

    def to_list(self) -> list[Any]:
        return [self.MSG_ID]


@dataclass(frozen=True)
class Error(Message):
    MSG_ID: ClassVar[int] = 14
    code: int = 0

    def to_list(self) -> list[Any]:
        return [self.MSG_ID, self.code]


@dataclass(frozen=True)
class MultiGetRequest(Message):
    MSG_ID: ClassVar[int] = 7
    item_ids: tuple[int, ...] = ()

    def to_list(self) -> list[Any]:
        return [self.MSG_ID, *self.item_ids]


@dataclass(frozen=True)
class MultiGetResponse(Message):
    MSG_ID: ClassVar[int] = 8
    items: tuple[tuple[int, Any], ...] = ()

    def to_list(self) -> list[Any]:
        flat: list[Any] = [self.MSG_ID]
        for item_id, value in self.items:
            flat += [item_id, value]
        return flat


@dataclass(frozen=True)
class MultiSetRequest(Message):
    MSG_ID: ClassVar[int] = 9
    items: tuple[tuple[int, Any], ...] = ()

    def to_list(self) -> list[Any]:
        flat: list[Any] = [self.MSG_ID]
        for item_id, value in self.items:
            flat += [item_id, value]
        return flat


_MESSAGE_TYPES: dict[int, type[Message]] = {
    cls.MSG_ID: cls
    for cls in (
        VersionRequest,
        VersionResponse,
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
        MultiGetRequest,
        MultiGetResponse,
        MultiSetRequest,
    )
}


def decode(frame: bytes) -> Message:
    data = cbor2.loads(frame)
    if not isinstance(data, list) or not data:
        raise StowProtocolError(f"Expected non-empty CBOR list, got {data!r}")

    msg_id = data[0]
    cls = _MESSAGE_TYPES.get(msg_id)
    if cls is None:
        raise StowProtocolError(f"Unknown message id {msg_id}")

    payload = data[1:]
    if cls in (VersionRequest, Ok):
        return cls()
    if cls is VersionResponse:
        return VersionResponse(version=int(payload[0]))
    if cls is DescribeRequest:
        return DescribeRequest(start_item_id=int(payload[0]))
    if cls is DescribeResponse:
        next_item_id = int(payload[0])
        has_more = bool(payload[1])
        chunk = payload[2]
        if not isinstance(chunk, (bytes, bytearray)):
            raise StowProtocolError("DescribeResponse chunk must be a byte string")
        return DescribeResponse(next_item_id=next_item_id, has_more=has_more, chunk=bytes(chunk))
    if cls is GetRequest:
        return GetRequest(item_id=int(payload[0]))
    if cls is SubscribeRequest:
        return SubscribeRequest(item_id=int(payload[0]))
    if cls is UnsubscribeRequest:
        return UnsubscribeRequest(item_id=int(payload[0]))
    if cls is GetResponse:
        return GetResponse(item_id=int(payload[0]), value=payload[1])
    if cls is SetRequest:
        return SetRequest(item_id=int(payload[0]), value=payload[1])
    if cls is Update:
        return Update(item_id=int(payload[0]), value=payload[1])
    if cls is Error:
        return Error(code=int(payload[0]))
    if cls is MultiGetRequest:
        return MultiGetRequest(item_ids=tuple(int(i) for i in payload))
    if cls is MultiGetResponse:
        if len(payload) % 2 != 0:
            raise StowProtocolError("MultiGetResponse payload must have even length")
        items = tuple(
            (int(payload[i]), payload[i + 1]) for i in range(0, len(payload), 2)
        )
        return MultiGetResponse(items=items)
    if cls is MultiSetRequest:
        if len(payload) % 2 != 0:
            raise StowProtocolError("MultiSetRequest payload must have even length")
        items = tuple(
            (int(payload[i]), payload[i + 1]) for i in range(0, len(payload), 2)
        )
        return MultiSetRequest(items=items)
    raise StowProtocolError(f"Unhandled message class {cls}")
