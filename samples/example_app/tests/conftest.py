from __future__ import annotations

import asyncio
import logging
import os
from pathlib import Path

import pytest
from stow_client import Storage, StowClient
from stow_client.transports import UartTransport

log = logging.getLogger(__name__)

# Stow cache that survives across test runs
_PERSISTENT_CACHE_DIR = Path(__file__).parent.parent / ".stow_cache"


async def _snapshot_persistent(port: str, baud: int) -> dict:
    transport = UartTransport(port=port, baudrate=baud)
    client = StowClient(transport, cache_dir=_PERSISTENT_CACHE_DIR)
    saved: dict = {}
    async with client:
        for item in client.get_all_items():
            if item.storage is Storage.EPHEMERAL:
                continue
            try:
                resp = await client.get_sync(item.name, timeout=2.0)
                saved[item.name] = resp.value
            except Exception:
                log.warning("Could not snapshot %s", item.name)
    return saved


async def _restore_persistent(port: str, baud: int, saved: dict) -> None:
    transport = UartTransport(port=port, baudrate=baud)
    client = StowClient(transport, cache_dir=_PERSISTENT_CACHE_DIR)
    async with client:
        for name, value in saved.items():
            try:
                await client.set_sync(name, value, timeout=2.0)
            except Exception:
                log.warning("Could not restore %s", name)


@pytest.fixture(scope="session", autouse=True)
def restore_persistent_values(request):
    """Save non-ephemeral item values before the session; restore them afterwards."""
    port = request.config.getoption("--uart-port")
    if not port:
        yield
        return
    baud = request.config.getoption("--uart-baud")
    saved = asyncio.run(_snapshot_persistent(port, baud))
    yield
    asyncio.run(_restore_persistent(port, baud, saved))


def pytest_addoption(parser):
    parser.addoption(
        "--uart-port",
        default=os.environ.get("STOW_UART_PORT", ""),
        help="Serial port for integration tests (e.g. /dev/ttyACM0)",
    )
    parser.addoption(
        "--uart-baud",
        type=int,
        default=int(os.environ.get("STOW_UART_BAUD", "115200")),
        help="Baud rate for the UART port (default: 115200)",
    )


def pytest_configure(config):
    config.addinivalue_line(
        "markers", "integration: mark test as requiring physical hardware"
    )


def _require_port(config) -> str:
    port = config.getoption("--uart-port")
    if not port:
        pytest.skip("No --uart-port specified; skipping integration test")
    return port


@pytest.fixture
async def uart_client(request):
    """A Stow client connected and loaded over UART"""
    port = _require_port(request.config)
    baud = request.config.getoption("--uart-baud")
    transport = UartTransport(port=port, baudrate=baud)
    client = StowClient(transport, cache_dir=_PERSISTENT_CACHE_DIR)
    async with client:
        yield client


@pytest.fixture
async def uart_transport(request):
    """Open the UART transport with no client attached"""
    port = _require_port(request.config)
    baud = request.config.getoption("--uart-baud")
    transport = UartTransport(port=port, baudrate=baud)
    yield transport
    await transport.close()
