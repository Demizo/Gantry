from pathlib import Path

import pytest
from resource_check.annotations import Annotations, load_annotations
from resource_check.checker import BUNDLED_ANNOTATIONS, check_file
from resource_check.findings import Finding
from resource_check.parser import make_parser

TEST_ANNOTATIONS = Path(__file__).parent / "annotations"


@pytest.fixture(scope="session")
def library_annotations() -> Annotations:
    registry, findings = load_annotations([BUNDLED_ANNOTATIONS, TEST_ANNOTATIONS])
    assert findings == []
    return registry


@pytest.fixture
def check_source(tmp_path, library_annotations):
    def run(source: str, annotations: Annotations | None = None) -> list[Finding]:
        path = tmp_path / "case.c"
        path.write_text(source)
        return check_file(path, annotations or library_annotations, make_parser())

    return run
