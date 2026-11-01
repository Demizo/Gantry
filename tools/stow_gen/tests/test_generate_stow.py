import json
import subprocess
from pathlib import Path

import yaml

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


def _stow_gen(*args: str) -> subprocess.CompletedProcess:
    return subprocess.run(["stow-gen", *args], capture_output=True, text=True)


def _schema(tmp_path: Path) -> Path:
    schema = tmp_path / "stow.yaml"
    schema.write_text(yaml.safe_dump(SCHEMA))
    return schema


def _items(schema: Path, out: Path) -> None:
    for sub in ("inc", "src"):
        (out / sub / "stow").mkdir(parents=True)
    _stow_gen(
        "items", "--yaml", str(schema), "--output-dir", str(out)
    ).check_returncode()


def test_printed_hash_is_the_generated_stow_hash(tmp_path: Path):
    schema = _schema(tmp_path)
    hashed = _stow_gen("hash", "--yaml", str(schema))
    hashed.check_returncode()
    printed = json.loads(hashed.stdout)

    out = tmp_path / "out"
    _items(schema, out)
    items = (out / "src" / "stow" / "generated_stow_items.c").read_text()
    assert len(printed) == 64
    assert f'"{printed}"' in items


def test_the_depth_of_a_schema_without_structs_is_zero(tmp_path: Path):
    out = tmp_path / "out"
    _items(_schema(tmp_path), out)
    assert (out / "generated_stow_depth").read_text() == "0\n"


def _peer_client(schema: Path, out: Path, item: str) -> subprocess.CompletedProcess:
    args = ["--yaml", str(schema), "--prefix", "peer", "--output-dir", str(out)]
    return _stow_gen("client", *args, "--item", item)


def test_client_binds_the_peer_items_under_the_prefix(tmp_path: Path):
    out = tmp_path / "out"
    _peer_client(_schema(tmp_path), out, "Level").check_returncode()
    header = (out / "inc" / "stow" / "generated_peer_stow_client.h").read_text()
    assert "PEER_ID_LEVEL" in header
    assert (out / "generated_peer_stow_depth").read_text() == "0\n"


def test_a_schema_error_fails_with_its_reason(tmp_path: Path):
    result = _peer_client(_schema(tmp_path), tmp_path / "out", "Missing")
    assert result.returncode == 1
    assert "no item(s) ['Missing']" in result.stderr
