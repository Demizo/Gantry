from __future__ import annotations

import abc


class StowTransport(abc.ABC):
    """Async transport for Stow messages; ``receive`` returns exactly one whole message per call."""

    @abc.abstractmethod
    async def send(self, data: bytes) -> None: ...

    @abc.abstractmethod
    async def receive(self) -> bytes: ...

    async def close(self) -> None:
        return None
