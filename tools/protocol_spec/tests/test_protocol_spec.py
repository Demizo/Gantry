import json
import shutil
import subprocess
import sys
from pathlib import Path

import cbor_diag
import pytest

from protocol_spec import load
from protocol_spec.cddl import Cddl
from protocol_spec.encodings import cbor_edn
from protocol_spec.output import to_c, to_json
from protocol_spec.vectors import uncovered

REPO = Path(__file__).resolve().parents[3]

CDDL = """
question = ping / count
answer = pong / counted
ping = [0, tag]
pong = [1, tag, bstr .cborseq pong-items]
pong-items = [* uint]
count = [2, tag, + uint]
counted = [3, tag, uint]
tag = 0..23
"""

HEADER = """cddl: test.cddl
sides:
  asker: question
  answerer: answer
"""

COVERING = """exchanges:
  - name: ping
    meaning: Ping answers pong
    roles: [Guest]
    steps:
      - asker: '[0 /ping/, 1]'
      - answerer: '[1 /pong/, 1, <<3>>]'
  - name: count
    meaning: Count answers how many
    roles: []
    steps:
      - asker: '[2, 2, 7, 8]'
      - answerer: '[3, 2, 2]'
"""


def _folder(tmp_path: Path, body: str, header: str = HEADER) -> Path:
    (tmp_path / "test.cddl").write_text(CDDL)
    (tmp_path / "vectors.yaml").write_text(header + body)
    return tmp_path


def _load_error(tmp_path: Path, body: str, header: str = HEADER) -> str:
    with pytest.raises(ValueError) as error:
        load(_folder(tmp_path, body, header))
    return str(error.value)


@pytest.mark.parametrize(
    "text, hex_bytes",
    [
        ("[1, null, 21.5_2]", "8301f6fa41ac0000"),
        ("21.5", "f94d60"),
        ("{0: -100, 1: h'00ff'}", "a2003863014200ff"),
        ("<<{0: 1}, 2>>", "44a1000102"),
    ],
)
def test_edn_round_trips(text: str, hex_bytes: str):
    data = cbor_edn(text)
    assert data.hex() == hex_bytes
    assert cbor_edn(cbor_diag.cbor2diag(data)) == data


def test_edn_encodes_a_cbor_sequence():
    assert cbor_edn("[0, 15], 0").hex() == "82000f00"


def test_cddl_rejects_a_message_of_the_wrong_shape():
    cddl = Cddl(CDDL)
    cddl.validate("question", cbor_edn("[2, 1, 2]"))
    for text in ["[2, 1]", "[2, 24, 2]", "[1, 1, 2]", "[2, 1, -1]", "[2, 1, 2], 0"]:
        with pytest.raises(ValueError):
            cddl.validate("question", cbor_edn(text))


def test_cddl_checks_every_item_of_a_cbor_sequence():
    cddl = Cddl(CDDL)
    cddl.validate("pong", cbor_edn("[1, 1, <<>>]"))
    cddl.validate("pong", cbor_edn("[1, 1, <<1, 2>>]"))
    with pytest.raises(ValueError):
        cddl.validate("pong", cbor_edn('[1, 1, <<1, "two">>]'))


def test_cddl_lists_the_alternatives_of_a_choice():
    cddl = Cddl(CDDL)
    assert cddl.alternatives("question") == ["ping", "count"]
    assert cddl.alternatives("ping") == ["ping"]


def test_load_encodes_each_step_for_its_side(tmp_path: Path):
    vectors = load(_folder(tmp_path, COVERING))
    assert vectors.sides == ("asker", "answerer")
    assert vectors.rules == ("question", "answer")
    ping = vectors.exchanges[0]
    assert (ping.name, ping.meaning, ping.roles) == (
        "ping",
        "Ping answers pong",
        ("Guest",),
    )
    assert [(step.side, step.data.hex()) for step in ping.steps] == [
        (0, "820001"),
        (1, "8301014103"),
    ]
    assert ping.steps[0].edn == "[0 /ping/, 1]"
    assert vectors.malformed == ()


def test_a_step_must_match_its_sides_rule(tmp_path: Path):
    body = """exchanges:
  - name: crossed
    steps:
      - asker: '[1, 1, <<>>]'
      - answerer: '[0, 1]'
"""
    error = _load_error(tmp_path, body)
    assert "crossed.steps[0]: does not match question" in error
    assert "crossed.steps[1]: does not match answer" in error


def test_a_malformed_vector_breaks_its_sides_rule_only_in_its_first_step(
    tmp_path: Path,
):
    body = """malformed:
  - name: empty_count
    steps:
      - asker: '[2, 3]'
      - answerer: '[3, 3, 0]'
  - name: not_malformed
    steps:
      - asker: '[0, 4]'
      - answerer: '[3, 4, 0]'
  - name: bad_answer
    steps:
      - asker: '[2, 5]'
      - answerer: '[3, 5]'
"""
    error = _load_error(tmp_path, body)
    assert "empty_count" not in error
    assert "not_malformed.steps[0]: matches question" in error
    assert "bad_answer.steps[1]: does not match answer" in error


def test_raw_bytes_are_allowed_only_as_a_malformed_first_step(tmp_path: Path):
    body = """exchanges:
  - name: raw_exchange
    steps:
      - asker_hex: '820001'
malformed:
  - name: raw_first
    steps:
      - asker_hex: ff
      - answerer: '[3, 6, 0]'
  - name: raw_second
    steps:
      - asker: '[2, 6]'
      - answerer_hex: '830306'
"""
    error = _load_error(tmp_path, body)
    assert "raw_exchange.steps[0]: asker_hex is allowed only" in error
    assert "raw_second.steps[1]: answerer_hex is allowed only" in error
    assert "raw_first" not in error


