"""Steps that turn a message, as written in a vectors file, into its bytes."""

import cbor2
import cbor_diag


def cbor_edn(text: str) -> bytes:
    """Encode CBOR diagnostic notation, which may hold a CBOR sequence such as ``[0, 1], 0``."""
    try:
        wrapped = cbor_diag.diag2cbor(f"<<\n{text}\n>>")
    except ValueError as error:
        raise ValueError(f"invalid diagnostic notation: {error}") from None
    return cbor2.loads(wrapped)
