"""Validation of encoded messages against a CDDL spec, using zcbor."""

import contextlib
import io

import cbor2
from zcbor.zcbor.zcbor import PRELUDE_PATH, CddlValidationError, DataTranslator

MAX_REPEATS = 1024


def decode_sequence(data: bytes) -> list:
    """Decode a CBOR sequence into its items."""
    stream = io.BytesIO(data)
    items = []
    while stream.tell() < len(data):
        items.append(cbor2.load(stream))
    return items


class _Translator(DataTranslator):
    """zcbor's validator, made to check every item of a ``.cborseq`` and the sign of a ``uint``.

    RFC 8610 controls ``.cborseq`` with an array of the sequence's items, where zcbor decodes only
    the first item against the controlling type.
    """

    def set_cbor(self, cbor, cborseq):
        super().set_cbor(cbor, cborseq=False)
        cbor.is_sequence = cborseq

    def decode_str(self, cbor_str):
        if getattr(self, "is_sequence", False):
            return self.decode_obj(decode_sequence(cbor_str))
        return super().decode_str(cbor_str)

    def _check_value(self, obj):
        super()._check_value(obj)
        if self.type == "UINT":
            self._decode_assert(obj >= 0, f"{obj} is not a uint")


class Cddl:
    """A CDDL spec whose rules validate encoded messages.

    Float widths aren't checked, since decoding hides them, and map entries must be in CDDL order.
    """

    def __init__(self, text: str):
        with contextlib.redirect_stdout(io.StringIO()):
            translator = _Translator.from_cddl(
                f"{PRELUDE_PATH.read_text()}\n{text}", MAX_REPEATS
            )
        self._rules = translator.my_types

    def validate(self, rule: str, data: bytes) -> None:
        """Raise ValueError unless data is a single CBOR item that matches rule."""
        if rule not in self._rules:
            raise ValueError(f"the CDDL has no rule {rule!r}")
        try:
            items = decode_sequence(data)
        except (cbor2.CBORDecodeError, EOFError) as error:
            raise ValueError(f"not valid CBOR: {error}") from None
        if len(items) != 1:
            raise ValueError(f"expected one CBOR item, found {len(items)}")
        try:
            with contextlib.redirect_stdout(io.StringIO()):
                self._rules[rule].validate_obj(items[0])
        except (CddlValidationError, cbor2.CBORDecodeError, EOFError) as error:
            reason = str(error).splitlines()[0] if str(error) else type(error).__name__
            raise ValueError(f"does not match {rule}: {reason[:200]}") from None
