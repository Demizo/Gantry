from __future__ import annotations

import pytest

pytest_plugins = ["pytester"]


def test_a_suite_without_link_changed_fails_at_the_network_fixture(
    pytester: pytest.Pytester,
):
    pytester.makepyfile(
        """
        def test_uses_the_network(network):
            pass
        """
    )
    result = pytester.runpytest()
    result.assert_outcomes(errors=1)
    result.stdout.fnmatch_lines(["*Override the link_changed fixture*"])
