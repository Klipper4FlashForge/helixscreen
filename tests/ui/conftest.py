"""Fixtures for the helixctl-driven UI tests."""

from __future__ import annotations

import os
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).parent))  # make `helix` importable

from helix.app import HelixApp  # noqa: E402

REPO_ROOT = Path(__file__).resolve().parents[2]
BINARY = REPO_ROOT / "build" / "bin" / "helix-screen"


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
