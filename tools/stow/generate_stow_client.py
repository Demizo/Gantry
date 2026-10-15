import argparse
import sys
from pathlib import Path

import yaml

sys.path.insert(0, str(Path(__file__).parent))
from client_generator import generate_client
from merger import compose
from validator import validate


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Generate Stow client bindings for a peer's Stow schema"
    )
    parser.add_argument(
        "--yaml", required=True, type=Path, help="Path to the peer's stow.yaml"
    )
    parser.add_argument(
        "--fragment",
        action="append",
        default=[],
        type=Path,
        help="Peer stow fragment YAML, lowest precedence, repeatable, in registration order",
    )
    parser.add_argument(
        "--prefix",
        required=True,
        help="Lowercase prefix for every generated name, unique per peer schema",
    )
    parser.add_argument(
        "--item",
        action="append",
        default=[],
        help="Name of a peer item the client uses, repeatable; every schema item when omitted",
    )
    parser.add_argument(
        "--local-yaml",
        type=Path,
        default=None,
        help="This build's merged local stow.yaml; identical enums and structs reuse the local types",
    )
    parser.add_argument(
        "--output-dir", required=True, type=Path, help="Generated output directory"
    )
    args = parser.parse_args()

    try:
        data, _ = compose(args.fragment, args.yaml, None)
        validate(data)
        local = yaml.safe_load(args.local_yaml.read_text()) if args.local_yaml else None
        generate_client(data, args.output_dir, args.prefix, args.item, local)
    except (ValueError, FileNotFoundError) as e:
        print(f"Error: {e}", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
