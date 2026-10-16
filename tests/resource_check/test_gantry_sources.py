import shutil
from pathlib import Path

import pytest
from resource_check.annotations import load_annotations
from resource_check.checker import BUNDLED_ANNOTATIONS, check_file, iter_c_files
from resource_check.parser import make_parser

REPO = Path(__file__).resolve().parents[2]
INCLUDE = REPO / "include"
LIB = REPO / "lib"
SAMPLE_SRC = REPO / "samples" / "example_app" / "src"
SAMPLE_INC = REPO / "samples" / "example_app" / "inc"


def _annotations_with(include_dir: Path):
    """Annotations from the same places as `just analyze`, the checked trees included."""
    registry, findings = load_annotations(
        [BUNDLED_ANNOTATIONS, include_dir, SAMPLE_INC, LIB, SAMPLE_SRC]
    )
    assert findings == []
    return registry


@pytest.fixture(scope="module")
def library_annotations():
    return _annotations_with(INCLUDE)


def _check_tree(root: Path, annotations) -> list:
    parser = make_parser()
    return [
        f for path in iter_c_files(root) for f in check_file(path, annotations, parser)
    ]


def test_library_and_sample_are_clean():
    registry = _annotations_with(INCLUDE)
    assert _check_tree(LIB, registry) == []
    assert _check_tree(SAMPLE_SRC, registry) == []


def test_removing_a_pass_ownership_marker_is_reported(tmp_path, library_annotations):
    source = (LIB / "event.c").read_text()
    assert "PASS_OWNERSHIP(event_block);" in source
    mutated = tmp_path / "event.c"
    mutated.write_text(source.replace("PASS_OWNERSHIP(event_block);", ""))

    findings = check_file(mutated, library_annotations, make_parser())

    assert any("'event_block'" in f.text for f in findings)


def test_removing_the_release_annotation_is_reported(tmp_path):
    include = tmp_path / "include"
    shutil.copytree(INCLUDE, include)
    memory_h = include / "gantry" / "memory.h"
    memory_h.write_text(memory_h.read_text().replace(" * @releases memory data\n", ""))

    findings = _check_tree(LIB, _annotations_with(include))

    assert findings
    assert all("was not released" in f.text for f in findings)


def test_removing_the_pass_ownership_annotation_is_reported(tmp_path):
    include = tmp_path / "include"
    shutil.copytree(INCLUDE, include)
    memory_h = include / "gantry" / "memory.h"
    memory_h.write_text(memory_h.read_text().replace(" * @releases * data\n", ""))

    findings = _check_tree(LIB, _annotations_with(include))

    assert findings
