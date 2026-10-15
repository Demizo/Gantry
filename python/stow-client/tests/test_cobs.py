from __future__ import annotations

import pytest

from stow_client.cobs import cobs_decode, cobs_encode

REQUEST = bytes.fromhex("820007")
REQUEST_FRAME = bytes.fromhex("0282020700")
RESPONSE = bytes.fromhex("820a07")
RESPONSE_FRAME = bytes.fromhex("04820a0700")


def test_encode_matches_firmware_framing():
    assert cobs_encode(REQUEST) == REQUEST_FRAME


def test_decode_matches_firmware_framing():
    assert cobs_decode(RESPONSE_FRAME[:-1]) == RESPONSE


@pytest.mark.parametrize(
    "payload",
    [b"", b"\x00", b"\x00\x00", bytes(range(1, 255)), bytes(range(256)) * 2],
)
def test_round_trip(payload):
    frame = cobs_encode(payload)
    assert frame.endswith(b"\x00")
    assert b"\x00" not in frame[:-1]
    assert cobs_decode(frame[:-1]) == payload
