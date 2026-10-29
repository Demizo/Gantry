from __future__ import annotations

import asyncio
import inspect
import logging
from collections.abc import Awaitable, Callable
from typing import Any

from .messages import Message, Response, Update, decode
from .transport import StowTransport

log = logging.getLogger(__name__)

UpdateHandler = Callable[[Update], None]


class Receiver:
    """Owns the task that reads a transport, resolving responses by tag and passing Updates on.

    A tag sent without an answer is quarantined while its response may still
    arrive, as Tags in ``protocol.rst`` requires.
    """

    def __init__(
        self,
        transport: StowTransport,
        on_update: UpdateHandler,
        *,
        quarantine_grace: float,
    ) -> None:
        self._transport = transport
        self._on_update = on_update
        self._quarantine_grace = quarantine_grace
        self._task: asyncio.Task | None = None
        self._pending: dict[int, asyncio.Future[Message]] = {}
        self._quarantined: dict[int, asyncio.TimerHandle] = {}
        self._tag_waiters: list[asyncio.Future[None]] = []

    def start(self) -> None:
        if self._task is not None:
            return
        self._clear_quarantine()
        self._task = asyncio.create_task(self._run(), name="stow-client-receiver")

    async def stop(self) -> None:
        task = self._task
        self._task = None
        if task is not None:
            task.cancel()
            try:
                await task
            except (asyncio.CancelledError, Exception):
                pass
        for fut in self._pending.values():
            if not fut.done():
                fut.cancel()
        self._pending.clear()
        self._clear_quarantine()
        self._wake_tag_waiters()

    def tag_in_use(self, tag: int) -> bool:
        """Return True while *tag* awaits a response or is quarantined."""
        return tag in self._pending or tag in self._quarantined

    async def wait_for_free_tag(self) -> None:
        """Wait until a tag is released."""
        waiter: asyncio.Future[None] = asyncio.get_running_loop().create_future()
        self._tag_waiters.append(waiter)
        try:
            await waiter
        finally:
            if waiter in self._tag_waiters:
                self._tag_waiters.remove(waiter)

    def _quarantine(self, tag: int) -> None:
        handle = asyncio.get_running_loop().call_later(
            self._quarantine_grace, self._release, tag
        )
        self._quarantined[tag] = handle

    def _release(self, tag: int) -> None:
        handle = self._quarantined.pop(tag, None)
        if handle is not None:
            handle.cancel()
            self._wake_tag_waiters()

    def _clear_quarantine(self) -> None:
        for handle in self._quarantined.values():
            handle.cancel()
        self._quarantined.clear()

    def _wake_tag_waiters(self) -> None:
        waiters, self._tag_waiters = self._tag_waiters, []
        for waiter in waiters:
            if not waiter.done():
                waiter.set_result(None)

    def expect(self, tag: int) -> asyncio.Future[Message]:
        """Register a future resolved by the response carrying *tag*."""
        if tag in self._pending:
            raise RuntimeError(f"Tag {tag} is already awaiting a response")
        fut: asyncio.Future[Message] = asyncio.get_running_loop().create_future()
        self._pending[tag] = fut
        return fut

    def finish(self, tag: int, sent: bool) -> None:
        """Release *tag*, quarantining it when it was sent and its response has not arrived."""
        fut = self._pending.pop(tag, None)
        answered = fut is not None and fut.done() and not fut.cancelled()
        if fut is not None and not fut.done():
            fut.cancel()
        if sent and not answered:
            self._quarantine(tag)
            return
        self._wake_tag_waiters()

    async def _run(self) -> None:
        try:
            while True:
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

                self._dispatch(msg)
        except asyncio.CancelledError:
            return

    def _dispatch(self, msg: Message) -> None:
        if isinstance(msg, Update):
            try:
                self._on_update(msg)
            except Exception:
                log.exception("Failed to handle update for item %d", msg.item_id)
            return

        if isinstance(msg, Response):
            if msg.tag is None:
                log.warning("Server could not decode a request: %r", msg)
            elif msg.tag in self._quarantined:
                self._release(msg.tag)
            else:
                fut = self._pending.get(msg.tag)
                if fut is not None and not fut.done():
                    fut.set_result(msg)


_callback_tasks: set[asyncio.Task] = set()


def invoke_callback(cb: Callable[[Any], Awaitable[None] | None], msg: Message) -> None:
    """Call *cb* with *msg*, running it as a task when it is a coroutine, and log what it raises."""
    try:
        result = cb(msg)
    except Exception:
        log.exception("Callback raised")
        return
    if inspect.isawaitable(result):
        task = asyncio.ensure_future(_run_async_callback(result))
        _callback_tasks.add(task)
        task.add_done_callback(_callback_tasks.discard)


async def _run_async_callback(coro: Awaitable[Any]) -> None:
    try:
        await coro
    except Exception:
        log.exception("Async callback raised")
