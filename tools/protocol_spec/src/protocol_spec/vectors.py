"""Exchanges loaded and checked from a protocol folder's vectors.yaml."""

import re
import subprocess
from dataclasses import dataclass
from pathlib import Path

import yaml

from .cddl import Cddl
from .encodings import cbor_edn

SOURCE = "vectors.yaml"
SECTIONS = ("exchanges", "malformed")
_HEADER_KEYS = {"cddl", "sides", "values", *SECTIONS}
_VECTOR_KEYS = {"name", "meaning", "roles", "steps"}
_VALUE = re.compile(r"\$([A-Za-z_][A-Za-z0-9_]*)")


@dataclass(frozen=True)
class Step:
    """One message of an exchange; ``side`` indexes ``Vectors.sides``."""

    side: int
    data: bytes
    edn: str | None


@dataclass(frozen=True)
class Vector:
    name: str
    meaning: str
    roles: tuple[str, ...]
    steps: tuple[Step, ...]


@dataclass(frozen=True)
class Vectors:
    cddl: str
    sides: tuple[str, str]
    rules: tuple[str, str]
    exchanges: tuple[Vector, ...]
    malformed: tuple[Vector, ...]


class _Folder:
    def __init__(self, folder: Path):
        header = yaml.safe_load((folder / SOURCE).read_text(encoding="utf-8"))
        unknown = set(header) - _HEADER_KEYS
        if unknown:
            raise ValueError(f"{SOURCE}: unknown keys {sorted(unknown)}")
        sides = header.get("sides") or {}
        if len(sides) != 2:
            raise ValueError(f"{SOURCE}: sides must name exactly two sides")
        self.header = header
        self.sides = tuple(sides)
        self.rules = tuple(sides.values())
        self.cddl = Cddl((folder / header["cddl"]).read_text(encoding="utf-8"))
        for rule in self.rules:
            if not self.cddl.has_rule(rule):
                raise ValueError(f"{SOURCE}: the CDDL has no rule {rule!r}")
        self.errors: list[str] = []
        self.values: dict[str, str | None] = {}
        for name, command in (header.get("values") or {}).items():
            try:
                self.values[name] = _run_value(folder, name, command)
            except ValueError as error:
                self.errors.append(str(error))
                self.values[name] = None

    def step(self, written: dict, first_malformed: bool) -> Step:
        if not isinstance(written, dict) or len(written) != 1:
            raise ValueError("must be a map with exactly one key")
        ((key, text),) = written.items()
        side_name = key.removesuffix("_hex")
        if side_name not in self.sides:
            raise ValueError(f"names no side: {key}")
        side = self.sides.index(side_name)
        if key.endswith("_hex"):
            if not first_malformed:
                raise ValueError(
                    f"{key} is allowed only as a malformed vector's first step"
                )
            return Step(side, bytes.fromhex(str(text)), None)
        edn = _VALUE.sub(self._value, str(text)).strip()
        return Step(side, cbor_edn(edn), edn)

    def _value(self, match: re.Match) -> str:
        if match[1] not in self.values:
            raise ValueError(f"no value ${match[1]}")
        if self.values[match[1]] is None:
            raise ValueError(f"uses ${match[1]}, whose command failed")
        return self.values[match[1]]

    def check(self, step: Step, conforms: bool) -> None:
        rule = self.rules[step.side]
        try:
            self.cddl.validate(rule, step.data)
        except ValueError:
            if conforms:
                raise
        else:
            if not conforms:
                raise ValueError(
                    f"matches {rule}, but a malformed vector's first step must not"
                )


def _run_value(folder: Path, name: str, command: list) -> str:
    result = subprocess.run(
        [str(part) for part in command], cwd=folder, capture_output=True, text=True
    )
    if result.returncode != 0:
        reason = (result.stderr or result.stdout).strip().splitlines()
        raise ValueError(
            f"values.{name}: command exited with {result.returncode}"
            + (f": {reason[-1]}" if reason else "")
        )
    return result.stdout.strip()


def _vector(
    folder: _Folder, written: dict, malformed: bool, errors: list[str]
) -> Vector:
    name = written.get("name", "?")
    unknown = set(written) - _VECTOR_KEYS
    if unknown:
        errors.append(f"{name}: unknown keys {sorted(unknown)}")
    steps = []
    for index, step in enumerate(written.get("steps") or []):
        first_malformed = malformed and index == 0
        try:
            built = folder.step(step, first_malformed)
            folder.check(built, conforms=not first_malformed)
            steps.append(built)
        except ValueError as error:
            errors.append(f"{name}.steps[{index}]: {error}")
    if not steps:
        errors.append(f"{name}: has no steps")
    return Vector(
        name=name,
        meaning=written.get("meaning", ""),
        roles=tuple(written.get("roles") or ()),
        steps=tuple(steps),
    )


def _load(folder: Path) -> tuple[Vectors, _Folder]:
    spec = _Folder(folder)
    errors = spec.errors
    sections = {}
    names: set[str] = set()
    for section in SECTIONS:
        vectors = []
        for written in spec.header.get(section) or []:
            vector = _vector(spec, written, section == "malformed", errors)
            if vector.name in names:
                errors.append(f"{vector.name}: duplicate name")
            names.add(vector.name)
            vectors.append(vector)
        sections[section] = tuple(vectors)
    if errors:
        raise ValueError("\n".join(errors))
    vectors = Vectors(
        cddl=spec.header["cddl"], sides=spec.sides, rules=spec.rules, **sections
    )
    return vectors, spec


def load(folder: Path) -> Vectors:
    """Read, encode and validate a folder's vectors; raises ValueError listing every bad step."""
    return _load(folder)[0]


def uncovered(folder: Path) -> list[str]:
    """Load a folder's vectors and name each alternative of a side's rule that no step matches."""
    vectors, spec = _load(folder)
    steps = [
        step
        for vector in vectors.exchanges + vectors.malformed
        for step in vector.steps
    ]
    missing = []
    for side, rule in enumerate(spec.rules):
        for alternative in spec.cddl.alternatives(rule):
            if not any(
                step.side == side and spec.cddl.matches(alternative, step.data)
                for step in steps
            ):
                missing.append(f"{spec.sides[side]}: no step is a {alternative}")
    return missing
