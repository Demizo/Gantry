"""COBS framing: each frame is COBS-encoded and ends with a 0x00 delimiter."""

from __future__ import annotations


def cobs_encode(data: bytes) -> bytes:
    """Encode *data* using COBS and append a 0x00 frame delimiter."""
    out = bytearray()
    code_idx = 0
    out.append(0)
    code = 1
    for byte in data:
        if byte == 0:
            out[code_idx] = code
            code_idx = len(out)
            out.append(0)
            code = 1
        else:
            out.append(byte)
            code += 1
            if code == 0xFF:
                out[code_idx] = 0xFF
                code_idx = len(out)
                out.append(0)
                code = 1
    out[code_idx] = code
    out.append(0)
    return bytes(out)


def cobs_decode(data: bytes) -> bytes:
    """Decode one COBS frame, without its trailing 0x00."""
    if not data:
        return b""
    out = bytearray()
    idx = 0
    while idx < len(data):
        code = data[idx]
        if code == 0:
            raise ValueError("Unexpected zero byte in COBS data")
        for i in range(1, code):
            if idx + i >= len(data):
                raise ValueError("COBS data truncated")
            out.append(data[idx + i])
        idx += code
        if code < 0xFF and idx < len(data):
            out.append(0)
    return bytes(out)
