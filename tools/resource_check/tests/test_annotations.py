from pathlib import Path

import pytest
from resource_check.annotations import Annotations, Tag, load_annotations, scan_source


def _scan(source: str):
    return scan_source(source.encode(), "t.h")


def _only_spec(source: str):
    scan = _scan(source)
    assert scan.findings == []
    assert len(scan.specs) == 1
    return scan.specs[0]


def test_function_declaration_tags():
    spec = _only_spec("""
        /**
         * @acquires memory block_ptr on-success
         * @param[out] block_ptr destination
         */
        int mem_alloc(size_t size, void** block_ptr);
    """)
    assert spec.name == "mem_alloc"
    assert spec.params == ("size", "block_ptr")
    assert spec.acquires == [Tag("acquires", "memory", "block_ptr", "on-success")]
    assert spec.releases == []


def test_single_line_comment_and_function_definition():
    spec = _only_spec("""
        /** @releases memory block_ptr */
        static void drop(void** block_ptr) { }
    """)
    assert spec.name == "drop"
    assert spec.releases == [Tag("releases", "memory", "block_ptr")]


def test_several_tags_on_one_function():
    spec = _only_spec("""
        /**
         * @releases memory old_block
         * @acquires memory new_block on-non-null
         */
        int swap(void* old_block, void** new_block);
    """)
    assert {t.kind for t in spec.tags} == {"acquires", "releases"}


def test_object_like_and_function_like_macros():
    scan = _scan("""
        /** @releases * data */
        #define PASS_OWNERSHIP(data)
        /** @acquires irq return */
        #define LOCK irq_lock
    """)
    assert scan.findings == []
    pass_ownership, lock = scan.specs
    assert pass_ownership.params == ("data",)
    assert pass_ownership.releases == [Tag("releases", "*", "data")]
    assert lock.params == ()
    assert lock.acquires == [Tag("acquires", "irq", "return")]


def test_return_target_on_value_returning_and_pointer_returning_functions():
    scan = _scan("""
        /** @acquires irq return */
        unsigned int irq_lock(void);
        /** @acquires net_buf return on-non-null */
        struct net_buf* net_buf_alloc(struct net_buf_pool* pool, k_timeout_t timeout);
        /** @acquires heap return on-non-null */
        void* heap_alloc(size_t n);
    """)
    assert scan.findings == []
    assert [s.name for s in scan.specs] == ["irq_lock", "net_buf_alloc", "heap_alloc"]


def test_tags_inside_preprocessor_conditionals():
    scan = _scan("""
        #ifdef CONFIG_X
        /** @releases memory p */
        void a(void* p);
        #else
        /** @releases memory p */
        void b(void* p);
        #endif
    """)
    assert scan.findings == []
    assert [s.name for s in scan.specs] == ["a", "b"]


def test_comments_without_tags_are_ignored():
    scan = _scan("""
        /** Mentions @acquires in prose. */
        int f(void);
        // plain comment
        int g(void);
    """)
    assert scan.specs == []
    assert scan.findings == []


def test_unnamed_and_variadic_parameters_keep_argument_positions():
    spec = _only_spec("""
        /** @releases memory p */
        void f(int, void* p, ...);
    """)
    assert spec.arg_index("p") == 1


@pytest.mark.parametrize(
    ("source", "message"),
    [
        (
            "/** @releases memory nope */\nvoid f(void* p);",
            "'nope' is not a parameter of 'f'",
        ),
        (
            "/** @acquires memory return */\nvoid f(void);",
            "'return' target on void function 'f'",
        ),
        (
            "/** @acquires memory p */\nstruct s { int a; };",
            "not followed by a function or macro",
        ),
        (
            "/** @acquires memory p */\nint x = 3;",
            "not followed by a function or macro",
        ),
        ("/** @acquires memory p */\n", "not followed by a function or macro"),
        ("/** @acquires * p */\nvoid f(void** p);", "'*' is only valid on @releases"),
        (
            "/** @releases memory p on-success */\nvoid f(void* p);",
            "'on-success' is not valid on @releases",
        ),
        (
            "/** @acquires memory p sometimes */\nvoid f(void** p);",
            "unknown modifier 'sometimes'",
        ),
        ("/** @acquires memory */\nvoid f(void** p);", "malformed tag"),
        ("/** @releases memory p extra words */\nvoid f(void* p);", "malformed tag"),
        ("/** @acquires 9lives p */\nvoid f(void** p);", "not a valid resource class"),
        ("/** @acquires memory p->q */\nvoid f(void** p);", "not a parameter name"),
        (
            "/**\n * @acquires memory p\n * @acquires irq p\n */\nvoid f(void** p);",
            "@acquires 'p' conflicts with the tag on line 2",
        ),
        (
            "/**\n * @acquires memory p on-success\n * @acquires memory p\n */\nvoid f(void** p);",
            "conflicts with the tag on line 2",
        ),
    ],
)
def test_validation_errors(source, message):
    scan = _scan(source)
    assert len(scan.findings) >= 1
    assert message in scan.findings[0].text
    assert scan.findings[0].text.startswith("[t.h:")
    assert "ANNOTATION ERROR" in scan.findings[0].text
    assert scan.specs == []


def test_validation_error_reports_the_tag_line():
    scan = _scan(
        "int a;\n\n/**\n * brief\n * @releases memory nope\n */\nvoid f(void* p);"
    )
    assert scan.findings[0].line == 5


def test_stacked_tag_comments_report_the_detached_one():
    scan = _scan("""
        /** @releases memory p */
        /** more docs */
        void f(void* p);
    """)
    assert len(scan.findings) == 1
    assert "not followed by" in scan.findings[0].text


def test_duplicate_identical_tags_are_not_a_conflict():
    spec = _only_spec("""
        /**
         * @releases memory p
         * @releases memory p
         */
        void f(void* p);
    """)
    assert len(spec.tags) == 1


def test_registry_accepts_identical_redeclaration_and_rejects_different_tags():
    registry = Annotations()
    decl = _only_spec("/** @releases memory p */\nvoid f(void* p);")
    again = _only_spec("/** @releases memory p */\nvoid f(void* p) { }")
    different = _only_spec("/** @releases * p */\nvoid f(void* p);")

    assert registry.add(decl) is None
    assert registry.add(again) is None
    conflict = registry.add(different)
    assert conflict is not None
    assert "conflict" in conflict.text


def test_load_annotations_reads_directories_and_files(tmp_path: Path):
    (tmp_path / "a.h").write_text("/** @releases memory p */\nvoid a(void* p);")
    (tmp_path / "sub").mkdir()
    (tmp_path / "sub" / "b.h").write_text("/** @releases memory p */\nvoid b(void* p);")
    (tmp_path / "ignored.c").write_text("/** @releases memory p */\nvoid c(void* p);")
    extra = tmp_path / "extra.txt"
    extra.write_text("/** @releases memory p */\nvoid d(void* p);")

    registry, findings = load_annotations([tmp_path, extra])

    assert findings == []
    assert registry.get("a") and registry.get("b") and registry.get("d")
    assert registry.get("c") is None


def test_load_annotations_reports_conflicts_across_files(tmp_path: Path):
    (tmp_path / "a.h").write_text("/** @releases memory p */\nvoid f(void* p);")
    (tmp_path / "b.h").write_text("/** @releases irq p */\nvoid f(void* p);")

    _, findings = load_annotations([tmp_path])

    assert len(findings) == 1
    assert "b.h" in findings[0].text
    assert "a.h" in findings[0].text
