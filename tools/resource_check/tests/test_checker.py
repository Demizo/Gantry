import re
from pathlib import Path

import pytest
from resource_check.checker import check_file, main
from resource_check.parser import make_parser

FIXTURES_DIR = Path(__file__).parent / "fixtures"

_LEAK_COMMENT = re.compile(r"//\s*LEAK:\s*(\w+)")
_IDENTIFIER_IN_FINDING = re.compile(r"Variable '(\w+)'")


def _expected_findings(path: Path) -> set[tuple[int, str]]:
    expected = set()
    for lineno, line in enumerate(path.read_text().splitlines(), start=1):
        match = _LEAK_COMMENT.search(line)
        if match:
            expected.add((lineno, match.group(1)))
    return expected


def _actual_findings(path: Path, annotations) -> set[tuple[int, str]]:
    actual = set()
    for finding in check_file(path, annotations, make_parser()):
        match = _IDENTIFIER_IN_FINDING.search(finding.text)
        assert match, f"finding text has no identifier: {finding.text}"
        actual.add((finding.line, match.group(1)))
    return actual


@pytest.mark.parametrize(
    "fixture", sorted(FIXTURES_DIR.glob("*.c")), ids=lambda p: p.stem
)
def test_fixture_matches_exactly(fixture: Path, library_annotations):
    assert _actual_findings(fixture, library_annotations) == _expected_findings(fixture)


def test_identifier_argument_rule_flags_non_identifier_argument(check_source):
    findings = check_source("""
        int f(struct s* p) {
            MEM_UNREF(p->field);
            return 0;
        }
    """)
    assert len(findings) == 1
    assert "POLICY ERROR" in findings[0].text
    assert "p->field" in findings[0].text


def test_identifier_rule_checks_the_annotated_parameter_only(check_source):
    findings = check_source("""
        int f(struct s* p) {
            STOW_RELEASE(p->id, value);
            return 0;
        }
    """)
    assert findings == []


def test_identifier_rule_ignores_return_targets(check_source):
    findings = check_source("""
        int f(void) {
            int key = irq_lock();
            irq_unlock(key);
            return 0;
        }
    """)
    assert findings == []


def test_file_local_tags_apply_to_the_file_they_are_in(check_source):
    findings = check_source("""
        /** @acquires msg msg_ptr */
        static int msg_alloc(int size, void** msg_ptr);
        /** @releases msg msg_ptr */
        static void msg_free(void** msg_ptr);

        int leak(int flag) {
            void* msg;
            msg_alloc(4, &msg);
            if (flag) {
                return 1;
            }
            msg_free(&msg);
            return 0;
        }

        int clean(void) {
            void* msg;
            msg_alloc(4, &msg);
            msg_free(&msg);
            return 0;
        }
    """)
    assert len(findings) == 1
    assert "'msg'" in findings[0].text


def test_file_local_tag_conflicting_with_a_library_tag_is_reported(check_source):
    findings = check_source("""
        /** @releases irq block_ptr */
        void mem_unref(void** block_ptr);
    """)
    assert len(findings) == 1
    assert "conflict" in findings[0].text


def test_annotation_errors_in_the_checked_file_are_reported(check_source):
    findings = check_source("""
        /** @releases memory nope */
        void f(void* p);
    """)
    assert len(findings) == 1
    assert "ANNOTATION ERROR" in findings[0].text


@pytest.mark.parametrize(
    "statement",
    [
        "framer->rx_buf = net_buf_alloc(pool, K_NO_WAIT);",
        "net_buf_ref(buf);",
        "use(net_buf_ref(buf));",
        "return net_buf_alloc(pool, K_NO_WAIT);",
        "if (net_buf_alloc(pool, K_NO_WAIT) == NULL) { return 1; }",
    ],
)
def test_return_target_acquire_must_be_assigned_to_an_identifier(
    check_source, statement
):
    findings = check_source(f"""
        struct net_buf* f(struct net_buf* buf, struct net_buf_pool* pool, struct framer* framer) {{
            {statement}
            return NULL;
        }}
    """)
    assert len(findings) == 1
    assert "POLICY ERROR" in findings[0].text
    assert "net_buf_" in findings[0].text


