from __future__ import annotations

import abc


class StowTransport(abc.ABC):
    """Async byte-level transport for the Stow protocol.

    Implementations are responsible for delimiting messages: ``receive``
    must return exactly one complete CBOR frame per call.
    """

    @abc.abstractmethod
    async def send(self, data: bytes) -> None: ...

    @abc.abstractmethod
    async def receive(self) -> bytes: ...

    async def close(self) -> None:
        return None
