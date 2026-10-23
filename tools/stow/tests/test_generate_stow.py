import json
import subprocess
import sys
from pathlib import Path

import yaml

SCRIPT = Path(__file__).resolve().parents[1] / "generate_stow.py"

SCHEMA = {
    "roles": ["Guest"],
    "categories": ["General"],
    "items": [
        {
            "name": "Level",
            "description": "An item",
            "categories": ["General"],
            "type": "INT",
            "storage": "EPHEMERAL",
            "permissions": [{"read": "ANY"}, {"write": "ANY"}],
            "default": 0,
            "constraints": [{"min": 0}, {"max": 9}],
        }
    ],
}


def _run(*args: str) -> str:
    result = subprocess.run(
        [sys.executable, str(SCRIPT), *args], capture_output=True, text=True, check=True
    )
    return result.stdout


def test_printed_hash_is_the_generated_stow_hash(tmp_path: Path):
    schema = tmp_path / "stow.yaml"
    schema.write_text(yaml.safe_dump(SCHEMA))
    printed = json.loads(_run("--yaml", str(schema), "--print-hash"))

    out = tmp_path / "out"
    for sub in ("inc", "src"):
        (out / sub / "stow").mkdir(parents=True)
    _run("--yaml", str(schema), "--output-dir", str(out))
    items = (out / "src" / "stow" / "generated_stow_items.c").read_text()
    assert len(printed) == 64
    assert f'"{printed}"' in items
