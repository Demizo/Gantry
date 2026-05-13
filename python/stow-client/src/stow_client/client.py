from __future__ import annotations

import asyncio
import logging
from collections.abc import Awaitable, Callable
from pathlib import Path
from typing import Any

from . import messages as msg_mod
from ._receiver import Receiver, ResponseCallback
from .cache import DescriptionCache
from .descriptions import (
    ItemDescription,
    ItemType,
    decode_value,
    encode_value,
    parse_description,
)
from .messages import (
    DescribeRequest,
    DescribeResponse,
    Error,
    GetRequest,
    GetResponse,
    Message,
    MultiGetRequest,
    MultiGetResponse,
    MultiSetRequest,
    Ok,
    SetRequest,
    SubscribeRequest,
    UnsubscribeRequest,
    VersionRequest,
    VersionResponse,
)
from .transport import StowTransport

log = logging.getLogger(__name__)

PROTOCOL_VERSION = 1

_EXPECTED_RESPONSE: dict[type[msg_mod.Message], tuple[type[msg_mod.Message], ...]] = {
    VersionRequest: (VersionResponse,),
    DescribeRequest: (DescribeResponse,),
    GetRequest: (GetResponse, Error),
    SetRequest: (Ok, Error),
    SubscribeRequest: (Ok, Error),
    UnsubscribeRequest: (Ok, Error),
    MultiGetRequest: (MultiGetResponse, Error),
    MultiSetRequest: (Ok, Error),
}
STOW_HASH_ITEM_ID = 0
_DEFAULT_HANDSHAKE_TIMEOUT = 3.0


class StowError(RuntimeError):
    """Raised when the device returns an Error response to a sync request."""


class StowNotLoadedError(RuntimeError):
    """Raised when a public API is used before load() completes."""


class StowVersionMismatch(RuntimeError):
    """Raised when the device protocol version does not match."""


class UnexpectedResponseError(RuntimeError):
    """Raised when a sync request receives an unexpected response type."""


