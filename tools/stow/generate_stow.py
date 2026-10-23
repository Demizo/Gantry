import argparse
import json
import sys
from pathlib import Path

import yaml

sys.path.insert(0, str(Path(__file__).parent))
from generator import generate, stow_hash
from merger import compose
from validator import validate


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Generate stow from stow.yaml, composed from any registered fragments"
    )
    parser.add_argument(
        "--yaml", required=True, type=Path, help="Path to the app's stow.yaml"
    )
    parser.add_argument(
        "--fragment",
        action="append",
        default=[],
        type=Path,
        help="Additional stow fragment YAML, lowest precedence, repeatable, in registration order",
    )
    parser.add_argument(
        "--board-yaml",
        type=Path,
        default=None,
        help="Board-specific stow fragment YAML, highest precedence",
    )
    parser.add_argument(
        "--app-dir",
        type=Path,
        default=None,
        help="App directory to look for a VERSION file in (defaults to --yaml's directory)",
    )
    output = parser.add_mutually_exclusive_group(required=True)
    output.add_argument(
        "--output-dir",
        type=Path,
        help="Generated output directory",
    )
    output.add_argument(
        "--print-hash",
        action="store_true",
        help="Print the StowHash as a CBOR diagnostic notation string instead of generating",
    )
    args = parser.parse_args()

    try:
        data, provenance = compose(args.fragment, args.yaml, args.board_yaml)
        validate(data)
        merged_yaml = yaml.safe_dump(data, sort_keys=False)
        version_dir = args.app_dir if args.app_dir is not None else args.yaml.parent
        if args.print_hash:
            print(json.dumps(stow_hash(merged_yaml.encode(), version_dir)))
            return

        args.output_dir.mkdir(parents=True, exist_ok=True)
        merged_path = args.output_dir / "merged.stow.yaml"
        merged_path.write_text(merged_yaml)
        print(f"Generated {merged_path}")
        generate(data, args.output_dir, merged_yaml.encode(), version_dir)
    except (ValueError, FileNotFoundError) as e:
        print(f"Error: {e}", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
