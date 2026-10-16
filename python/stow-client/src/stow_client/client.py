from __future__ import annotations

import asyncio
import dataclasses
import logging
from collections.abc import Awaitable, Callable, Iterable, Sequence
from pathlib import Path
from typing import Any

from ._receiver import Receiver, invoke_callback
from .cache import DescriptionCache
from .descriptions import (
    ItemDescription,
    decode_value,
    encode_value,
    parse_description,
)
from .messages import (
    TAG_MAX,
    DescribeRequest,
    DescribeResponse,
    Error,
    ErrorCode,
    GetRequest,
    GetResponse,
    HelloRequest,
    HelloResponse,
    Ok,
    Request,
    Response,
    SetRequest,
    SubscribeRequest,
    UnsubscribeRequest,
    Update,
)
from .transport import StowTransport

log = logging.getLogger(__name__)

PROTOCOL_VERSION = 1
DEFAULT_RESPONSE_TIMEOUT = 5.0
QUARANTINE_GRACE_FACTOR = 3

ItemListener = Callable[[Update], Awaitable[None] | None]


class StowError(RuntimeError):
    """Raised when the device returns an Error response to a request."""

    def __init__(self, error: Error) -> None:
        try:
            name = ErrorCode(error.code).name
        except ValueError:
            name = "UNRECOGNIZED"
        details = f"Error code {error.code} ({name})"
        if error.item is not None:
            details += f", item {error.item}"
        if error.index is not None:
            details += f", entry {error.index}"
        super().__init__(details)
        self.code = error.code
        self.tag = error.tag
        self.item = error.item
        self.index = error.index


class StowNotLoadedError(RuntimeError):
    """Raised when a public API is used before load() completes."""


class StowVersionMismatch(RuntimeError):
    """Raised when the device protocol version does not match."""


class StowRequestTooLargeError(ValueError):
    """Raised before sending a request larger than the server's ``max_message``."""


class UnexpectedResponseError(RuntimeError):
    """Raised when the device answers a request with a response that does not fit it."""


