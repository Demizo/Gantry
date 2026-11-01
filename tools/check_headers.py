"""Checks that every C source carries the copyright header, or inserts it with --fix."""

import argparse
import sys
from pathlib import Path

HEADER = (
    "/*\n"
    " * Copyright (c) 2026 Demizo\n"
    " *\n"
    " * SPDX-License-Identifier: Apache-2.0\n"
    " */\n\n"
)

TARGETS = ["lib", "include", "samples", "tests"]


def sources():
    for target in TARGETS:
        for path in sorted(Path(target).rglob("*.[ch]")):
            if "build" not in path.parts:
                yield path


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--fix", action="store_true", help="insert the header where it is missing"
    )
    args = parser.parse_args()

    failed = False
    for path in sources():
        try:
            text = path.read_text(encoding="utf-8")
            if text.startswith(HEADER):
                continue
            if args.fix:
                path.write_text(HEADER + text, encoding="utf-8")
                print(f"{path}: added the copyright header")
            else:
                print(f"{path}: missing the copyright header")
                failed = True
        except (OSError, UnicodeError) as error:
            print(f"{path}: {error}")
            failed = True
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
