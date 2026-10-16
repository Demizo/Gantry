import re

import pytest
import yaml

from validator import validate

SCHEMA = """
enums:
  Mode:
    description: "Mode"
    values:
      - name: "OFF"
        value: 0
structs:
  - name: Pair
    description: "Pair"
    fields:
      - name: Count
        type: INT
        constraints:
          - min: 0
          - max: 1
roles:
  - Guest
categories:
  - General
items:
  - name: Level
    description: "Level"
    categories: [General]
    type: INT
    storage: EPHEMERAL
    permissions:
      - read: ANY
      - write: ANY
    default: 0
    constraints:
      - min: 0
      - max: 1
"""


def test_quoted_names_are_accepted():
    validate(yaml.safe_load(SCHEMA))


@pytest.mark.parametrize(
    "written, unquoted, where",
    [
        ('- name: "OFF"', "- name: OFF", "enum 'Mode' value[0]"),
        ("- name: Pair", "- name: yes", "struct 'True'"),
        ("- name: Count", "- name: 1", "struct 'Pair' field[0]"),
        ("  - Guest", "  - on", "roles[0]"),
        ("- name: Level", "- name: null", "item 'None'"),
    ],
)
def test_names_yaml_reads_as_non_strings_are_rejected(written, unquoted, where):
    with pytest.raises(ValueError, match=re.escape(f"{where}: name must be")):
        validate(yaml.safe_load(SCHEMA.replace(written, unquoted)))
