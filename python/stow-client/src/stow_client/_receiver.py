from __future__ import annotations

import asyncio
import inspect
import logging
from collections import deque
from collections.abc import Awaitable, Callable
from typing import Any

from .messages import Message, Update, decode
from .transport import StowTransport

log = logging.getLogger(__name__)

ResponseCallback = Callable[[Message], Awaitable[None] | None]


class Receiver:
    """Owns the background task that consumes frames from a StowTransport."""

    def __init__(self, transport: StowTransport) -> None:
        self._transport = transport
        self._task: asyncio.Task | None = None
        self._callbacks: list[ResponseCallback] = []
        self._response_future: asyncio.Future[Message] | None = None
        self._expected_types: tuple[type[Message], ...] | None = None
        self._pending: deque[Message] = deque()
        self._stopping = False

    def start(self) -> None:
        if self._task is not None:
            return
        self._stopping = False
        self._task = asyncio.create_task(self._run(), name="stow-client-receiver")

    async def stop(self) -> None:
        self._stopping = True
        task = self._task
        self._task = None
        if task is None:
            return
        task.cancel()
        try:
            await task
        except (asyncio.CancelledError, Exception):
            pass
        if self._response_future is not None and not self._response_future.done():
            self._response_future.cancel()

    def add_callback(self, cb: ResponseCallback) -> None:
        if cb not in self._callbacks:
            self._callbacks.append(cb)

    def remove_callback(self, cb: ResponseCallback) -> None:
        try:
            self._callbacks.remove(cb)
        except ValueError:
            pass

    def expect_response(
        self,
        expected_types: tuple[type[Message], ...] | None = None,
    ) -> asyncio.Future[Message]:
        """Register a single-shot future for the next matching non-Update message.

        Messages that don't match *expected_types* are forwarded to callbacks
        and skipped; they belong to a concurrent fire-and-forget request.
        If *expected_types* is None any non-Update message matches.
        """
        if self._response_future is not None and not self._response_future.done():
            raise RuntimeError("A response is already being awaited")
        loop = asyncio.get_running_loop()
        fut: asyncio.Future[Message] = loop.create_future()
        # Drain pending messages: skip non-matching ones to callbacks.
        while self._pending:
            msg = self._pending.popleft()
            if expected_types is None or isinstance(msg, expected_types):
                fut.set_result(msg)
                return fut
            self._fire_callbacks(msg)
        self._expected_types = expected_types
        self._response_future = fut
        return fut

    def clear_expected(self) -> None:
        """Cancel the pending waiter and drop any buffered responses."""
        if self._response_future is not None and not self._response_future.done():
            self._response_future.cancel()
        self._response_future = None
        self._expected_types = None
        self._pending.clear()

    def _fire_callbacks(self, msg: Message) -> None:
        for cb in list(self._callbacks):
            try:
                result = cb(msg)
                if inspect.isawaitable(result):
                    asyncio.create_task(_run_async_callback(result))
            except Exception:
                log.exception("Response callback raised")

    async def _run(self) -> None:
        try:
            while not self._stopping:
                try:
                    frame = await self._transport.receive()
                except asyncio.CancelledError:
                    raise
                except Exception:
                    log.exception("Transport receive failed; stopping receiver")
                    return

                try:
                    msg = decode(frame)
                except Exception:
                    log.exception("Failed to decode frame: %r", frame)
                    continue

                await self._dispatch(msg)
        except asyncio.CancelledError:
            return

    async def _dispatch(self, msg: Message) -> None:
        if not isinstance(msg, Update):
            fut = self._response_future
            if fut is not None and not fut.done():
                expected = self._expected_types
                if expected is None or isinstance(msg, expected):
                    fut.set_result(msg)
                    self._response_future = None
                    self._expected_types = None
                    # Callbacks still run below, fall through.
                # else: wrong type for the sync waiter, callbacks handle it below.
            else:
                self._pending.append(msg)

        self._fire_callbacks(msg)


async def _run_async_callback(coro: Awaitable[Any]) -> None:
    try:
        await coro
    except Exception:
        log.exception("Async response callback raised")
