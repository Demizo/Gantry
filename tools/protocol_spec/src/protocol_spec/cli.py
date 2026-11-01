"""Command line that checks a protocol folder's vectors, or renders them or its constants for other consumers."""

import argparse
import sys
from pathlib import Path

from .constants import constants, to_header


def _write(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8", newline="\n")


def run(args: argparse.Namespace) -> bool:
    """Run one command, printing problems to stderr; False when there were any."""
    try:
        if args.command == "constants":
            _write(args.output, to_header(constants(args.folder)))
            return True
        from .output import to_c, to_json
        from .vectors import load, uncovered

        if args.command == "check":
            missing = uncovered(args.folder)
            for line in missing:
                print(f"{args.folder}: {line}", file=sys.stderr)
            return not missing
        vectors = load(args.folder)
    except (OSError, ValueError) as error:
        print(f"{args.folder}: {error}", file=sys.stderr)
        return False
    if args.command == "c":
        for name, text in to_c(vectors, args.prefix).items():
            _write(args.output_dir / name, text)
    else:
        _write(args.output, to_json(vectors))
    return True


def parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="protocol-spec", description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    check = commands.add_parser(
        "check", help="validate every vector and check every message kind has one"
    )
    check.add_argument(
        "folder", type=Path, help="protocol folder, such as protocols/stow"
    )
    render_c = commands.add_parser(
        "c", help="render the vectors as C data and a header that lists them by name"
    )
    render_c.add_argument("folder", type=Path)
    render_c.add_argument(
        "--prefix", required=True, help="prefix of the C names and files"
    )
    render_c.add_argument("--output-dir", required=True, type=Path)
    export = commands.add_parser("json", help="export the vectors as JSON")
    export.add_argument("folder", type=Path)
    export.add_argument("--output", required=True, type=Path)
    header = commands.add_parser(
        "constants", help="render the CDDL integers constants.yaml names as a C header"
    )
    header.add_argument("folder", type=Path)
    header.add_argument("--output", required=True, type=Path)
    return parser


def main() -> None:
    sys.exit(0 if run(parser().parse_args()) else 1)
