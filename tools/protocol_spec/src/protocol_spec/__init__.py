"""Golden vectors and wire constants of a protocol folder."""

from .constants import Constants, Group, constants

_VECTORS = ("Step", "Vector", "Vectors", "load")
__all__ = ["Constants", "Group", "constants", *_VECTORS]


def __getattr__(name: str):
    # The vectors need the vectors extra; the constants need only the base install
    if name in _VECTORS:
        from . import vectors

        return getattr(vectors, name)
    raise AttributeError(f"module {__name__!r} has no attribute {name!r}")
