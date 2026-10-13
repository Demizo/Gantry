import re
from collections.abc import Iterable, Iterator
from dataclasses import dataclass, field
from pathlib import Path
from typing import Literal

from tree_sitter import Node, Parser, Tree

from .ast_nodes import declarator_identifier, iter_nodes, text
from .findings import Finding
from .parser import parse_source

TagKind = Literal["acquires", "releases"]

CONDITIONS = ("on-success", "on-non-null")
ANY_CLASS = "*"
RETURN_TARGET = "return"

_IDENTIFIER = re.compile(r"[A-Za-z_]\w*\Z")
_TAG_LINE = re.compile(r"@(acquires|releases)\b(.*)")
_COMMENT_PREFIX = re.compile(r"^\s*(?:/\*+|//[/!]?)?\s*\**")
_DECLARATION_TYPES = (
    "declaration",
    "function_definition",
    "preproc_def",
    "preproc_function_def",
)


@dataclass(frozen=True)
class Tag:
    kind: TagKind
    resource_class: str
    target: str
    condition: str | None = None
    line: int = field(default=0, compare=False)


@dataclass(frozen=True)
class Spec:
    name: str
    params: tuple[str, ...]
    tags: frozenset[Tag]
    file: str = field(default="", compare=False)
    line: int = field(default=0, compare=False)

    @property
    def acquires(self) -> list[Tag]:
        return sorted(
            (t for t in self.tags if t.kind == "acquires"), key=lambda t: t.line
        )

    @property
    def releases(self) -> list[Tag]:
        return sorted(
            (t for t in self.tags if t.kind == "releases"), key=lambda t: t.line
        )

    def arg_index(self, target: str) -> int | None:
        return self.params.index(target) if target in self.params else None


@dataclass
class Scan:
    specs: list[Spec] = field(default_factory=list)
    findings: list[Finding] = field(default_factory=list)


class Annotations:
    def __init__(self):
        self._specs: dict[str, Spec] = {}

    def get(self, name: str | None) -> Spec | None:
        return self._specs.get(name) if name is not None else None

    def copy(self) -> "Annotations":
        clone = Annotations()
        clone._specs = dict(self._specs)
        return clone

    def add(self, spec: Spec) -> Finding | None:
        existing = self._specs.get(spec.name)
        if existing is None:
            self._specs[spec.name] = spec
            return None
        if existing.tags == spec.tags:
            return None
        return _error(
            spec.file,
            spec.line,
            f"tags on '{spec.name}' conflict with the tags at {existing.file}:{existing.line}",
        )


def _error(file: str, line: int, message: str) -> Finding:
    return Finding(
        file=file, line=line, text=f"[{file}:{line}] ANNOTATION ERROR: {message}"
    )


def _parameter_names(parameters: Node | None) -> tuple[str, ...]:
    if parameters is None:
        return ()
    names = []
    for param in parameters.named_children:
        if param.type == "parameter_declaration":
            names.append(
                text(declarator_identifier(param.child_by_field_name("declarator")))
            )
        elif param.type == "identifier":
            names.append(text(param))
        elif param.type == "variadic_parameter":
            names.append("")
    return tuple(names)


@dataclass(frozen=True)
class _Target:
    name: str
    params: tuple[str, ...]
    returns_void: bool


def _describe(node: Node) -> _Target | None:
    if node.type == "preproc_def":
        return _Target(text(node.child_by_field_name("name")), (), False)
    if node.type == "preproc_function_def":
        params = _parameter_names(node.child_by_field_name("parameters"))
        return _Target(text(node.child_by_field_name("name")), params, False)

    declarator = node.child_by_field_name("declarator")
    returns_pointer = False
    while declarator is not None and declarator.type == "pointer_declarator":
        returns_pointer = True
        declarator = declarator.child_by_field_name("declarator")
    if declarator is None or declarator.type != "function_declarator":
        return None
    name = declarator.child_by_field_name("declarator")
    if name is None or name.type != "identifier":
        return None
    type_node = node.child_by_field_name("type")
    returns_void = (
        not returns_pointer and type_node is not None and text(type_node) == "void"
    )
    params = _parameter_names(declarator.child_by_field_name("parameters"))
    return _Target(text(name), params, returns_void)


def _comment_lines(comment: Node) -> Iterator[tuple[int, str]]:
    first_line = comment.start_point[0] + 1
    for offset, raw in enumerate(text(comment).splitlines()):
        stripped = _COMMENT_PREFIX.sub("", raw, count=1)
        stripped = re.sub(r"\*+/\s*$", "", stripped).strip()
        yield first_line + offset, stripped


