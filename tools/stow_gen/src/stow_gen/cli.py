"""Command line that generates a Stow schema's C code, a peer's client bindings, or a schema's StowHash."""

import argparse
import json
import sys
from pathlib import Path

import yaml

from .client_generator import generate_client
from .generator import generate, stow_hash
from .merger import compose
from .validator import validate


def _schema(args: argparse.Namespace, board_yaml: Path | None = None) -> dict:
    """The validated schema composed from --yaml, its fragments and a board fragment."""
    data, _ = compose(args.fragment, args.yaml, board_yaml)
    validate(data)
    return data


def _app_schema(args: argparse.Namespace) -> tuple[dict, str, Path]:
    """The app's schema, its merged YAML, and the directory of its VERSION file."""
    data = _schema(args, args.board_yaml)
    merged_yaml = yaml.safe_dump(data, sort_keys=False)
    version_dir = args.app_dir if args.app_dir is not None else args.yaml.parent
    return data, merged_yaml, version_dir


def _items(args: argparse.Namespace) -> None:
    data, merged_yaml, version_dir = _app_schema(args)
    args.output_dir.mkdir(parents=True, exist_ok=True)
    merged_path = args.output_dir / "merged.stow.yaml"
    merged_path.write_text(merged_yaml)
    print(f"Generated {merged_path}")
    generate(data, args.output_dir, merged_yaml.encode(), version_dir)


def _hash(args: argparse.Namespace) -> None:
    _, merged_yaml, version_dir = _app_schema(args)
    print(json.dumps(stow_hash(merged_yaml.encode(), version_dir)))


def _client(args: argparse.Namespace) -> None:
    data = _schema(args)
    local = yaml.safe_load(args.local_yaml.read_text()) if args.local_yaml else None
    generate_client(data, args.output_dir, args.prefix, args.item, local)


def _parser() -> argparse.ArgumentParser:
    schema = argparse.ArgumentParser(add_help=False)
    schema.add_argument(
        "--yaml", required=True, type=Path, help="Path to the stow.yaml"
    )
    schema.add_argument(
        "--fragment",
        action="append",
        default=[],
        type=Path,
        help="Additional stow fragment YAML, lowest precedence, repeatable, in registration order",
    )
    app = argparse.ArgumentParser(add_help=False, parents=[schema])
    app.add_argument(
        "--board-yaml",
        type=Path,
        default=None,
        help="Board-specific stow fragment YAML, highest precedence",
    )
    app.add_argument(
        "--app-dir",
        type=Path,
        default=None,
        help="App directory to look for a VERSION file in (defaults to --yaml's directory)",
    )
    output = argparse.ArgumentParser(add_help=False)
    output.add_argument(
        "--output-dir", required=True, type=Path, help="Generated output directory"
    )

    parser = argparse.ArgumentParser(prog="stow-gen", description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    items = commands.add_parser(
        "items",
        parents=[app, output],
        help="generate the app's Stow items, enums and structs",
    )
    items.set_defaults(run=_items)
    hashing = commands.add_parser(
        "hash",
        parents=[app],
        help="print the StowHash as a CBOR diagnostic notation string",
    )
    hashing.set_defaults(run=_hash)
    client = commands.add_parser(
        "client",
        parents=[schema, output],
        help="generate Stow client bindings for a peer's schema",
    )
    client.add_argument(
        "--prefix",
        required=True,
        help="Lowercase prefix for every generated name, unique per peer schema",
    )
    client.add_argument(
        "--item",
        action="append",
        default=[],
        help="Name of a peer item the client uses, repeatable; every schema item when omitted",
    )
    client.add_argument(
        "--local-yaml",
        type=Path,
        default=None,
        help="This build's merged local stow.yaml; identical enums and structs reuse the local types",
    )
    client.set_defaults(run=_client)
    return parser


def main() -> None:
    args = _parser().parse_args()
    try:
        args.run(args)
    except (ValueError, FileNotFoundError) as error:
        print(f"Error: {error}", file=sys.stderr)
        sys.exit(1)
