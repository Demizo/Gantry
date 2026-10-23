"""Renderings of loaded vectors for consumers outside Python."""

import json

import jinja2

from .vectors import SECTIONS, SOURCE, Step, Vectors


def _step(vectors: Vectors, step: Step) -> dict:
    message = {"hex": step.data.hex(), "edn": step.edn}
    return {vectors.sides[step.side]: message}


def to_json(vectors: Vectors) -> str:
    """The vectors in the shape of vectors.yaml, with each message as hex and EDN."""
    export = {
        "cddl": vectors.cddl,
        "sides": dict(zip(vectors.sides, vectors.rules, strict=True)),
    }
    for section in SECTIONS:
        export[section] = [
            {
                "name": vector.name,
                "meaning": vector.meaning,
                "roles": list(vector.roles),
                "steps": [_step(vectors, step) for step in vector.steps],
            }
            for vector in getattr(vectors, section)
        ]
    return json.dumps(export, indent=2) + "\n"


def _c_string(value: str | None) -> str:
    return "NULL" if value is None else json.dumps(value)


def _c_bytes(data: bytes) -> str:
    return ", ".join(f"0x{byte:02X}" for byte in data)


def to_c(vectors: Vectors, prefix: str) -> str:
    """C data for ``protocol_vectors.h``: ``<prefix>_sides``, ``_exchanges`` and ``_malformed``."""
    environment = jinja2.Environment(
        loader=jinja2.PackageLoader("protocol_spec"),
        trim_blocks=True,
        lstrip_blocks=True,
        keep_trailing_newline=True,
        undefined=jinja2.StrictUndefined,
    )
    environment.filters["c_string"] = _c_string
    environment.filters["c_bytes"] = _c_bytes
    return environment.get_template("vectors.c.j2").render(
        source=SOURCE,
        prefix=prefix,
        sides=vectors.sides,
        sections={section: getattr(vectors, section) for section in SECTIONS},
    )
