from __future__ import annotations

import json
from pathlib import Path

import cbor2
import pytest

from stow_client.descriptions import ItemType, parse_description
from stow_client.messages import DescribeResponse, HelloResponse, Response, decode

VECTORS_FILE = (
    Path(__file__).resolve().parents[3] / "protocols" / "stow" / "vectors.json"
)
VECTORS = {
    vector["name"]: vector for vector in json.loads(VECTORS_FILE.read_text())["vectors"]
}


def test_vectors_exist():
    assert VECTORS


@pytest.mark.parametrize("vector", VECTORS.values(), ids=VECTORS)
def test_client_round_trips_the_vector_request(vector: dict):
    if not vector.get("client_sends", True):
        pytest.skip("the client never sends this request")
    for hex_request in [*vector.get("setup", []), vector["request"]]:
        request = bytes.fromhex(hex_request)
        assert decode(request).encode() == request


@pytest.mark.parametrize("vector", VECTORS.values(), ids=VECTORS)
def test_client_decodes_the_vector_response(vector: dict):
    response_bytes = bytes.fromhex(vector["response"])
    response = decode(response_bytes)
    assert response.to_list() == cbor2.loads(response_bytes)
    assert response.encode() == response_bytes, "client encodes differently"

    if vector.get("client_sends", True):
        request = decode(bytes.fromhex(vector["request"]))
        assert isinstance(response, Response)
        assert response.tag == request.tag


def _response(name: str):
    return decode(bytes.fromhex(VECTORS[name]["response"]))


DESCRIBE_VECTORS = [
    name for name in VECTORS if isinstance(_response(name), DescribeResponse)
]


def test_describe_vectors_exist():
    assert DESCRIBE_VECTORS


@pytest.mark.parametrize("name", DESCRIBE_VECTORS)
def test_client_parses_the_vector_descriptions(name: str):
    info = _response("hello")
    assert isinstance(info, HelloResponse)
    response = _response(name)

    items = parse_description(response.chunk, info.role_names)
    assert items
    for item in items:
        assert isinstance(item.type, ItemType)
        assert item.constraints is not None
        assert set(item.read_perm) <= set(info.role_names)
        assert set(item.write_perm) <= set(info.role_names)
