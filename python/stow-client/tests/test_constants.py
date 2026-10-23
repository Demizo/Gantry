"""The client's protocol constants match stow.cddl, named through protocols/stow/constants.yaml."""

import importlib
from enum import IntEnum
from pathlib import Path

import pytest

from protocol_spec import Group, constants
from stow_client import Message

GROUPS = [
    group
    for group in constants(
        Path(__file__).resolve().parents[3] / "protocols" / "stow"
    ).groups
    if "python" in group.entry
]


def _python(path: str):
    module, name = path.rsplit(".", 1)
    return getattr(importlib.import_module(module), name)


def _message_classes() -> dict[int, list[type]]:
    found: dict[int, list[type]] = {}
    pending = [Message]
    while pending:
        for cls in pending.pop().__subclasses__():
            pending.append(cls)
            if "MSG_ID" in vars(cls):
                found.setdefault(cls.MSG_ID, []).append(cls)
    return found


def _class_names(suffix: str) -> set[str]:
    name = "".join(part.capitalize() for part in suffix.split("_"))
    return {name, f"{name}Request"}


@pytest.mark.parametrize("group", GROUPS, ids=lambda group: group.cddl)
def test_python_constants_match_the_cddl(group: Group):
    target = _python(group.entry["python"])
    expected = {group.suffix(name): value for name, value in group.members}
    if target is Message:
        classes = _message_classes()
        assert set(classes) == set(expected.values())
        for suffix, value in expected.items():
            (cls,) = classes[value]
            assert cls.__name__ in _class_names(suffix), suffix
    elif isinstance(target, type) and issubclass(target, IntEnum):
        assert {member.name: member.value for member in target} == expected
    else:
        assert [target] == list(expected.values())
