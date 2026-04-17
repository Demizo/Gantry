import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from generator import generate


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Generate datastore from datastore.yaml"
    )
    parser.add_argument("--yaml", required=True, type=Path, help="Path to datastore.yaml")
    parser.add_argument(
        "--output-dir",
        required=True,
        type=Path,
        help="App directory root (contains inc/ and src/)",
    )
    args = parser.parse_args()

    try:
        generate(args.yaml, args.output_dir)
    except (ValueError, FileNotFoundError) as e:
        print(f"Error: {e}", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
