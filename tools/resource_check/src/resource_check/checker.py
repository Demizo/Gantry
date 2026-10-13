import argparse
from pathlib import Path

from tree_sitter import Parser

from .annotations import Annotations, load_annotations, scan_tree
from .cfg import build_cfg
from .findings import Finding
from .parser import iter_function_definitions, make_parser, parse_file
from .rules import build_rule_kinds

BUNDLED_ANNOTATIONS = Path(__file__).resolve().parent / "bundled"


def check_file(path: Path, annotations: Annotations, parser: Parser) -> list[Finding]:
    tree = parse_file(path, parser)
    scan = scan_tree(tree, str(path))
    visible = annotations.copy()
    findings = list(scan.findings)
    findings.extend(f for spec in scan.specs if (f := visible.add(spec)) is not None)

    rule_kinds = build_rule_kinds(visible)
    for function_node in iter_function_definitions(tree.root_node):
        cfg = build_cfg(function_node)
        for kind in rule_kinds:
            findings.extend(kind.check(cfg, str(path)))
    return findings


def iter_c_files(target: Path):
    if target.is_file():
        yield target
        return
    yield from sorted(target.rglob("*.c"))


def main(argv=None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("paths", nargs="+", type=Path)
    parser.add_argument(
        "--annotations",
        action="append",
        default=[],
        type=Path,
        metavar="FILE_OR_DIR",
        help="header file or directory outside the checked paths to read annotations from (repeatable)",
    )
    args = parser.parse_args(argv)

    for path in args.annotations:
        if not path.exists():
            parser.error(f"annotation path not found: {path}")

    tree_sitter_parser = make_parser()
    checked_dirs = [path for path in args.paths if path.is_dir()]
    sources = [BUNDLED_ANNOTATIONS, *args.annotations, *checked_dirs]
    annotations, findings = load_annotations(sources, tree_sitter_parser)

    for target in args.paths:
        for c_file in iter_c_files(target):
            findings.extend(check_file(c_file, annotations, tree_sitter_parser))

    unique = {(f.file, f.line, f.text): f for f in findings}
    for finding in sorted(unique.values(), key=lambda f: (f.file, f.line)):
        print(finding)

    if unique:
        return 1
    print("SUCCESS: No resource leaks detected.")
    return 0
