"""Command line that generates or checks the golden vectors of protocol spec folders."""

import argparse
import sys
from pathlib import Path

from .spec import OUTPUT, build


def run(command: str, folders: list[Path]) -> bool:
    """Generate or check the vectors of each folder, returning False on any failure."""
    ok = True
    for folder in folders:
        try:
            text = build(folder)
        except (OSError, ValueError) as error:
            print(f"{folder}: {error}", file=sys.stderr)
            ok = False
            continue
        output = folder / OUTPUT
        if command == "generate":
            output.write_text(text, encoding="utf-8", newline="\n")
        elif not output.is_file() or output.read_text(encoding="utf-8") != text:
            print(
                f"{output} is out of date; run protocol-spec generate", file=sys.stderr
            )
            ok = False
    return ok


def main() -> None:
    """Run protocol-spec generate or check over the given folders."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "command",
        choices=["generate", "check"],
        help="write vectors.json, or fail when it differs from what would be written",
    )
    parser.add_argument(
        "folders",
        nargs="+",
        type=Path,
        help="protocol spec folders, such as protocols/stow",
    )
    args = parser.parse_args()
    sys.exit(0 if run(args.command, args.folders) else 1)