def _parse_tag(kind: TagKind, rest: str, line: int, file: str) -> Tag | Finding:
    words = rest.split()
    if len(words) not in (2, 3):
        usage = (
            "@acquires <class> <target> [on-success | on-non-null]"
            if kind == "acquires"
            else "@releases <class | *> <target>"
        )
        return _error(file, line, f"malformed tag, expected '{usage}'")

    resource_class, target = words[0], words[1]
    modifier = words[2] if len(words) == 3 else None

    if resource_class == ANY_CLASS:
        if kind == "acquires":
            return _error(file, line, "'*' is only valid on @releases")
    elif not _IDENTIFIER.match(resource_class):
        return _error(file, line, f"'{resource_class}' is not a valid resource class")

    if target != RETURN_TARGET and not _IDENTIFIER.match(target):
        return _error(file, line, f"'{target}' is not a parameter name or 'return'")

    if modifier is not None:
        if modifier not in CONDITIONS:
            return _error(file, line, f"unknown modifier '{modifier}'")
        if kind == "releases":
            return _error(file, line, f"'{modifier}' is not valid on @releases")

    return Tag(kind, resource_class, target, modifier, line)


def _validate_target(tag: Tag, described: _Target, file: str) -> Finding | None:
    if tag.target == RETURN_TARGET:
        if described.returns_void:
            return _error(
                file, tag.line, f"'return' target on void function '{described.name}'"
            )
        return None
    if tag.target not in described.params:
        return _error(
            file, tag.line, f"'{tag.target}' is not a parameter of '{described.name}'"
        )
    return None


def _find_conflicts(tags: list[Tag], file: str) -> list[Finding]:
    seen: dict[tuple[str, str], Tag] = {}
    findings = []
    for tag in tags:
        key = (tag.kind, tag.target)
        previous = seen.setdefault(key, tag)
        if previous != tag:
            findings.append(
                _error(
                    file,
                    tag.line,
                    f"@{tag.kind} '{tag.target}' conflicts with the tag on line {previous.line}",
                )
            )
    return findings


def _tags_in(comment: Node, file: str) -> tuple[list[Tag], list[Finding]]:
    tags, findings = [], []
    for line, content in _comment_lines(comment):
        match = _TAG_LINE.match(content)
        if match is None:
            continue
        parsed = _parse_tag(match.group(1), match.group(2), line, file)
        (findings if isinstance(parsed, Finding) else tags).append(parsed)
    return tags, findings


def scan_tree(tree: Tree, file: str) -> Scan:
    scan = Scan()
    root = tree.root_node
    for comment in (n for n in iter_nodes(root) if n.type == "comment"):
        tags, findings = _tags_in(comment, file)
        scan.findings.extend(findings)
        if not tags:
            continue

        following = comment.next_named_sibling
        described = (
            _describe(following)
            if following is not None and following.type in _DECLARATION_TYPES
            else None
        )
        if described is None:
            scan.findings.append(
                _error(
                    file,
                    tags[0].line,
                    "tag is not followed by a function or macro declaration",
                )
            )
            continue

        valid = []
        for tag in tags:
            problem = _validate_target(tag, described, file)
            if problem is None:
                valid.append(tag)
            else:
                scan.findings.append(problem)

        conflicts = _find_conflicts(valid, file)
        scan.findings.extend(conflicts)
        if conflicts or not valid:
            continue
        scan.specs.append(
            Spec(
                described.name,
                described.params,
                frozenset(valid),
                file=file,
                line=min(t.line for t in valid),
            )
        )
    return scan


def scan_source(source: bytes, file: str, parser: Parser | None = None) -> Scan:
    return scan_tree(parse_source(source, parser), file)


def scan_file(path: Path, parser: Parser | None = None) -> Scan:
    return scan_source(path.read_bytes(), str(path), parser)


def iter_annotation_files(target: Path) -> Iterator[Path]:
    if target.is_file():
        yield target
        return
    yield from sorted(target.rglob("*.h"))


def load_annotations(
    targets: Iterable[Path], parser: Parser | None = None
) -> tuple[Annotations, list[Finding]]:
    registry = Annotations()
    findings: list[Finding] = []
    for target in targets:
        for path in iter_annotation_files(target):
            scan = scan_file(path, parser)
            findings.extend(scan.findings)
            findings.extend(
                f for spec in scan.specs if (f := registry.add(spec)) is not None
            )
    return registry, findings