class StowClient:
    def __init__(
        self,
        transport: StowTransport,
        *,
        cache_dir: Path | None = None,
    ) -> None:
        self._transport = transport
        self._cache = DescriptionCache(cache_dir)
        self._receiver = Receiver(transport)
        self._cmd_lock = asyncio.Lock()
        self._items: list[ItemDescription] = []
        self._items_by_name: dict[str, ItemDescription] = {}
        self._items_by_id: dict[int, ItemDescription] = {}
        self._loaded = False

    async def __aenter__(self) -> "StowClient":
        await self.load()
        return self

    async def __aexit__(self, exc_type, exc, tb) -> None:
        await self.close()

    async def load(self) -> None:
        self._receiver.start()
        await self._handshake_version()
        digest = await self._fetch_hash()
        blob = self._cache.lookup(digest)
        if blob is None:
            blob = await self._fetch_description()
            self._cache.store(digest, blob)
        self._items = parse_description(blob)
        self._items_by_name = {it.name: it for it in self._items}
        self._items_by_id = {it.id: it for it in self._items}
        self._loaded = True
    
    async def close(self) -> None:
        await self._receiver.stop()
        try:
            await self._transport.close()
        except Exception:
            log.exception("Transport close raised")

    def get_all_items(self) -> list[ItemDescription]:
        self._require_loaded()
        return list(self._items)

    def get_item_description(self, item_name: str) -> ItemDescription:
        self._require_loaded()
        try:
            return self._items_by_name[item_name]
        except KeyError as e:
            raise KeyError(f"Unknown stow item: {item_name}") from e

    # ------------------------------------------------------------------
    # Core Stow API
    # ------------------------------------------------------------------

    async def get(self, item_name: str) -> None:
        self._require_loaded()
        item = self.get_item_description(item_name)
        await self._send(GetRequest(item_id=item.id))

    async def set(self, item_name: str, value: Any) -> None:
        self._require_loaded()
        item = self.get_item_description(item_name)
        wire_value = encode_value(item.type, item.constraints, value)
        await self._send(SetRequest(item_id=item.id, value=wire_value))

    async def subscribe(self, item_name: str) -> None:
        self._require_loaded()
        item = self.get_item_description(item_name)
        await self._send(SubscribeRequest(item_id=item.id))

    async def unsubscribe(self, item_name: str) -> None:
        self._require_loaded()
        item = self.get_item_description(item_name)
        await self._send(UnsubscribeRequest(item_id=item.id))

    async def multi_get(self, item_names: list[str]) -> None:
        self._require_loaded()
        ids = tuple(self.get_item_description(n).id for n in item_names)
        await self._send(MultiGetRequest(item_ids=ids))

    async def multi_set(self, items: dict[str, Any]) -> None:
        self._require_loaded()
        pairs: list[tuple[int, Any]] = []
        for name, value in items.items():
            desc = self.get_item_description(name)
            pairs.append((desc.id, encode_value(desc.type, desc.constraints, value)))
        await self._send(MultiSetRequest(items=tuple(pairs)))

    # ------------------------------------------------------------------
    # Synchronous operations (wait for response)
    # ------------------------------------------------------------------

    async def get_sync(self, item_name: str, timeout: float) -> GetResponse:
        self._require_loaded()
        item = self.get_item_description(item_name)
        async with self._cmd_lock:
            response = await self._request_response(
                GetRequest(item_id=item.id), timeout
            )
        if isinstance(response, Error):
            raise StowError(f"Error code {response.code}")
        if not isinstance(response, GetResponse):
            raise UnexpectedResponseError(
                f"Expected GetResponse, got {type(response).__name__}"
            )
        decoded = decode_value(item.type, item.constraints, response.value)
        return GetResponse(item_id=response.item_id, value=decoded)

    async def set_sync(self, item_name: str, value: Any, timeout: float) -> Ok:
        self._require_loaded()
        item = self.get_item_description(item_name)
        wire_value = encode_value(item.type, item.constraints, value)
        async with self._cmd_lock:
            response = await self._request_response(
                SetRequest(item_id=item.id, value=wire_value), timeout
            )
        return self._expect_ok(response)

    async def subscribe_sync(self, item_name: str, timeout: float) -> Ok:
        self._require_loaded()
        item = self.get_item_description(item_name)
        async with self._cmd_lock:
            response = await self._request_response(
                SubscribeRequest(item_id=item.id), timeout
            )
        return self._expect_ok(response)

    async def unsubscribe_sync(self, item_name: str, timeout: float) -> Ok:
        self._require_loaded()
        item = self.get_item_description(item_name)
        async with self._cmd_lock:
            response = await self._request_response(
                UnsubscribeRequest(item_id=item.id), timeout
            )
        return self._expect_ok(response)

    async def multi_get_sync(self, item_names: list[str], timeout: float) -> MultiGetResponse:
        self._require_loaded()
        descs = [self.get_item_description(n) for n in item_names]
        ids = tuple(d.id for d in descs)
        async with self._cmd_lock:
            response = await self._request_response(
                MultiGetRequest(item_ids=ids), timeout
            )
        if isinstance(response, Error):
            raise StowError(f"Error code {response.code}")
        if not isinstance(response, MultiGetResponse):
            raise UnexpectedResponseError(
                f"Expected MultiGetResponse, got {type(response).__name__}"
            )
        desc_by_id = {d.id: d for d in descs}
        decoded_items = tuple(
            (item_id, decode_value(desc_by_id[item_id].type, desc_by_id[item_id].constraints, val))
            for item_id, val in response.items
            if item_id in desc_by_id
        )
        return MultiGetResponse(items=decoded_items)

    async def multi_set_sync(self, items: dict[str, Any], timeout: float) -> Ok:
        self._require_loaded()
        pairs: list[tuple[int, Any]] = []
        for name, value in items.items():
            desc = self.get_item_description(name)
            pairs.append((desc.id, encode_value(desc.type, desc.constraints, value)))
        async with self._cmd_lock:
            response = await self._request_response(
                MultiSetRequest(items=tuple(pairs)), timeout
            )
        return self._expect_ok(response)

    # ------------------------------------------------------------------
    # Callbacks
    # ------------------------------------------------------------------

    def add_response_callback(self, callback: ResponseCallback) -> None:
        self._receiver.add_callback(callback)

    def remove_response_callback(self, callback: ResponseCallback) -> None:
        self._receiver.remove_callback(callback)

    # ------------------------------------------------------------------
    # Internal helpers
    # ------------------------------------------------------------------

    def _require_loaded(self) -> None:
        if not self._loaded:
            raise StowNotLoadedError("StowClient.load() must complete first")

    async def _send(self, message: Message) -> None:
        await self._transport.send(message.encode())

    async def _request_response(self, request: Message, timeout: float) -> Message:
        expected = _EXPECTED_RESPONSE.get(type(request))
        future = self._receiver.expect_response(expected)
        try:
            await self._send(request)
            return await asyncio.wait_for(future, timeout=timeout)
        except (asyncio.TimeoutError, asyncio.CancelledError):
            self._receiver.clear_expected()
            raise

    @staticmethod
    def _expect_ok(response: Message) -> Ok:
        if isinstance(response, Error):
            raise StowError(f"Error code {response.code}")
        if not isinstance(response, Ok):
            raise UnexpectedResponseError(
                f"Expected Ok, got {type(response).__name__}"
            )
        return response

    async def _handshake_version(self) -> None:
        async with self._cmd_lock:
            response = await self._request_response(
                VersionRequest(), _DEFAULT_HANDSHAKE_TIMEOUT
            )
        if not isinstance(response, VersionResponse):
            raise UnexpectedResponseError(
                f"Expected VersionResponse, got {type(response).__name__}"
            )
        if response.version != PROTOCOL_VERSION:
            raise StowVersionMismatch(
                f"Device reports protocol version {response.version}, "
                f"client supports {PROTOCOL_VERSION}"
            )

    async def _fetch_hash(self) -> str:
        async with self._cmd_lock:
            response = await self._request_response(
                GetRequest(item_id=STOW_HASH_ITEM_ID), _DEFAULT_HANDSHAKE_TIMEOUT
            )
        if isinstance(response, Error):
            raise StowError(f"Error code {response.code}")
        if not isinstance(response, GetResponse) or response.item_id != STOW_HASH_ITEM_ID:
            raise UnexpectedResponseError(
                f"Expected GetResponse for item 0, got {type(response).__name__}"
            )
        if not isinstance(response.value, str):
            raise UnexpectedResponseError("StowHash value must be a text string")
        return response.value

    async def _fetch_description(self) -> bytes:
        parts: list[bytes] = []
        async with self._cmd_lock:
            start_id = 0
            while True:
                response = await self._request_response(
                    DescribeRequest(start_item_id=start_id), _DEFAULT_HANDSHAKE_TIMEOUT
                )
                if not isinstance(response, DescribeResponse):
                    raise UnexpectedResponseError(
                        f"Expected DescribeResponse, got {type(response).__name__}"
                    )
                parts.append(response.chunk)
                if not response.has_more:
                    break
                start_id = response.next_item_id
        return b"".join(parts)
