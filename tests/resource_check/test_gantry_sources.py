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
    """Annotations from the same places as `just check`, the checked trees included."""
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


def _check_mutated(tmp_path, annotations, source: Path, old: str, new: str) -> list:
    parser = make_parser()
    assert check_file(source, annotations, parser) == []
    text = source.read_text()
    assert text.count(old) == 1
    mutated = tmp_path / source.name
    mutated.write_text(text.replace(old, new))
    return check_file(mutated, annotations, parser)


def test_removing_a_pass_ownership_marker_is_reported(tmp_path, library_annotations):
    findings = _check_mutated(
        tmp_path,
        library_annotations,
        LIB / "event.c",
        "PASS_OWNERSHIP(event_block);",
        "",
    )

    assert any("'event_block'" in f.text for f in findings)


def test_keeping_a_message_the_queue_refused_is_reported(tmp_path, library_annotations):
    findings = _check_mutated(
        tmp_path,
        library_annotations,
        LIB / "stow" / "stow_runtime.c",
        'queue full", session_id);\n        net_buf_unref(held);',
        'queue full", session_id);',
    )

    assert any("'held'" in f.text and "not released" in f.text for f in findings)


def test_sending_a_response_twice_is_reported(tmp_path, library_annotations):
    send = "stow_transport_send(session->transport, session->id, buf);"
    findings = _check_mutated(
        tmp_path,
        library_annotations,
        LIB / "stow" / "stow_runtime.c",
        f"\n    ret = {send}",
        f"\n    (void){send}\n    ret = {send}",
    )

    assert any("'buf' released twice" in f.text for f in findings)


def test_a_response_neither_sent_nor_dropped_is_reported(tmp_path, library_annotations):
    findings = _check_mutated(
        tmp_path,
        library_annotations,
        LIB / "stow" / "stow_runtime.c",
        'dropping the response");\n        net_buf_unref(buf);',
        'dropping the response");',
    )

    assert any("'buf'" in f.text and "not released" in f.text for f in findings)


def test_keeping_a_borrowed_buffer_is_reported(tmp_path, library_annotations):
    findings = _check_mutated(
        tmp_path,
        library_annotations,
        LIB / "stow" / "stow_runtime.c",
        "deliver(runtime, &entry, buf);\n    release_buf(buf);",
        "deliver(runtime, &entry, buf);",
    )

    assert any("'buf'" in f.text and "not released" in f.text for f in findings)


def test_keeping_a_set_entry_is_reported(tmp_path, library_annotations):
    findings = _check_mutated(
        tmp_path,
        library_annotations,
        LIB / "stow" / "stow_protocol.c",
        "ret = apply(session, &entry);\n            stow_msg_entry_release(&entry);",
        "ret = apply(session, &entry);",
    )

    assert any("'entry'" in f.text and "not released" in f.text for f in findings)


def test_leaving_a_client_call_unfinished_is_reported(tmp_path, library_annotations):
    findings = _check_mutated(
        tmp_path,
        library_annotations,
        LIB / "stow" / "stow_client.c",
        "complete_request(peer, slot, err, &reader, &msg);\n        return;",
        "return;",
    )

    assert any("'slot'" in f.text and "not released" in f.text for f in findings)


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
