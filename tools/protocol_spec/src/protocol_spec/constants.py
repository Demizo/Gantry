"""Integer constants of a protocol folder's CDDL, named by its constants.yaml."""

import re
from dataclasses import dataclass
from pathlib import Path

import yaml

from .rendering import render

SOURCE = "constants.yaml"
_INTEGER = re.compile(r"^([A-Za-z][\w-]*)\s*=\s*(-?\d+)\s*(;.*)?$")
_RANGE = re.compile(r"^([A-Za-z][\w-]*)\s*=\s*(-?\d+)\s*\.\.\s*(-?\d+)\s*(;.*)?$")
_BOUNDS = {"min": 0, "max": 1}


@dataclass(frozen=True)
class Group:
    """An entry of constants.yaml: a CDDL prefix ending in ``-``, or one CDDL name, with the values it names."""

    cddl: str
    entry: dict
    members: tuple[tuple[str, int], ...]

    def suffix(self, name: str) -> str:
        """A member's CDDL name after the group's prefix, as a C or Python identifier part."""
        return name.removeprefix(self.cddl).upper().replace("-", "_")


@dataclass(frozen=True)
class Constants:
    """A protocol folder's CDDL file and its groups of named integers."""

    cddl: str
    groups: tuple[Group, ...]


def _parse(text: str) -> tuple[dict[str, int], dict[str, tuple[int, int]]]:
    integers = {}
    ranges = {}
    for line in text.splitlines():
        if match := _INTEGER.match(line.strip()):
            integers[match[1]] = int(match[2])
        elif match := _RANGE.match(line.strip()):
            ranges[match[1]] = (int(match[2]), int(match[3]))
    return integers, ranges


def _group(key: str, entry: dict, integers: dict, ranges: dict) -> Group:
    if key.endswith("-"):
        members = tuple((n, v) for n, v in integers.items() if n.startswith(key))
    elif key in integers:
        members = ((key, integers[key]),)
    elif key in ranges and entry.get("bound") in _BOUNDS:
        members = ((key, ranges[key][_BOUNDS[entry["bound"]]]),)
    else:
        members = ()
    if not members:
        raise ValueError(f"{SOURCE}: {key} names no CDDL integer")
    return Group(key, entry, members)


def constants(folder: Path) -> Constants:
    """Read a folder's constants.yaml and the CDDL values it names; raises ValueError for a CDDL integer it misses."""
    mapping = yaml.safe_load((folder / SOURCE).read_text(encoding="utf-8"))
    cddl = mapping["cddl"]
    integers, ranges = _parse((folder / cddl).read_text(encoding="utf-8"))
    groups = tuple(
        _group(key, entry or {}, integers, ranges)
        for key, entry in mapping["constants"].items()
    )
    named = {name for group in groups for name, _ in group.members}
    missed = sorted(set(integers) - named)
    if missed:
        raise ValueError(f"{cddl}: {SOURCE} doesn't name {', '.join(missed)}")
    return Constants(cddl, groups)


def to_header(constants: Constants) -> str:
    """A C header with a ``#define`` per single constant and an enum per group."""
    for group in constants.groups:
        values = sorted(value for _, value in group.members)
        if "c_count" in group.entry and values != list(range(len(values))):
            raise ValueError(
                f"{group.cddl} has a c_count, so its values must run from 0 without gaps"
            )
    return render("constants.h.j2", {}, cddl=constants.cddl, groups=constants.groups)
