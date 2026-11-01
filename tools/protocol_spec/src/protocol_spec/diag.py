"""One message, given as bytes, written in the notation of a protocol folder's vectors."""

import io

import cbor2
import cbor_diag

from .vectors import Folder

_BYTES, _ARRAY, _MAP, _TAG = 2, 4, 5, 6


def _head(data: bytes, offset: int) -> tuple[int, int | None, int]:
    """The major type, the argument and the offset after the head of the item at offset.

    The argument is None for a head that is indefinite or longer than it needs to be, which only cbor2diag writes.
    """
    major, info = data[offset] >> 5, data[offset] & 0x1F
    if info < 24:
        return major, info, offset + 1
    if info > 27:
        return major, None, offset + 1
    size = 1 << (info - 24)
    argument = int.from_bytes(data[offset + 1 : offset + 1 + size])
    shortest = 24 + (argument > 0xFF) + (argument > 0xFFFF) + (argument > 0xFFFFFFFF)
    if argument < 24 or info != shortest:
        return major, None, offset + 1 + size
    return major, argument, offset + 1 + size


def _end(data: bytes, offset: int) -> int:
    stream = io.BytesIO(data)
    stream.seek(offset)
    cbor2.load(stream)
    return stream.tell()


class _Writer:
    """Writes CBOR items, nesting into arrays, maps, tags and the byte strings that hold a CBOR sequence."""

    def __init__(self, values: dict[bytes, str], sequences: list[bytes]):
        self.values = values
        self.sequences = sequences

    def item(self, data: bytes, start: int) -> tuple[str, int]:
        """The notation of the item at start, and the offset after it."""
        end = _end(data, start)
        raw = data[start:end]
        if raw in self.values:
            return f"${self.values[raw]}", end
        major, argument, body = _head(data, start)
        if argument is None:
            return cbor_diag.cbor2diag(raw), end
        if major == _ARRAY:
            return f"[{', '.join(self.items(data, body, argument))}]", end
        if major == _MAP:
            written = self.items(data, body, 2 * argument)
            pairs = (
                f"{key}: {value}"
                for key, value in zip(written[::2], written[1::2], strict=True)
            )
            return f"{{{', '.join(pairs)}}}", end
        if major == _TAG:
            return f"{argument}({self.item(data, body)[0]})", end
        if major == _BYTES:
            content = data[body:end]
            if content and content in self.sequences:
                lines = ",\n".join(self.every(content)).splitlines()
                return "<<\n" + "\n".join(f"  {line}" for line in lines) + "\n>>", end
            return f"h'{content.hex()}'", end
        return cbor_diag.cbor2diag(raw), end

    def items(self, data: bytes, offset: int, count: int) -> list[str]:
        written = []
        for _ in range(count):
            text, offset = self.item(data, offset)
            written.append(text)
        return written

    def every(self, data: bytes) -> list[str]:
        """The notation of each item of a CBOR sequence."""
        written = []
        offset = 0
        while offset < len(data):
            text, offset = self.item(data, offset)
            written.append(text)
        return written


def diag(spec: Folder, data: bytes) -> str:
    """Write a message, or a CBOR sequence of them, so that it encodes back to data.

    A byte string the folder's CDDL controls with ``.cborseq`` is written as ``<< >>``, one item a line, and an item
    equal to one of the folder's values as ``$name``.
    """
    values = {
        spec.encode(f"${name}"): name
        for name, text in spec.values.items()
        if text is not None
    }
    sequences = next(
        (
            found
            for rule in spec.rules
            if (found := spec.cddl.sequences(rule, data)) is not None
        ),
        [],
    )
    try:
        written = _Writer(values, sequences).every(data)
    except (ValueError, EOFError, IndexError) as error:
        raise ValueError(f"not CBOR: {error}") from None
    if not written:
        raise ValueError("not CBOR: no bytes")
    return ", ".join(written)
