"""A node: one native_sim firmware process, driven through its console on stdin and stdout."""

from __future__ import annotations

import logging
import re
import subprocess
import threading
import time
from pathlib import Path

_ESCAPE = re.compile(r"\x1b\[[0-9;]*[A-Za-z]")


class Node:
    """Runs one image from ``run_dir``.

    The console is the process's stdin and stdout. Every output line is kept,
    without terminal escape sequences, so a test can wait for a line that
    arrived before it started waiting.
    The PTY of the second UART, ``uart_1``, is the node's link port.
    """

    def __init__(self, name: str, image: Path, run_dir: Path) -> None:
        self.name = name
        self.image = Path(image)
        self.run_dir = Path(run_dir)
        self.pty: str | None = None
        self._log = logging.getLogger(f"link_broker.node.{name}")
        self._lines: list[str] = []
        self._cond = threading.Condition()
        self._exited = False
        self._proc: subprocess.Popen[bytes] | None = None
        self._reader: threading.Thread | None = None

    def start(self, timeout: float = 10.0) -> None:
        """Start the process and wait until its link PTY exists."""
        self.run_dir.mkdir(parents=True, exist_ok=True)
        self._proc = subprocess.Popen(
            ["stdbuf", "-oL", str(self.image)],
            cwd=self.run_dir,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
        )
        self._reader = threading.Thread(
            target=self._read, name=f"node-{self.name}", daemon=True
        )
        self._reader.start()
        match = self.expect(r"^uart_1 connected to pseudotty: (\S+)", timeout=timeout)
        self.pty = match.group(1)

    def stop(self) -> None:
        if self._proc is None:
            return
        self._proc.terminate()
        try:
            self._proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            self._proc.kill()
            self._proc.wait()
        if self._reader is not None:
            self._reader.join(timeout=5)
        for pipe in (self._proc.stdin, self._proc.stdout):
            if pipe is not None:
                pipe.close()
        self._proc = None

    @property
    def lines(self) -> list[str]:
        """Every console line so far."""
        with self._cond:
            return list(self._lines)

    def mark(self) -> int:
        """Position after the last line so far, for ``expect(since=...)``."""
        with self._cond:
            return len(self._lines)

    def send(self, line: str) -> None:
        """Write one console line."""
        assert self._proc is not None and self._proc.stdin is not None
        self._log.debug("<< %s", line)
        self._proc.stdin.write(line.encode() + b"\n")
        self._proc.stdin.flush()

    def expect(
        self, pattern: str, since: int = 0, timeout: float = 5.0
    ) -> re.Match[str]:
        """Wait for the first line at or after ``since`` that matches ``pattern``."""
        regex = re.compile(pattern)
        deadline = time.monotonic() + timeout
        index = since
        with self._cond:
            while True:
                while index < len(self._lines):
                    if match := regex.search(self._lines[index]):
                        return match
                    index += 1
                if self._exited:
                    raise RuntimeError(f"{self.name} exited before {pattern!r}")
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    raise TimeoutError(f"{self.name}: no line matching {pattern!r}")
                self._cond.wait(remaining)

    def _read(self) -> None:
        assert self._proc is not None and self._proc.stdout is not None
        for raw in self._proc.stdout:
            line = _ESCAPE.sub("", raw.decode(errors="replace")).rstrip("\r\n")
            self._log.debug(">> %s", line)
            with self._cond:
                self._lines.append(line)
                self._cond.notify_all()
        with self._cond:
            self._exited = True
            self._cond.notify_all()
