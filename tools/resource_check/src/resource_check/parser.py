from pathlib import Path

import tree_sitter_c as tsc
from tree_sitter import Language, Node, Parser, Tree

from .ast_nodes import iter_nodes

_LANGUAGE = Language(tsc.language())
_CONTROL_FLOW = frozenset(
    {
        "compound_statement",
        "return_statement",
        "if_statement",
        "goto_statement",
        "while_statement",
        "for_statement",
        "do_statement",
        "switch_statement",
    }
)


def make_parser() -> Parser:
    return Parser(_LANGUAGE)


def parse_source(source: bytes, parser: Parser | None = None) -> Tree:
    return (parser or make_parser()).parse(source)


def parse_file(path: Path, parser: Parser | None = None) -> Tree:
    return parse_source(path.read_bytes(), parser)


def iter_function_definitions(root: Node):
    stack = [root]
    while stack:
        node = stack.pop()
        if node.type == "function_definition":
            yield node
            continue
        stack.extend(reversed(node.children))


def iter_unparsed_code(root: Node):
    """Yield the outermost ERROR nodes that swallowed control flow; any function inside is never analysed."""
    stack = [root]
    while stack:
        node = stack.pop()
        if node.type == "ERROR":
            if any(n.type in _CONTROL_FLOW for n in iter_nodes(node)):
                yield node
            continue
        stack.extend(reversed(node.children))
