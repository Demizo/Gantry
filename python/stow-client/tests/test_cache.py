from __future__ import annotations

import os
import time
from pathlib import Path

from stow_client.cache import MAX_ENTRIES, DescriptionCache


def test_store_and_lookup(tmp_path: Path) -> None:
    cache = DescriptionCache(tmp_path)
    cache.store("abc", b"hello")
    assert cache.lookup("abc") == b"hello"


def test_lookup_missing_returns_none(tmp_path: Path) -> None:
    cache = DescriptionCache(tmp_path)
    assert cache.lookup("nope") is None


def test_default_cache_dir_uses_temp() -> None:
    cache = DescriptionCache()
    assert "stow-client" in str(cache.directory)


def _set_mtime(path: Path, ts: float) -> None:
    os.utime(path, (ts, ts))


def test_prune_keeps_only_most_recent(tmp_path: Path) -> None:
    cache = DescriptionCache(tmp_path)
    base = time.time() - 1000
    for i in range(MAX_ENTRIES + 2):
        digest = f"{i:02d}" * 32
        cache.store(digest, f"payload-{i}".encode())
        _set_mtime(cache.directory / f"{digest}.cbor", base + i)

    cache.store("zz" * 32, b"newest")

    remaining = sorted(p.stem for p in cache.directory.glob("*.cbor"))
    assert len(remaining) == MAX_ENTRIES
    assert "zz" * 32 in remaining
    assert "00" * 32 not in remaining


def test_lookup_refreshes_mtime(tmp_path: Path) -> None:
    cache = DescriptionCache(tmp_path)
    cache.store("aaa", b"first")

    old_ts = time.time() - 5000
    _set_mtime(cache.directory / "aaa.cbor", old_ts)

    cache.lookup("aaa")
    new_mtime = (cache.directory / "aaa.cbor").stat().st_mtime
    assert new_mtime > old_ts + 100
