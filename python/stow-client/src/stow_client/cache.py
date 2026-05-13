from __future__ import annotations

import os
import tempfile
import time
from pathlib import Path

MAX_ENTRIES = 10


class DescriptionCache:
    """LRU cache of Stow descriptions on disk, keyed by hash."""

    def __init__(self, cache_dir: Path | None = None) -> None:
        base = Path(cache_dir) if cache_dir is not None else Path(tempfile.gettempdir())
        self._dir = base / "stow-client"

    @property
    def directory(self) -> Path:
        return self._dir

    def _ensure_dir(self) -> None:
        self._dir.mkdir(parents=True, exist_ok=True)

    def _path_for(self, digest: str) -> Path:
        return self._dir / f"{digest}.cbor"

    def lookup(self, digest: str) -> bytes | None:
        path = self._path_for(digest)
        if not path.exists():
            return None
        try:
            data = path.read_bytes()
        except OSError:
            return None
        now = time.time()
        try:
            os.utime(path, (now, now))
        except OSError:
            pass
        return data

    def store(self, digest: str, blob: bytes) -> None:
        self._ensure_dir()
        path = self._path_for(digest)
        path.write_bytes(blob)
        now = time.time()
        try:
            os.utime(path, (now, now))
        except OSError:
            pass
        self._prune()

    def _prune(self) -> None:
        try:
            entries = [p for p in self._dir.iterdir() if p.is_file() and p.suffix == ".cbor"]
        except FileNotFoundError:
            return
        if len(entries) <= MAX_ENTRIES:
            return
        entries.sort(key=lambda p: p.stat().st_mtime, reverse=True)
        for stale in entries[MAX_ENTRIES:]:
            try:
                stale.unlink()
            except OSError:
                pass