class StowClient:
    """Async Stow client that matches responses to requests by tag.

    Every request waits for its response, up to ``response_timeout`` seconds
    unless the call passes its own timeout. Up to 24 requests can be in flight
    at once, one per tag. The tag of a request that timed out stays
    quarantined until its late response arrives or ``quarantine_grace``
    seconds pass, by default ``QUARANTINE_GRACE_FACTOR`` times
    ``response_timeout``. The server drops requests it cannot take and never
    answers them, so the grace period returns their tags. A late response for
    a released tag is ignored.
    """

    def __init__(
        self,
        transport: StowTransport,
        *,
        cache_dir: Path | None = None,
        response_timeout: float = DEFAULT_RESPONSE_TIMEOUT,
        quarantine_grace: float | None = None,
    ) -> None:
        if quarantine_grace is None:
            quarantine_grace = QUARANTINE_GRACE_FACTOR * response_timeout
        self._transport = transport
        self._cache = DescriptionCache(cache_dir)
        self._receiver = Receiver(
            transport, self._on_update, quarantine_grace=quarantine_grace
        )
        self._listeners: dict[int, list[ItemListener]] = {}
        self._response_timeout = response_timeout
        self._next_tag = 0
        self._items: list[ItemDescription] = []
        self._items_by_name: dict[str, ItemDescription] = {}
        self._items_by_id: dict[int, ItemDescription] = {}
        self._hello: HelloResponse | None = None
        self._loaded = False

    async def __aenter__(self) -> StowClient:
        await self.load()
        return self

    async def __aexit__(self, exc_type, exc, tb) -> None:
        await self.close()

    async def load(self) -> None:
        """Send Hello, then read the description from the cache or the device."""
        self._receiver.start()
        hello = await self.hello()
        digest = hello.stow_hash
        if not isinstance(digest, str):
            raise UnexpectedResponseError("Hello does not report a StowHash")
        items = None
        blob = self._cache.lookup(digest)
        if blob is not None:
            try:
                items = parse_description(blob, hello.role_names)
            except (KeyError, TypeError, ValueError):
                log.warning("Discarding a cached description that does not parse")
                self._cache.discard(digest)
        if items is None:
            blob = await self._fetch_description(None, None)
            items = parse_description(blob, hello.role_names)
            self._cache.store(digest, blob)
        self._items = items
        self._items_by_name = {it.name: it for it in self._items}
        self._items_by_id = {it.id: it for it in self._items}
        self._loaded = True

    async def close(self) -> None:
        await self._receiver.stop()
        try:
            await self._transport.close()
        except Exception:
            log.exception("Transport close raised")

    @property
    def hello_response(self) -> HelloResponse | None:
        """The Hello response read by load(), or None before it completes."""
        return self._hello

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

    async def hello(self, timeout: float | None = None) -> HelloResponse:
        """Exchange Hello and check the protocol version."""
        response = await self._request_typed(HelloRequest(), HelloResponse, timeout)
        if response.version != PROTOCOL_VERSION:
            raise StowVersionMismatch(
                f"Device reports protocol version {response.version}, "
                f"client supports {PROTOCOL_VERSION}"
            )
        self._hello = response
        return response

    async def get(
        self, item_names: str | Sequence[str], timeout: float | None = None
    ) -> Any:
        """Get one item's value, or a dict of values when given a list of names."""
        self._require_loaded()
        names = [item_names] if isinstance(item_names, str) else list(item_names)
        descs = [self.get_item_description(n) for n in names]
        response = await self._request_typed(
            GetRequest(item_ids=tuple(d.id for d in descs)), GetResponse, timeout
        )
        if [item_id for item_id, _ in response.items] != [d.id for d in descs]:
            raise UnexpectedResponseError(
                f"Expected items {[d.id for d in descs]}, got {response.items}"
            )
        values = {
            d.name: decode_value(d.type, d.constraints, value)
            for d, (_, value) in zip(descs, response.items, strict=True)
        }
        return values[item_names] if isinstance(item_names, str) else values

    async def set(
        self,
        items: str | dict[str, Any],
        value: Any = None,
        timeout: float | None = None,
    ) -> None:
        """Set one item, ``set(name, value)``, or several, ``set({name: value, ...})``."""
        self._require_loaded()
        if isinstance(items, str):
            items = {items: value}
        pairs = []
        for name, item_value in items.items():
            desc = self.get_item_description(name)
            pairs.append(
                (desc.id, encode_value(desc.type, desc.constraints, item_value))
            )
        await self._request_typed(SetRequest(items=tuple(pairs)), Ok, timeout)

    async def subscribe(
        self,
        item_names: str | Sequence[str],
        timeout: float | None = None,
        listener: ItemListener | None = None,
    ) -> None:
        """Subscribe to items; *listener* gets their Updates until they are unsubscribed."""
        self._require_loaded()
        item_ids = self._ids(item_names)
        added = self._add_listener(item_ids, listener)
        try:
            await self._request_typed(SubscribeRequest(item_ids=item_ids), Ok, timeout)
        except StowError as error:
            start = error.index if error.index is not None else 0
            kept = set(item_ids[:start])
            self._remove_listener(
                [i for i in item_ids[start:] if i in added and i not in kept], listener
            )
            raise
        except BaseException:
            self._remove_listener(added, listener)
            raise

    async def unsubscribe(
        self, item_names: str | Sequence[str], timeout: float | None = None
    ) -> None:
        self._require_loaded()
        item_ids = self._ids(item_names)
        await self._request_typed(UnsubscribeRequest(item_ids=item_ids), Ok, timeout)
        for item_id in item_ids:
            self._listeners.pop(item_id, None)

    async def describe_items(
        self, item_names: Sequence[str], timeout: float | None = None
    ) -> list[ItemDescription]:
        """Describe only the named items. Names the device does not know are absent."""
        self._require_loaded()
        blob = await self._fetch_description(tuple(item_names), timeout)
        return parse_description(blob, self._hello.role_names)

    # ------------------------------------------------------------------
    # Internal helpers
    # ------------------------------------------------------------------

    def _on_update(self, update: Update) -> None:
        item = self._items_by_id.get(update.item_id)
        if item is not None:
            update = Update(
                item_id=update.item_id,
                value=decode_value(item.type, item.constraints, update.value),
            )
        for listener in list(self._listeners.get(update.item_id, [])):
            invoke_callback(listener, update)

    def _add_listener(
        self, item_ids: Sequence[int], listener: ItemListener | None
    ) -> set[int]:
        added: set[int] = set()
        if listener is None:
            return added
        for item_id in item_ids:
            listeners = self._listeners.setdefault(item_id, [])
            if listener not in listeners:
                listeners.append(listener)
                added.add(item_id)
        return added

    def _remove_listener(
        self, item_ids: Iterable[int], listener: ItemListener | None
    ) -> None:
        if listener is None:
            return
        for item_id in item_ids:
            listeners = self._listeners.get(item_id, [])
            if listener in listeners:
                listeners.remove(listener)
            if not listeners:
                self._listeners.pop(item_id, None)

    def _require_loaded(self) -> None:
        if not self._loaded:
            raise StowNotLoadedError("StowClient.load() must complete first")

    def _ids(self, item_names: str | Sequence[str]) -> tuple[int, ...]:
        if isinstance(item_names, str):
            item_names = [item_names]
        return tuple(self.get_item_description(n).id for n in item_names)

    async def _allocate_tag(self) -> int:
        while True:
            for offset in range(TAG_MAX + 1):
                tag = (self._next_tag + offset) % (TAG_MAX + 1)
                if not self._receiver.tag_in_use(tag):
                    self._next_tag = (tag + 1) % (TAG_MAX + 1)
                    return tag
            await self._receiver.wait_for_free_tag()

    def _encode(self, request: Request, tag: int) -> bytes:
        frame = dataclasses.replace(request, tag=tag).encode()
        limit = self._hello.max_message if self._hello is not None else None
        if limit is not None and len(frame) > limit:
            raise StowRequestTooLargeError(
                f"{type(request).__name__} is {len(frame)} bytes; "
                f"the server's max_message is {limit}"
            )
        return frame

    async def _request(self, request: Request, timeout: float | None) -> Response:
        async with asyncio.timeout(
            self._response_timeout if timeout is None else timeout
        ):
            tag = await self._allocate_tag()
            future = self._receiver.expect(tag)
            sent = False
            try:
                frame = self._encode(request, tag)
                sent = True
                try:
                    await self._transport.send(frame)
                except Exception:
                    sent = False
                    raise
                return await future
            finally:
                self._receiver.finish(tag, sent)

    async def _request_typed(
        self, request: Request, expected: type, timeout: float | None
    ):
        response = await self._request(request, timeout)
        if isinstance(response, Error):
            raise StowError(response)
        if not isinstance(response, expected):
            raise UnexpectedResponseError(
                f"Expected {expected.__name__}, got {type(response).__name__}"
            )
        return response

    async def _fetch_description(
        self, names: tuple[str, ...] | None, timeout: float | None
    ) -> bytes:
        parts: list[bytes] = []
        start_id = 0
        while True:
            response = await self._request_typed(
                DescribeRequest(start_item_id=start_id, names=names),
                DescribeResponse,
                timeout,
            )
            parts.append(response.chunk)
            if response.next_item_id is None:
                break
            if response.next_item_id <= start_id:
                raise UnexpectedResponseError(
                    f"Describe made no progress at item {start_id}"
                )
            start_id = response.next_item_id
        return b"".join(parts)
