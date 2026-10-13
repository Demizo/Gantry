from pathlib import Path

import tree_sitter_c as tsc
from tree_sitter import Language, Node, Parser, Tree

_LANGUAGE = Language(tsc.language())


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
