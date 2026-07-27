# SPDX-License-Identifier: GPL-3.0-or-later

"""The diagnostics fixture must actually produce artifacts on failure.

Uses pytester so we can assert on the outcome of a *deliberately failing* test
without failing this suite.
"""

from pathlib import Path

pytest_plugins = ["pytester"]

CONFTEST_SRC = (Path(__file__).parent / "conftest.py").read_text()

# The copied conftest (below) computes its own REPO_ROOT from *its* __file__,
# which lives under a pytest tmp dir once pytester copies it — so it can never
# find the real binary on its own. HELIX_UI_BINARY overrides that.
REPO_ROOT = Path(__file__).resolve().parents[2]
BINARY = REPO_ROOT / "build" / "bin" / "helix-screen"


def test_artifacts_written_when_a_test_fails(pytester, monkeypatch):
    # Belt-and-suspenders: in-process pytester runs share sys.modules/sys.path
    # with the outer session (so `helix` is already importable), but set this
    # too in case a future run uses `--runpytest=subprocess`.
    monkeypatch.setenv("PYTHONPATH", str(Path(__file__).parent))
    monkeypatch.setenv("HELIX_UI_BINARY", str(BINARY))
    # This test's glob below hardcodes the "ui-artifacts" dirname the copied
    # conftest defaults to. If the outer environment has HELIX_UI_ARTIFACTS
    # set to something else, the pytester sub-run inherits it, writes
    # artifacts under a different name, and the glob below matches nothing —
    # next() on that empty iterator raises a bare, unhelpful StopIteration.
    monkeypatch.delenv("HELIX_UI_ARTIFACTS", raising=False)
    pytester.makeconftest(CONFTEST_SRC)
    pytester.makepyfile(
        test_boom="""
        def test_deliberate_failure(helix_app, artifacts):
            assert False, "boom"
        """
    )
    result = pytester.runpytest("-p", "no:cacheprovider")
    result.assert_outcomes(failed=1)

    dump_dir = next(pytester.path.glob("**/ui-artifacts/test_deliberate_failure"))
    names = {p.name for p in dump_dir.iterdir()}
    assert "screen.png" in names
    assert "app.log" in names
    assert "state.txt" in names

    # Filenames existing isn't enough — a zero-byte PNG or an empty log would
    # pass a bare presence check while telling a debugging session nothing.
    assert (dump_dir / "screen.png").stat().st_size > 0
    assert (dump_dir / "app.log").read_text().strip() != ""
    assert (dump_dir / "state.txt").read_text().strip() != ""
