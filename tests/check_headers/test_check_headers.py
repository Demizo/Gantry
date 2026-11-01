import subprocess
import sys
from pathlib import Path

SCRIPT = Path(__file__).resolve().parents[2] / "tools" / "check_headers.py"
HEADER = "/*\n * Copyright (c) 2026 Demizo\n *\n * SPDX-License-Identifier: Apache-2.0\n */\n\n"


def run(tree: Path, *args: str) -> subprocess.CompletedProcess:
    return subprocess.run(
        [sys.executable, SCRIPT, *args], cwd=tree, capture_output=True, text=True
    )


def test_a_source_without_the_header_fails_the_check(tmp_path):
    (tmp_path / "lib").mkdir()
    (tmp_path / "lib" / "with.c").write_text(HEADER + "int a;\n")
    (tmp_path / "lib" / "without.h").write_text("int b;\n")

    result = run(tmp_path)

    assert result.returncode == 1
    assert result.stdout == "lib/without.h: missing the copyright header\n"
    assert (tmp_path / "lib" / "without.h").read_text() == "int b;\n"


def test_fix_inserts_the_header(tmp_path):
    (tmp_path / "samples").mkdir()
    source = tmp_path / "samples" / "main.c"
    source.write_text("int a;\n")

    assert run(tmp_path, "--fix").returncode == 0
    assert source.read_text() == HEADER + "int a;\n"
    assert run(tmp_path).returncode == 0


def test_an_unreadable_source_fails_the_check(tmp_path):
    (tmp_path / "tests").mkdir()
    (tmp_path / "tests" / "latin1.c").write_bytes(HEADER.encode() + b"/* \xe9 */\n")

    result = run(tmp_path, "--fix")

    assert result.returncode == 1
    assert result.stdout.startswith("tests/latin1.c: ")
