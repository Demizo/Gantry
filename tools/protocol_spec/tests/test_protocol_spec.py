import json
from pathlib import Path

import cbor_diag
import pytest

from protocol_spec.cddl import Cddl
from protocol_spec.cli import run
from protocol_spec.encodings import cbor_edn
from protocol_spec.spec import build

REPO = Path(__file__).resolve().parents[3]

CDDL = """
message = ping / pong
ping = [0, tag, + uint]
pong = [1, tag, bstr .cborseq pong-items]
pong-items = [* uint]
tag = 0..23
"""


def _write_spec(folder: Path, vectors: str) -> Path:
    (folder / "test.cddl").write_text(CDDL)
    (folder / "vectors.yaml").write_text(
        "cddl: test.cddl\nrequest_rule: ping\nresponse_rule: pong\nvectors:\n" + vectors
    )
    return folder


def _vector(name: str, request: str, response: str, extra: str = "") -> str:
    return f"  - name: {name}\n    request: '{request}'\n    response: '{response}'\n{extra}"


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
    cddl.validate("message", cbor_edn("[0, 1, 2]"))
    for text in ["[0, 1]", "[0, 24, 2]", "[2, 1, 2]", "[0, 1, -1]", "[0, 1, 2], 0"]:
        with pytest.raises(ValueError):
            cddl.validate("message", cbor_edn(text))


def test_cddl_checks_every_item_of_a_cbor_sequence():
    cddl = Cddl(CDDL)
    cddl.validate("pong", cbor_edn("[1, 1, <<>>]"))
    cddl.validate("pong", cbor_edn("[1, 1, <<1, 2>>]"))
    with pytest.raises(ValueError):
        cddl.validate("pong", cbor_edn('[1, 1, <<1, "two">>]'))


def test_build_writes_hex_and_keeps_other_fields(tmp_path: Path):
    _write_spec(
        tmp_path,
        _vector(
            "one", "[0, 1, 2]", "[1, 1, <<3>>]", "    meaning: Ping answers pong\n"
        ),
    )
    vectors = json.loads(build(tmp_path))["vectors"]
    assert vectors == [
        {
            "name": "one",
            "request": "83000102",
            "response": "8301014103",
            "meaning": "Ping answers pong",
        }
    ]


def test_build_names_each_vector_that_breaks_the_cddl(tmp_path: Path):
    _write_spec(
        tmp_path,
        _vector("bad_response", "[0, 1, 2]", "[1, 1, 3]")
        + _vector(
            "wrongly_invalid",
            "[0, 1, 2]",
            "[1, 1, <<>>]",
            "    invalid_request: true\n",
        ),
    )
    with pytest.raises(ValueError) as error:
        build(tmp_path)
    assert "bad_response: response" in str(error.value)
    assert "wrongly_invalid: request is marked invalid" in str(error.value)


def test_build_rejects_a_message_given_twice(tmp_path: Path):
    extra = "    request_hex: '830001'\n"
    _write_spec(tmp_path, _vector("twice", "[0, 1, 2]", "[1, 1, <<>>]", extra))
    with pytest.raises(ValueError, match="both request and request_hex"):
        build(tmp_path)


def test_check_detects_drift(tmp_path: Path):
    _write_spec(tmp_path, _vector("one", "[0, 1, 2]", "[1, 1, <<>>]"))
    assert not run("check", [tmp_path])
    assert run("generate", [tmp_path])
    assert run("check", [tmp_path])

    spec = tmp_path / "vectors.yaml"
    spec.write_text(spec.read_text().replace("[0, 1, 2]", "[0, 1, 3]"))
    assert not run("check", [tmp_path])


@pytest.mark.parametrize(
    "folder",
    sorted(p.parent for p in REPO.glob("protocols/*/vectors.yaml")),
    ids=lambda p: p.name,
)
def test_repo_vectors_are_up_to_date(folder: Path):
    assert run("check", [folder])