def test_unknown_sides_and_duplicate_names_are_reported(tmp_path: Path):
    body = """exchanges:
  - name: twice
    steps:
      - client: '[0, 1]'
malformed:
  - name: twice
    steps:
      - asker: '[2, 1]'
"""
    error = _load_error(tmp_path, body)
    assert "twice.steps[0]: names no side: client" in error
    assert "twice: duplicate name" in error


def test_values_are_substituted_from_their_commands(tmp_path: Path):
    command = json.dumps([sys.executable, "-c", "print(3)"])
    body = """exchanges:
  - name: counted
    steps:
      - asker: '[2, 1, $three]'
      - answerer: '[3, 1, 1]'
"""
    vectors = load(_folder(tmp_path, body, HEADER + f"values:\n  three: {command}\n"))
    assert vectors.exchanges[0].steps[0].data == cbor_edn("[2, 1, 3]")
    assert vectors.exchanges[0].steps[0].edn == "[2, 1, 3]"


def test_a_failing_value_command_is_reported_with_the_bad_steps(tmp_path: Path):
    command = json.dumps([sys.executable, "-c", "raise SystemExit(3)"])
    body = """exchanges:
  - name: uses_it
    steps:
      - asker: '[2, 1, $broken]'
  - name: crossed
    steps:
      - asker: '[1, 1, <<>>]'
"""
    error = _load_error(tmp_path, body, HEADER + f"values:\n  broken: {command}\n")
    assert "values.broken: command exited with 3" in error
    assert "uses_it.steps[0]: uses $broken, whose command failed" in error
    assert "crossed.steps[0]: does not match question" in error


def test_an_unknown_value_is_reported(tmp_path: Path):
    body = """exchanges:
  - name: unknown
    steps:
      - asker: '[2, 1, $missing]'
"""
    assert "unknown.steps[0]: no value $missing" in _load_error(tmp_path, body)


def test_coverage_names_each_alternative_without_a_step(tmp_path: Path):
    assert uncovered(_folder(tmp_path, COVERING)) == []
    body = COVERING.split("  - name: count")[0]
    assert uncovered(_folder(tmp_path, body)) == [
        "asker: no step is a count",
        "answerer: no step is a counted",
    ]


def test_json_export_keeps_the_yaml_shape(tmp_path: Path):
    body = (
        COVERING
        + """malformed:
  - name: raw
    steps:
      - asker_hex: ff
      - answerer: '[3, 9, 0]'
"""
    )
    export = json.loads(to_json(load(_folder(tmp_path, body))))
    assert export["cddl"] == "test.cddl"
    assert export["sides"] == {"asker": "question", "answerer": "answer"}
    assert export["exchanges"][0] == {
        "name": "ping",
        "meaning": "Ping answers pong",
        "roles": ["Guest"],
        "steps": [
            {"asker": {"hex": "820001", "edn": "[0 /ping/, 1]"}},
            {"answerer": {"hex": "8301014103", "edn": "[1 /pong/, 1, <<3>>]"}},
        ],
    }
    assert export["malformed"][0]["steps"][0] == {"asker": {"hex": "ff", "edn": None}}


HOST_TEST = r"""
#include <protocol_vectors.h>
#include <stdio.h>

PROTOCOL_VECTORS_DECLARE(toy);

int main(void)
{
    const struct protocol_vector* ping = &toy_exchanges[0];
    const struct protocol_step* pong = &ping->steps[1];
    printf("%s|%s|%zu|%zu|%s|%s|%d|%u|%zu|%02x|%s\n", toy_sides[0], toy_sides[1],
           toy_exchanges_count, toy_malformed_count, ping->name, ping->roles[0],
           ping->roles[1] == NULL, pong->side, pong->len, pong->data[4], pong->edn);
    return 0;
}
"""

COMPILER = next(
    (shutil.which(cc) for cc in ("cc", "gcc", "clang") if shutil.which(cc)), None
)


@pytest.mark.skipif(COMPILER is None, reason="no host C compiler")
def test_c_output_compiles_and_holds_the_vectors(tmp_path: Path):
    (tmp_path / "vectors.c").write_text(to_c(load(_folder(tmp_path, COVERING)), "toy"))
    (tmp_path / "main.c").write_text(HOST_TEST)
    program = tmp_path / "host_test"
    subprocess.run(
        [
            COMPILER,
            "-std=c11",
            "-Wall",
            "-Werror",
            f"-I{REPO / 'tests' / 'common'}",
            str(tmp_path / "main.c"),
            str(tmp_path / "vectors.c"),
            "-o",
            str(program),
        ],
        check=True,
    )
    output = subprocess.run([str(program)], capture_output=True, text=True, check=True)
    assert output.stdout.strip() == (
        "asker|answerer|2|0|ping|Guest|1|1|5|03|[1 /pong/, 1, <<3>>]"
    )


@pytest.mark.parametrize(
    "folder",
    sorted(p.parent for p in REPO.glob("protocols/*/vectors.yaml")),
    ids=lambda p: p.name,
)
def test_repo_vectors_load_and_cover_every_message(folder: Path):
    assert uncovered(folder) == []
