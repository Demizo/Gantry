"""Golden vectors built from a protocol's spec folder."""

import json
from pathlib import Path

import yaml

from .cddl import Cddl
from .encodings import cbor_edn

SOURCE = "vectors.yaml"
OUTPUT = "vectors.json"
NOTE = f"Generated from {SOURCE} by tools/protocol_spec. Do not edit."


def _checked(cddl: Cddl | None, rule: str | None, data: bytes, valid=True) -> str:
    """The hex of data, once it matches rule, or fails to when it is marked invalid."""
    if cddl is not None and rule is not None:
        try:
            cddl.validate(rule, data)
        except ValueError:
            if valid:
                raise
        else:
            if not valid:
                raise ValueError(f"is marked invalid but matches {rule}")
    return data.hex()


def _request(vector: dict) -> bytes:
    """A vector's request, written as notation or, as request_hex, raw."""
    if "request_hex" not in vector:
        return cbor_edn(vector["request"])
    if "request" in vector:
        raise ValueError("is given as both request and request_hex")
    return bytes.fromhex(vector["request_hex"])


def _build_vector(vector: dict, cddl: Cddl | None, settings: dict) -> dict:
    request_rule = settings.get("request_rule")
    built = {}
    for key, value in vector.items():
        try:
            if key == "setup":
                built[key] = [_checked(cddl, request_rule, cbor_edn(s)) for s in value]
            elif key in ("request", "request_hex"):
                valid = not vector.get("invalid_request", False)
                built["request"] = _checked(cddl, request_rule, _request(vector), valid)
            elif key == "response":
                rule = settings.get("response_rule")
                built[key] = _checked(cddl, rule, cbor_edn(value))
            else:
                built[key] = value
        except ValueError as error:
            raise ValueError(f"{key} {error}") from None
    for key in ("name", "request", "response"):
        if key not in built:
            raise ValueError(f"has no {key}")
    return built


def build(folder: Path) -> str:
    """The text of the vectors.json for folder; raises ValueError naming every bad vector."""
    settings = yaml.safe_load((folder / SOURCE).read_text(encoding="utf-8"))
    cddl = None
    if "cddl" in settings:
        cddl = Cddl((folder / settings["cddl"]).read_text(encoding="utf-8"))
    vectors = []
    errors = []
    names = set()
    for index, vector in enumerate(settings.get("vectors", [])):
        name = vector.get("name", f"#{index}")
        if name in names:
            errors.append(f"{name}: duplicate name")
        names.add(name)
        try:
            vectors.append(_build_vector(vector, cddl, settings))
        except ValueError as error:
            errors.append(f"{name}: {error}")
    if errors:
        raise ValueError("\n".join(errors))
    return json.dumps({"note": NOTE, "vectors": vectors}, indent=2) + "\n"
