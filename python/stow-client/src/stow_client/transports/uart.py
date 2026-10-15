"""COBS-framed UART transport for the Stow protocol.

Framing: each message is COBS-encoded and terminated with a 0x00 byte.
"""

from __future__ import annotations

import asyncio

import serial_asyncio  # type: ignore[import-untyped]

from ..cobs import cobs_decode, cobs_encode
from ..transport import StowTransport


# ---------------------------------------------------------------------------
# Transport
# ---------------------------------------------------------------------------


class UartTransport(StowTransport):
    """Async UART transport using COBS framing.

    Parameters
    ----------
    port:
        Serial port path, e.g. ``"/dev/ttyACM0"``.
    baudrate:
        Baud rate (default 115200).
    """

    def __init__(self, port: str, baudrate: int = 115200) -> None:
        self._port = port
        self._baudrate = baudrate
        self._reader: asyncio.StreamReader | None = None
        self._writer: asyncio.StreamWriter | None = None

    async def _connect(self) -> None:
        if self._reader is None:
            self._reader, self._writer = await serial_asyncio.open_serial_connection(
                url=self._port, baudrate=self._baudrate
            )

    async def send(self, data: bytes) -> None:
        await self._connect()
        assert self._writer is not None
        self._writer.write(cobs_encode(data))
        await self._writer.drain()

    async def receive(self) -> bytes:
        await self._connect()
        assert self._reader is not None
        while True:
            frame = await self._reader.readuntil(b"\x00")
            try:
                return cobs_decode(frame[:-1])
            except ValueError:
                # Discard corrupt/partial frames
                continue

    async def close(self) -> None:
        if self._writer is not None:
            self._writer.close()
            try:
                await self._writer.wait_closed()
            except Exception:
                pass
            self._writer = None
            self._reader = None