@pytest.mark.parametrize(
    "assignment",
    [
        "struct net_buf* b = net_buf_alloc(pool, K_NO_WAIT);",
        "struct net_buf* b = (struct net_buf*)net_buf_alloc(pool, K_NO_WAIT);",
        "struct net_buf* b; b = (net_buf_alloc(pool, K_NO_WAIT));",
    ],
)
def test_assigned_return_target_acquire_is_tracked_through_casts(
    check_source, assignment
):
    findings = check_source(f"""
        int f(struct net_buf_pool* pool, int flag) {{
            {assignment}
            if (b == NULL) {{
                return -1;
            }}
            if (flag) {{
                return 1;  // LEAK
            }}
            net_buf_unref(b);
            return 0;
        }}
    """)
    assert [f.line for f in findings] == [8]
    assert "Variable 'b'" in findings[0].text


def test_code_the_parser_cannot_read_is_reported(check_source):
    findings = check_source("""
        int f(int x) {
        #if A
            if (x) {
        #else
            if (!x) {
        #endif
                return 1;
            }
            return 0;
        }
    """)
    assert len(findings) == 1
    assert "PARSE ERROR" in findings[0].text
    assert findings[0].line == 2


@pytest.mark.parametrize(
    "body",
    [
        'LOG_INF("no semicolon")\n return 0;',
        "K_SPINLOCK(&lock) {\n if (x) { return 1; }\n }\n return 0;",
    ],
)
def test_macro_syntax_that_keeps_the_structure_is_not_a_parse_error(check_source, body):
    findings = check_source(f"int f(int x) {{\n {body}\n}}\n")
    assert findings == []


def test_unannotated_functions_are_never_tracked(check_source):
    findings = check_source("""
        int f(int flag) {
            void* buf;
            custom_alloc(4, &buf);
            if (flag) {
                return 1;
            }
            return 0;
        }
    """)
    assert findings == []


def test_main_reads_extra_annotations_from_a_header(tmp_path, capsys):
    header = tmp_path / "app.h"
    header.write_text(
        "/** @acquires msg msg_ptr */\nint msg_alloc(int size, void** msg_ptr);"
    )
    source = tmp_path / "app.c"
    source.write_text("int f(void) { void* msg; msg_alloc(4, &msg); return 0; }")

    assert main([str(source)]) == 0
    assert main([str(source), "--annotations", str(header)]) == 1
    assert "'msg'" in capsys.readouterr().out


def test_main_reads_annotations_from_headers_under_a_checked_directory(
    tmp_path, capsys
):
    src = tmp_path / "src"
    src.mkdir()
    (src / "app.h").write_text(
        "/** @acquires msg msg_ptr */\nint msg_alloc(int size, void** msg_ptr);"
    )
    (src / "app.c").write_text(
        "int f(void) { void* msg; msg_alloc(4, &msg); return 0; }"
    )

    assert main([str(src)]) == 1
    assert "'msg'" in capsys.readouterr().out


def test_main_does_not_share_tags_from_other_checked_sources(tmp_path):
    (tmp_path / "a.c").write_text(
        "/** @acquires msg msg_ptr */\nstatic int msg_alloc(int size, void** msg_ptr);"
    )
    (tmp_path / "b.c").write_text(
        "int f(void) { void* msg; msg_alloc(4, &msg); return 0; }"
    )

    assert main([str(tmp_path)]) == 0


def test_main_rejects_a_missing_annotation_path(tmp_path):
    source = tmp_path / "app.c"
    source.write_text("int f(void) { return 0; }")
    with pytest.raises(SystemExit) as exit_info:
        main([str(source), "--annotations", str(tmp_path / "missing")])
    assert exit_info.value.code == 2


def test_main_exits_non_zero_on_annotation_errors(tmp_path, capsys):
    header = tmp_path / "bad.h"
    header.write_text("/** @releases memory nope */\nvoid f(void* p);")
    source = tmp_path / "app.c"
    source.write_text("int f(void) { return 0; }")

    assert main([str(source), "--annotations", str(header)]) == 1
    out = capsys.readouterr().out
    assert "bad.h:1" in out
    assert "nope" in out


def test_main_reports_each_annotation_error_once(tmp_path, capsys):
    header = tmp_path / "bad.h"
    header.write_text("/** @releases memory nope */\nvoid f(void* p);")

    assert main([str(header), "--annotations", str(tmp_path)]) == 1
    assert capsys.readouterr().out.count("nope") == 1


def test_library_annotations_cover_the_gantry_headers(library_annotations):
    for name in (
        "MEM_ALLOC",
        "mem_alloc",
        "MEM_UNREF",
        "PASS_OWNERSHIP",
        "STOW_GET",
        "event_unref",
        "irq_lock",
    ):
        assert library_annotations.get(name) is not None, name
