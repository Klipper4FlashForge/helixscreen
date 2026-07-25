# SPDX-License-Identifier: GPL-3.0-or-later

"""Fixtures for the helixctl-driven UI tests."""

from __future__ import annotations

import os
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).parent))  # make `helix` importable

from helix.app import HelixApp  # noqa: E402

REPO_ROOT = Path(__file__).resolve().parents[2]
# HELIX_UI_BINARY lets a test that copies this conftest elsewhere (see
# test_diagnostics.py's pytester sub-run) point back at the real binary —
# REPO_ROOT above is computed from *this* file's location, which is wrong
# once the file has been copied into a temp directory.
BINARY = Path(os.environ.get("HELIX_UI_BINARY", str(REPO_ROOT / "build" / "bin" / "helix-screen")))


@pytest.fixture(scope="session")
def helix_app(tmp_path_factory):
    """One app instance shared by the whole session.

    Session scope because a boot costs ~2s and a full corpus run is hundreds of
    tests. Tests that dirty global state should request `fresh_helix_app`.
    """
    if not BINARY.exists():
        pytest.skip(f"{BINARY} not built — run `make -j`")
    workdir = tmp_path_factory.mktemp("helix-session")
    app = HelixApp(binary=BINARY,
                   socket_path=workdir / "control.sock",
                   log_path=workdir / "app.log")
    with app:
        yield app


@pytest.fixture
def fresh_helix_app(tmp_path):
    """A private instance for a single test. Use when a test dirties global state."""
    if not BINARY.exists():
        pytest.skip(f"{BINARY} not built — run `make -j`")
    app = HelixApp(binary=BINARY,
                   socket_path=tmp_path / "control.sock",
                   log_path=tmp_path / "app.log")
    with app:
        yield app


ARTIFACT_ROOT = Path(os.environ.get("HELIX_UI_ARTIFACTS", "ui-artifacts"))


@pytest.hookimpl(hookwrapper=True, tryfirst=True)
def pytest_runtest_makereport(item, call):
    """Stash each phase's report on the item so fixtures can see the outcome."""
    outcome = yield
    report = outcome.get_result()
    setattr(item, f"rep_{report.when}", report)


@pytest.fixture
def artifacts(request, helix_app):
    """A directory for this test's diagnostics. Populated only if the test fails."""
    target = ARTIFACT_ROOT / request.node.name
    yield target

    failed = any(
        getattr(request.node, f"rep_{phase}", None) is not None
        and getattr(request.node, f"rep_{phase}").failed
        for phase in ("setup", "call", "teardown")
    )
    if not failed:
        return

    target.mkdir(parents=True, exist_ok=True)
    # Each dump is independently best-effort: the app may be wedged or dead, and
    # losing the screenshot must not cost us the log.
    try:
        helix_app.screenshot(str((target / "screen.png").resolve()))
    except Exception as exc:  # noqa: BLE001 - diagnostics must never mask the real failure
        (target / "screen.png.error").write_text(str(exc))
    try:
        (target / "app.log").write_text("\n".join(helix_app.log(200)))
    except Exception:  # noqa: BLE001
        (target / "app.log").write_text(helix_app._log_tail_from_file(200))
    try:
        state = [f"current: {helix_app.current()}", "", f"ls: {helix_app.ls()}"]
        (target / "state.txt").write_text("\n".join(state))
    except Exception as exc:  # noqa: BLE001
        (target / "state.txt").write_text(f"state dump failed: {exc}")

    print(f"\n[ui-artifacts] failure diagnostics written to {target}")
