# helixctl UI Test Harness Implementation Plan

> ⚠️ **Historical record (verified 2026-08-09) - not instructions. Status: SHIPPED.** The 76
> `- [ ]` boxes were never ticked and do **not** mean the work is outstanding. Every prescribed
> artifact exists: `tests/ui/helix/app.py`, `tests/ui/helix/goldens.py`, `tests/ui/conftest.py`,
> `tests/ui/goldens/`, and all nine `tests/ui/test_*.py`. Verify against the tree before
> following anything here.

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build an out-of-process UI test harness that drives a live HelixScreen instance through `helix-screen ctl`, supporting both scripted verification and golden-screenshot regression.

**Architecture:** Add a `--json` flag plus five determinism/introspection commands to the existing JSON-RPC remote-control surface, then build a pytest harness that spawns the binary on a private socket and drives it via `ctl --json` subprocesses. Exactly one protocol client exists (the C++ `ctl` client); Python shells out to it rather than reimplementing JSON-RPC.

**Tech Stack:** C++17 (LVGL 9.5, nlohmann::json), Python 3 (pytest, Pillow, numpy), bats for CLI smoke tests.

**Spec:** `docs/devel/specs/2026-07-25-helixctl-ui-test-harness-design.md`

## Global Constraints

- Branch: `feature/ui-test-harness`. Worktree at `.worktrees/ui-test-harness`.
- **Never `git add -A` or `git add .`** — `lib/` is a symlink to the main tree and staging it clobbers real content on merge. Always stage explicit paths.
- All C++ files carry `// SPDX-License-Identifier: GPL-3.0-or-later` as the first line.
- Logging is `spdlog` only. No `printf` in server code (the `ctl` *client* is exempt — it already uses `printf` for user-facing output).
- The whole remote-control subsystem is behind `HELIX_ENABLE_REMOTE_CONTROL`. Every new server handler lives in files already inside that gate; no new `#ifdef` is needed.
- New RPC handlers register in `RemoteControlServer::register_default_handlers()` (`src/remote/remote_control_server.cpp:255-295`) and run widget work through `execute_on_ui_thread(...)`.
- `make -j` builds only the binary. Run `make test` before `./build/bin/helix-tests`.
- **Run Python through the repo venv: `./.venv/bin/python -m pytest ...`, never bare `python3`.**
  `.venv` is a symlink to the main tree's virtualenv and is the only interpreter here with
  pytest (9.1.1), Pillow, and numpy (2.4.3). Bare `python3` has none of them and will fail
  with `ModuleNotFoundError`. Every `pytest` invocation in the task steps below means
  `./.venv/bin/python -m pytest`.
- Python deps already in `requirements.txt`: `Pillow>=10.0.0`, `pandas`, `PyYAML`. `numpy` arrives via pandas.
- Commit style: subject line + one ~4-line paragraph. Reference issues as `fix(scope): thing (prestonbrown/helixscreen#123)` when one applies.

## File Structure

**C++ (server + client):**
- `src/remote/remote_client.cpp` — add `--json`; modify `handle_response` and `remote_client_main`.
- `src/remote/remote_control_server.cpp` — add `wait_idle`, `freeze`, `unfreeze`, `text`, `reset` handlers.
- `include/remote_control_server.h` — handler declarations, paused-timer vector.
- `include/ui_update_queue.h` — add `pending_count()`.
- `include/http_executor.h` / `src/system/http_executor.cpp` — add `inflight()`.
- `include/screenshot.h` / `src/system/screenshot.cpp` — split capture from encode so the server can hash and crop.

**Python harness:**
- `tests/ui/helix/app.py` — `HelixApp`: process lifecycle, `ctl --json` invocation, typed methods.
- `tests/ui/helix/goldens.py` — golden capture/compare/accept. No app knowledge.
- `tests/ui/conftest.py` — pytest fixtures and the `--accept-goldens` option.
- `tests/ui/test_*.py` — the tests themselves.
- `tests/ui/goldens/` — PNG corpus.

**Shell:**
- `tests/shell/test_helixctl_json.bats` — CLI-level smoke coverage of `--json`.

**Docs:**
- `docs/devel/HELIXCTL.md` — document each new command as it lands.
- `docs/devel/UI_TESTING.md` — add a section pointing at the new harness.

Split rationale: `app.py` owns "how do I talk to a running instance", `goldens.py` owns "how do I compare two images". Neither imports the other; `conftest.py` wires them together. That keeps `goldens.py` usable from a future soak-test runner that does not use pytest.

**Out of scope for this plan:** Tier 2 commands (`press`, `toasts`, `set_clock`) and Tier 3 (socket transport, `AsyncWorkRegistry` tokens). The spec defers these until a test demands them; they get their own plan.

---

### Task 1: `--json` output mode on the one-shot client

**Files:**
- Modify: `src/remote/remote_client.cpp:219-267` (`handle_response`), `:887-910` (arg parsing), `:49-101` (`print_usage`)
- Modify: `docs/devel/HELIXCTL.md`
- Test: `tests/shell/test_helixctl_json.bats`

**Interfaces:**
- Consumes: nothing.
- Produces: the contract every later task depends on — `helix-screen ctl --json <cmd>` writes the raw JSON-RPC `result` as one line to stdout and exits 0; on a **server** error it writes the raw JSON-RPC `error` object to stderr and exits 1. **Client-side** usage errors (unknown command, missing argument, no instance at the socket) stay human-readable on stderr and also exit non-zero.

- [ ] **Step 1: Write the failing bats test**

Create `tests/shell/test_helixctl_json.bats`:

```bash
#!/usr/bin/env bats
# CLI-level smoke coverage for `helix-screen ctl --json`.

BIN="./build/bin/helix-screen"

setup_file() {
    export SOCK="${BATS_FILE_TMPDIR}/helix-ctl.sock"
    export APP_LOG="${BATS_FILE_TMPDIR}/app.log"
    # Mirror scripts/screenshot.sh's launch: Wayland needs SDL's native driver.
    if [ -n "$WAYLAND_DISPLAY" ] && [ -z "$SDL_VIDEODRIVER" ]; then
        export SDL_VIDEODRIVER=wayland
    fi
    "$BIN" --test --skip-wizard --skip-splash --remote --remote-socket "$SOCK" \
        >"$APP_LOG" 2>&1 &
    echo $! >"${BATS_FILE_TMPDIR}/app.pid"
    for _ in $(seq 1 100); do
        [ -S "$SOCK" ] && "$BIN" ctl -s "$SOCK" ping >/dev/null 2>&1 && return 0
        sleep 0.2
    done
    echo "app never became responsive; log:" >&2
    tail -20 "$APP_LOG" >&2
    return 1
}

teardown_file() {
    "$BIN" ctl -s "$SOCK" shutdown >/dev/null 2>&1 || true
    local pid
    pid=$(cat "${BATS_FILE_TMPDIR}/app.pid" 2>/dev/null) || return 0
    for _ in $(seq 1 25); do
        kill -0 "$pid" 2>/dev/null || return 0
        sleep 0.2
    done
    kill "$pid" 2>/dev/null || true
}

@test "--json emits the raw result and nothing else" {
    run "$BIN" ctl -s "$SOCK" --json ping
    [ "$status" -eq 0 ]
    [ "$output" = '"pong"' ]
}

@test "--json result is parseable and structured for object results" {
    run "$BIN" ctl -s "$SOCK" --json current
    [ "$status" -eq 0 ]
    echo "$output" | jq -e '.panel' >/dev/null
}

@test "--json emits one line, not pretty-printed" {
    run "$BIN" ctl -s "$SOCK" --json current
    [ "$status" -eq 0 ]
    [ "$(echo "$output" | wc -l)" -eq 1 ]
}

@test "--json reports server errors as a JSON error object on stderr" {
    run --separate-stderr "$BIN" ctl -s "$SOCK" --json get nonexistent_subject_xyz
    [ "$status" -ne 0 ]
    echo "$stderr" | jq -e '.message' >/dev/null
    [ -z "$output" ]
}

@test "log stays line-oriented even under --json" {
    # The raw result is an object with a "lines" array; --json must not
    # pretty-print it, but it must still be valid JSON.
    run "$BIN" ctl -s "$SOCK" --json log -n 5
    [ "$status" -eq 0 ]
    echo "$output" | jq -e '.lines | type == "array"' >/dev/null
}
```

- [ ] **Step 2: Run it to verify it fails**

Run: `make -j && bats tests/shell/test_helixctl_json.bats`
Expected: FAIL — `--json` is parsed as a command name, so the server returns an unknown-method error.

- [ ] **Step 3: Add the flag**

In `src/remote/remote_client.cpp`, near the other file-scope statics (around line 210):

```cpp
/// When set, the one-shot client emits the raw JSON-RPC `result` instead of the
/// human-formatted rendering. Errors from the server go to stderr as the raw
/// `error` object. Client-side usage errors stay human-readable — a caller that
/// mistyped a command name needs prose, not a protocol object.
static bool g_json_output = false;
```

In `handle_response`, replace the error branch:

```cpp
        if (response.contains("error")) {
            auto& error = response["error"];
            if (g_json_output) {
                fprintf(stderr, "%s\n", error.dump().c_str());
            } else {
                fprintf(stderr, "Error: %s (code %d)\n",
                        error.value("message", "Unknown error").c_str(),
                        error.value("code", -1));
            }
            return 1;
        }
```

and the result branch's `else`:

```cpp
        if (response.contains("result")) {
            if (out_result) {
                *out_result = response["result"];
            } else if (g_json_output) {
                // Raw, single-line. Callers pipe this to jq or json.loads.
                printf("%s\n", response["result"].dump().c_str());
            } else {
                auto& result = response["result"];
                // ... existing pretty-printing unchanged ...
            }
            return 0;
        }
```

- [ ] **Step 4: Parse the flag**

In `remote_client_main`, inside the option loop before the `-s` branch:

```cpp
        if (strcmp(argv[arg_start], "--json") == 0) {
            g_json_output = true;
            arg_start++;
            continue;
        }
```

Then immediately before the two `run_repl` call sites, disable it — the REPL's formatting is
the point of the REPL:

```cpp
    g_json_output = false; // --json is a one-shot flag; the REPL always formats
```

- [ ] **Step 5: Run the tests**

Run: `make -j && bats tests/shell/test_helixctl_json.bats`
Expected: all 5 PASS.

- [ ] **Step 6: Mutation-verify the tests**

Temporarily change the `--json` result branch to `printf("%s\n", response["result"].dump(2).c_str())` (pretty-print). Re-run.
Expected: the "one line, not pretty-printed" test FAILS. Revert the change and confirm it passes again. A golden/format test that passes under both forms is not testing anything.

- [ ] **Step 7: Document it**

In `docs/devel/HELIXCTL.md`, under "One binary — `ctl` / `repl` subcommands", add:

```markdown
### Machine-readable output — `--json`

`helix-screen ctl --json <command>` prints the raw JSON-RPC `result` on one line and
exits 0. A **server** error prints the raw `error` object to stderr and exits non-zero;
a **client-side** usage error (unknown command, missing argument, no instance at the
socket) stays human-readable on stderr and also exits non-zero. This split means a
script can trust the exit code without inspecting the payload, while a typo still
produces a sentence rather than a protocol object.

The REPL ignores `--json` — formatted output is the reason the REPL exists.

    helix-screen ctl --json current | jq -r .panel
```

- [ ] **Step 8: Commit**

```bash
git add src/remote/remote_client.cpp tests/shell/test_helixctl_json.bats docs/devel/HELIXCTL.md
git commit -m "feat(helixctl): add --json output mode to the one-shot client" \
  -m "Emits the raw JSON-RPC result on one line so scripts and the coming pytest harness can consume ctl output without parsing human-formatted text. Server errors go to stderr as the raw error object with a non-zero exit; client-side usage errors stay prose, since a mistyped command needs a sentence. The REPL is unaffected."
```

---

### Task 2: Python harness skeleton — `HelixApp`

**Files:**
- Create: `tests/ui/helix/__init__.py`, `tests/ui/helix/app.py`, `tests/ui/conftest.py`, `tests/ui/test_navigation.py`
- Delete: `tests/integration/test-navigation.sh`

**Interfaces:**
- Consumes: `ctl --json` contract from Task 1.
- Produces:
  - `class HelixCtlError(RuntimeError)` with attributes `.message: str`, `.code: int`, `.command: list[str]`
  - `class HelixApp` with `ctl(*args) -> Any`, `navigate(target: str) -> dict`, `go_back() -> dict`, `click(target: str) -> dict`, `ls(target: str | None = None) -> dict`, `geom(target: str, depth: int = 0) -> dict`, `get(subject: str) -> Any`, `set(subject: str, value) -> dict`, `current() -> dict`, `log(n: int = 50) -> list[str]`, `screenshot(path: str) -> str`, `shutdown() -> None`
  - pytest fixture `helix_app` (session-scoped) yielding a `HelixApp`

- [ ] **Step 1: Write the failing test**

Create `tests/ui/test_navigation.py`. This replaces `tests/integration/test-navigation.sh`, which
asked a human to click the icons:

```python
"""Navigation smoke tests — the automated replacement for test-navigation.sh."""

import pytest


def test_app_responds_to_ping(helix_app):
    assert helix_app.ctl("ping") == "pong"


def test_lists_the_expected_base_panels(helix_app):
    panels = helix_app.ctl("list_panels")
    # list_panels returns the fixed PanelId set; these four are load-bearing
    # enough that losing one is a regression worth failing on.
    for expected in ("home", "controls", "settings", "print_select"):
        assert expected in panels, f"{expected} missing from {panels}"


@pytest.mark.parametrize("panel", ["home", "controls", "settings"])
def test_navigate_to_each_base_panel(helix_app, panel):
    helix_app.navigate(panel)
    assert helix_app.current()["panel"] == panel


def test_descend_into_an_overlay_and_back(helix_app):
    helix_app.navigate("controls")
    helix_app.click("btn_motion")
    after_click = helix_app.current()
    assert after_click["overlays"], "clicking btn_motion pushed no overlay"

    helix_app.go_back()
    assert helix_app.current()["overlays"] == []


def test_unknown_widget_raises_with_the_command_attached(helix_app):
    from helix.app import HelixCtlError

    helix_app.navigate("home")
    with pytest.raises(HelixCtlError) as exc:
        helix_app.click("no_such_widget_xyz")
    assert "no_such_widget_xyz" in " ".join(exc.value.command)
```

- [ ] **Step 2: Run it to verify it fails**

Run: `python3 -m pytest tests/ui/ -v`
Expected: FAIL — collection error, no `conftest.py` and no `helix` package.

- [ ] **Step 3: Write `HelixApp`**

Create `tests/ui/helix/__init__.py` (empty file) and `tests/ui/helix/app.py`:

```python
"""Drive a live HelixScreen instance through `helix-screen ctl --json`.

The C++ ctl client is the only JSON-RPC implementation in the tree. This module
shells out to it rather than speaking the protocol directly, so there is nothing
to keep in sync when the server changes.
"""

from __future__ import annotations

import json
import os
import signal
import subprocess
import time
from pathlib import Path
from typing import Any


class HelixCtlError(RuntimeError):
    """A command the server rejected."""

    def __init__(self, message: str, code: int, command: list[str]):
        super().__init__(f"{message} (code {code}) while running: {' '.join(command)}")
        self.message = message
        self.code = code
        self.command = command


class HelixAppError(RuntimeError):
    """The app failed to start, or died while we were driving it."""


class HelixApp:
    """A running helix-screen instance on a private control socket."""

    #: Seconds to wait for the control socket to appear and answer ping.
    BOOT_TIMEOUT = 30.0

    def __init__(self, binary: Path, socket_path: Path, log_path: Path,
                 extra_args: list[str] | None = None):
        self.binary = Path(binary)
        self.socket_path = Path(socket_path)
        self.log_path = Path(log_path)
        self.extra_args = list(extra_args or [])
        self.proc: subprocess.Popen | None = None

    # -- lifecycle ---------------------------------------------------------

    def start(self) -> "HelixApp":
        env = os.environ.copy()
        # Mirrors scripts/screenshot.sh: XWayland's GLX path crashes, so a
        # Wayland session must use SDL's native driver.
        if env.get("WAYLAND_DISPLAY") and not env.get("SDL_VIDEODRIVER"):
            env["SDL_VIDEODRIVER"] = "wayland"
        display_index = "0" if env.get("WAYLAND_DISPLAY") else "1"

        args = [
            str(self.binary),
            "--test", "--skip-wizard", "--skip-splash",
            "--remote", "--remote-socket", str(self.socket_path),
            "--display", display_index,
            *self.extra_args,
        ]
        self.log_file = self.log_path.open("w")
        self.proc = subprocess.Popen(args, stdout=self.log_file,
                                     stderr=subprocess.STDOUT, env=env)
        self._await_ready()
        return self

    def _await_ready(self) -> None:
        deadline = time.monotonic() + self.BOOT_TIMEOUT
        while time.monotonic() < deadline:
            if self.proc.poll() is not None:
                raise HelixAppError(
                    f"helix-screen exited with {self.proc.returncode} during boot\n"
                    f"{self._log_tail_from_file()}"
                )
            if self.socket_path.exists():
                try:
                    if self.ctl("ping") == "pong":
                        return
                except (HelixCtlError, HelixAppError):
                    pass  # server thread not up yet
            time.sleep(0.1)
        raise HelixAppError(
            f"helix-screen never answered ping within {self.BOOT_TIMEOUT}s\n"
            f"{self._log_tail_from_file()}"
        )

    def stop(self) -> None:
        if self.proc is None or self.proc.poll() is not None:
            return
        try:
            self.ctl("shutdown")
        except (HelixCtlError, HelixAppError):
            pass
        try:
            self.proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            self.proc.send_signal(signal.SIGTERM)
            try:
                self.proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.proc.kill()
        finally:
            if hasattr(self, "log_file"):
                self.log_file.close()

    def __enter__(self) -> "HelixApp":
        return self.start()

    def __exit__(self, *exc_info) -> None:
        self.stop()

    def _log_tail_from_file(self, n: int = 30) -> str:
        """Read the app's stdout log directly — used when the RPC channel is dead."""
        try:
            lines = self.log_path.read_text(errors="replace").splitlines()
        except OSError:
            return "(no log available)"
        return "\n".join(lines[-n:])

    # -- raw command -------------------------------------------------------

    def ctl(self, *args: Any) -> Any:
        """Run one `ctl --json` command; return the parsed result."""
        command = [str(self.binary), "ctl", "-s", str(self.socket_path), "--json",
                   *[str(a) for a in args]]
        completed = subprocess.run(command, capture_output=True, text=True, timeout=180)

        if completed.returncode != 0:
            stderr = completed.stderr.strip()
            try:
                err = json.loads(stderr)
                raise HelixCtlError(err.get("message", stderr),
                                    int(err.get("code", -1)), command)
            except (json.JSONDecodeError, ValueError):
                # Client-side usage error, or the app died. Distinguish them,
                # because "no instance at socket" and "you typo'd" need
                # different reactions from whoever reads the failure.
                if self.proc is not None and self.proc.poll() is not None:
                    raise HelixAppError(
                        f"helix-screen died (exit {self.proc.returncode})\n"
                        f"{self._log_tail_from_file()}"
                    ) from None
                raise HelixCtlError(stderr or "ctl failed", -1, command) from None

        out = completed.stdout.strip()
        return json.loads(out) if out else None

    # -- typed wrappers ----------------------------------------------------

    def navigate(self, target: str) -> dict:
        return self.ctl("navigate", target)

    def go_back(self) -> dict:
        return self.ctl("go_back")

    def click(self, target: str) -> dict:
        return self.ctl("click", target)

    def ls(self, target: str | None = None) -> dict:
        return self.ctl("ls", target) if target else self.ctl("ls")

    def geom(self, target: str, depth: int = 0) -> dict:
        return self.ctl("geom", target, depth) if depth else self.ctl("geom", target)

    def get(self, subject: str) -> Any:
        return self.ctl("get", subject)

    def set(self, subject: str, value: Any) -> dict:
        return self.ctl("set", subject, value)

    def current(self) -> dict:
        # CLI token is `current`; `get_current` is the wire method name.
        return self.ctl("current")

    def log(self, n: int = 50) -> list[str]:
        result = self.ctl("log", "-n", n)
        return result.get("lines", []) if isinstance(result, dict) else []

    def screenshot(self, path: str) -> str:
        return self.ctl("screenshot", path)["path"]

    def shutdown(self) -> None:
        self.stop()
```

- [ ] **Step 4: Write the pytest fixture**

Create `tests/ui/conftest.py`:

```python
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
```

- [ ] **Step 5: Run the tests**

Run: `make -j && python3 -m pytest tests/ui/ -v`
Expected: all PASS. If `test_descend_into_an_overlay_and_back` is flaky, do **not** add a
`sleep` — that is exactly what Task 4 exists to fix. Mark it `@pytest.mark.xfail(reason="needs
wait_idle, Task 4")` and move on.

- [ ] **Step 6: Remove the script it replaces**

```bash
git rm tests/integration/test-navigation.sh
```

`tests/integration/` becomes empty; leave the directory removed rather than adding a
placeholder.

- [ ] **Step 7: Commit**

```bash
git add tests/ui/ docs/devel/UI_TESTING.md
git rm --cached tests/integration/test-navigation.sh 2>/dev/null || true
git commit -m "test(ui): add a pytest harness that drives a live instance via ctl" \
  -m "HelixApp boots helix-screen on a private control socket and drives it through ctl --json subprocesses, so tests exercise the real widget lifecycle instead of building widgets in-process. Replaces tests/integration/test-navigation.sh, which ran the app for ten seconds and asked a human to click the icons. Session-scoped fixture shares one instance; fresh_helix_app is available for tests that dirty global state."
```

---

### Task 3: Failure diagnostics

**Files:**
- Modify: `tests/ui/conftest.py`
- Test: `tests/ui/test_diagnostics.py`

**Interfaces:**
- Consumes: `HelixApp` from Task 2.
- Produces: pytest fixture `artifacts` (function-scoped) returning a `Path`; on test failure the
  directory contains `screen.png`, `app.log`, and `state.txt`.

Rationale: a bare `assert False` diagnoses nothing. A PNG plus the app's own log tail is
usually the entire diagnosis, and it is what makes this harness replace the manual loop rather
than merely automate it.

- [ ] **Step 1: Write the failing test**

Create `tests/ui/test_diagnostics.py`:

```python
"""The diagnostics fixture must actually produce artifacts on failure.

Uses pytester so we can assert on the outcome of a *deliberately failing* test
without failing this suite.
"""

from pathlib import Path

pytest_plugins = ["pytester"]

CONFTEST_SRC = (Path(__file__).parent / "conftest.py").read_text()


def test_artifacts_written_when_a_test_fails(pytester):
    pytester.makeconftest(CONFTEST_SRC)
    pytester.makepyfile(
        test_boom="""
        def test_deliberate_failure(helix_app, artifacts):
            assert False, "boom"
        """
    )
    result = pytester.runpytest("-p", "no:cacheprovider")
    result.assert_outcomes(failed=1)

    dumps = list(pytester.path.glob("**/ui-artifacts/test_deliberate_failure/*"))
    names = {p.name for p in dumps}
    assert "screen.png" in names
    assert "app.log" in names
    assert "state.txt" in names
```

Note: `pytester` needs the sub-test to find the `helix` package. If the copied conftest's
`sys.path.insert` does not resolve from pytester's temp directory, set
`monkeypatch.setenv("PYTHONPATH", str(Path(__file__).parent))` in the test before
`runpytest`.

- [ ] **Step 2: Run it to verify it fails**

Run: `python3 -m pytest tests/ui/test_diagnostics.py -v`
Expected: FAIL — no `artifacts` fixture exists.

- [ ] **Step 3: Add the hook and fixture**

Append to `tests/ui/conftest.py`:

```python
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
```

Add `ui-artifacts/` to `.gitignore`.

- [ ] **Step 4: Run the test**

Run: `python3 -m pytest tests/ui/test_diagnostics.py -v`
Expected: PASS, and the three files exist.

- [ ] **Step 5: Verify it stays quiet on success**

Run: `rm -rf ui-artifacts && python3 -m pytest tests/ui/test_navigation.py && ls ui-artifacts 2>&1`
Expected: `ls` reports no such directory. A diagnostics fixture that dumps on every green run
fills the disk and trains you to ignore it.

- [ ] **Step 6: Commit**

```bash
git add tests/ui/conftest.py tests/ui/test_diagnostics.py .gitignore
git commit -m "test(ui): dump screenshot, log tail, and screen state on failure" \
  -m "A failing UI assertion on its own says nothing useful. The artifacts fixture writes screen.png, the app's own ring-buffer log, and a current+ls state dump into ui-artifacts/<test>/ when a test fails, and nothing at all when it passes. Each dump is independently guarded so a wedged app still yields the log."
```

---

### Task 4: `wait_idle`

**Files:**
- Modify: `include/ui_update_queue.h` (add `pending_count()`)
- Modify: `include/http_executor.h`, `src/system/http_executor.cpp` (add `inflight()`)
- Modify: `include/remote_control_server.h`, `src/remote/remote_control_server.cpp`
- Modify: `src/remote/remote_client.cpp` (usage text)
- Modify: `tests/ui/helix/app.py`, `docs/devel/HELIXCTL.md`
- Test: `tests/ui/test_wait_idle.py`

**Interfaces:**
- Consumes: `HelixApp` (Task 2).
- Produces:
  - `size_t UpdateQueue::pending_count() const`
  - `size_t HttpExecutor::inflight() const noexcept`
  - RPC `wait_idle` → `{"idle": true, "waited_ms": <int>}`, or a JSON-RPC error whose message
    names the nonzero counters (update_queue, http). Animations are NOT counted — see the
    design spec's determinism section.
  - `HelixApp.wait_idle(timeout: float = 10.0) -> dict`

- [ ] **Step 1: Write the failing test**

Create `tests/ui/test_wait_idle.py`:

```python
"""wait_idle must actually gate on async work, not just return immediately."""

import pytest

from helix.app import HelixCtlError


def test_wait_idle_returns_idle_on_a_quiet_screen(helix_app):
    helix_app.navigate("home")
    result = helix_app.wait_idle()
    assert result["idle"] is True
    assert result["waited_ms"] >= 0


def test_wait_idle_after_navigation_makes_the_panel_queryable(helix_app):
    # Without wait_idle this is the classic race: navigate returns as soon as
    # the panel is created, before its subjects populate.
    helix_app.navigate("controls")
    helix_app.wait_idle()
    listing = helix_app.ls()
    names = {w["name"] for w in listing["widgets"] if w.get("name")}
    assert "btn_motion" in names


def test_wait_idle_reports_which_counter_was_busy_on_timeout(helix_app):
    # A 0ms timeout cannot succeed unless everything is already idle; drive
    # something first so at least one counter is nonzero.
    helix_app.navigate("print_select")
    with pytest.raises(HelixCtlError) as exc:
        helix_app.wait_idle(timeout=0.0)
    # The message must name a counter, not just say "timeout" — a bare timeout
    # costs an hour of guessing which subsystem was busy.
    assert any(k in exc.value.message
               for k in ("update_queue", "animations", "http")), exc.value.message
```

Note on the `ls` shape: confirm whether the `describe_screen` result nests entries under
`"widgets"` before finalizing this assertion — run `helix-screen ctl --json ls | jq 'keys'`
against a live instance and adjust the key if it differs.

- [ ] **Step 2: Run to verify it fails**

Run: `python3 -m pytest tests/ui/test_wait_idle.py -v`
Expected: FAIL — `HelixApp` has no `wait_idle`.

- [ ] **Step 3: Expose the counters**

In `include/ui_update_queue.h`, as a public method alongside the other accessors:

```cpp
    /**
     * @brief Number of callbacks waiting to run, including frozen ones.
     *
     * Frozen work counts: a ScopedFreeze buffers rather than drops, so those
     * callbacks will fire on a later tick and the UI is not yet settled.
     */
    size_t pending_count() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return pending_.size() + frozen_buffer_.size();
    }
```

`mutex_` must be `mutable` for this to compile from a `const` method. Change its declaration:

```cpp
    mutable std::mutex mutex_;
```

In `include/http_executor.h`, add to the public section and the private state:

```cpp
    /// Items submitted but not yet finished, across queue and workers.
    std::size_t inflight() const noexcept { return state_->inflight.load(); }
```

and inside `struct SharedState`:

```cpp
        std::atomic<std::size_t> inflight{0};
```

In `src/system/http_executor.cpp`, increment in `submit` at the point the work is pushed onto
the queue, and decrement in the worker loop after the work returns — use a scope guard so a
throwing job still decrements:

```cpp
    state_->inflight.fetch_add(1, std::memory_order_relaxed);
```

and in the worker, wrapping the invocation:

```cpp
        struct InflightGuard {
            std::atomic<std::size_t>& c;
            ~InflightGuard() { c.fetch_sub(1, std::memory_order_relaxed); }
        } guard{state_->inflight};
```

- [ ] **Step 4: Add the handler**

In `include/remote_control_server.h`, declare alongside the other handlers:

```cpp
    nlohmann::json handle_wait_idle(const nlohmann::json& params);
```

In `src/remote/remote_control_server.cpp`, register it in `register_default_handlers()`:

```cpp
    handlers_["wait_idle"] = [this](const nlohmann::json& p) { return handle_wait_idle(p); };
```

and implement it. Note this must **not** block the UI thread — it polls it:

```cpp
nlohmann::json RemoteControlServer::handle_wait_idle(const nlohmann::json& params) {
    // Default generous enough for a gcode preview to land, short enough that a
    // wedged test fails inside a coffee break.
    double timeout_s = params.value("timeout", 10.0);

    // Sampled on the UI thread; compared on this (transport) thread. Polling
    // rather than blocking is deliberate — a handler that spun on the UI thread
    // would prevent the very work it is waiting for from running.
    struct Counters {
        size_t queue = 0;
        size_t anims = 0;
        size_t http = 0;
        bool idle() const { return queue == 0 && anims == 0 && http == 0; }
    };

    auto sample = [this]() -> Counters {
        auto j = execute_on_ui_thread([]() -> nlohmann::json {
            return {{"queue", helix::ui::UpdateQueue::instance().pending_count()},
                    {"anims", static_cast<size_t>(lv_anim_count_running())},
                    {"http", helix::http::HttpExecutor::fast().inflight() +
                                 helix::http::HttpExecutor::slow().inflight()}};
        });
        return Counters{j["queue"].get<size_t>(), j["anims"].get<size_t>(),
                        j["http"].get<size_t>()};
    };

    const auto start = std::chrono::steady_clock::now();
    const auto deadline = start + std::chrono::duration<double>(timeout_s);
    Counters last{1, 1, 1}; // force at least two samples before declaring idle

    while (true) {
        Counters now = sample();
        // Require idle on two consecutive samples: a single zero reading can
        // land in the gap between one callback finishing and the next being
        // enqueued by the work it just completed.
        if (now.idle() && last.idle()) {
            auto waited = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start);
            return {{"idle", true}, {"waited_ms", waited.count()}};
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            throw std::runtime_error(
                "wait_idle timed out after " + std::to_string(timeout_s) +
                "s — update_queue=" + std::to_string(now.queue) +
                " animations=" + std::to_string(now.anims) +
                " http=" + std::to_string(now.http));
        }
        last = now;
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
}
```

Add includes at the top of the file if absent: `<chrono>`, `<thread>`, `"http_executor.h"`,
`"ui_update_queue.h"`.

Add the token to the client's `build_request_from_tokens` so `--timeout N` maps to the
`timeout` param, following the existing `wait_for` precedent in `remote_client.cpp:306`.

- [ ] **Step 5: Add the harness method**

In `tests/ui/helix/app.py`, alongside the other wrappers:

```python
    def wait_idle(self, timeout: float = 10.0) -> dict:
        """Block until the UI has settled.

        Best-effort: raw lv_async_call work, the gcode/thumbnail build threads,
        and the mock backends' own threads are invisible to this. See the design
        spec's determinism section.
        """
        return self.ctl("wait_idle", "--timeout", timeout)
```

- [ ] **Step 6: Run the tests**

Run: `make -j && python3 -m pytest tests/ui/test_wait_idle.py -v`
Expected: all 3 PASS.

- [ ] **Step 7: Mutation-verify**

Change the handler's idle condition from `now.idle() && last.idle()` to `true`, rebuild, re-run.
Expected: `test_wait_idle_reports_which_counter_was_busy_on_timeout` FAILS (it now returns
success instead of raising). Revert and confirm green. If it still passes, the test is not
reaching the code and must be strengthened before moving on.

- [ ] **Step 8: Un-xfail Task 2's test**

If `test_descend_into_an_overlay_and_back` was marked xfail in Task 2, remove the marker and
insert `helix_app.wait_idle()` after the click. Run it.

- [ ] **Step 9: Document**

In `docs/devel/HELIXCTL.md`, add `wait_idle` to the "Diagnostics & lifecycle" table and a short
subsection naming what it cannot see (copy the source table from the design spec's determinism
section — the enumeration gaps are the thing a user needs warned about).

- [ ] **Step 10: Commit**

```bash
git add include/ui_update_queue.h include/http_executor.h src/system/http_executor.cpp \
        include/remote_control_server.h src/remote/remote_control_server.cpp \
        src/remote/remote_client.cpp tests/ui/ docs/devel/HELIXCTL.md
git commit -m "feat(helixctl): add wait_idle for async settling" \
  -m "Polls UpdateQueue pending, lv_anim_count_running, and HttpExecutor in-flight from the transport thread until all three read zero on two consecutive samples, so tests can gate on async work instead of sleeping. Two samples because a single zero can land between one callback finishing and the next being enqueued by it. Timeout errors name the nonzero counter rather than saying only that time ran out."
```

---

### Task 5: `freeze` / `unfreeze`

**Files:**
- Modify: `include/remote_control_server.h`, `src/remote/remote_control_server.cpp`
- Modify: `tests/ui/helix/app.py`, `docs/devel/HELIXCTL.md`
- Test: `tests/ui/test_freeze.py`

**Interfaces:**
- Consumes: `wait_idle` (Task 4).
- Produces: RPC `freeze` → `{"frozen": true, "timers_paused": <int>}`, RPC `unfreeze` →
  `{"frozen": false, "timers_resumed": <int>}`; `HelixApp.freeze() -> dict`,
  `HelixApp.unfreeze() -> dict`.

**CRITICAL — do not use `lv_timer_enable(false)`.** `UpdateQueue`'s processor is itself an
`lv_timer` (`include/ui_update_queue.h:108`) and `execute_on_ui_thread` dispatches through
`helix::ui::queue_update`. A global timer disable stops the update queue, so every later
command — `unfreeze` included — blocks for the 10s UI-thread timeout and throws. Freeze would
brick the channel it arrived on. Pause timers individually with a skip list instead.

- [ ] **Step 1: Write the failing test**

Create `tests/ui/test_freeze.py`:

```python
"""freeze must stop motion without severing the control channel."""


def test_freeze_then_unfreeze_keeps_the_channel_alive(helix_app):
    # The regression this guards: a global lv_timer_enable(false) stops the
    # UpdateQueue timer that execute_on_ui_thread depends on, so every command
    # after freeze times out — including unfreeze.
    helix_app.freeze()
    try:
        assert helix_app.ctl("ping") == "pong"
        assert helix_app.current()["panel"] is not None
    finally:
        helix_app.unfreeze()
    assert helix_app.ctl("ping") == "pong"


def test_freeze_reports_pausing_at_least_one_timer(helix_app):
    helix_app.navigate("home")
    helix_app.wait_idle()
    result = helix_app.freeze()
    try:
        assert result["frozen"] is True
        assert result["timers_paused"] >= 1
    finally:
        helix_app.unfreeze()


def test_unfreeze_resumes_exactly_what_freeze_paused(helix_app):
    frozen = helix_app.freeze()
    thawed = helix_app.unfreeze()
    assert thawed["timers_resumed"] == frozen["timers_paused"]


def test_screen_is_still_captured_while_frozen(helix_app, tmp_path):
    # Guards the second skip-list entry: pausing the display refresh timer
    # would leave nothing rendering.
    helix_app.navigate("home")
    helix_app.wait_idle()
    helix_app.freeze()
    try:
        out = helix_app.screenshot(str(tmp_path / "frozen.png"))
        assert (tmp_path / "frozen.png").stat().st_size > 0, out
    finally:
        helix_app.unfreeze()
```

- [ ] **Step 2: Run to verify it fails**

Run: `python3 -m pytest tests/ui/test_freeze.py -v`
Expected: FAIL — no `freeze` method.

- [ ] **Step 3: Implement the handlers**

In `include/remote_control_server.h`:

```cpp
    nlohmann::json handle_freeze(const nlohmann::json& params);
    nlohmann::json handle_unfreeze(const nlohmann::json& params);

    /// Timers paused by `freeze`, so `unfreeze` resumes exactly that set and
    /// never resumes one that was already paused by its owner.
    std::vector<lv_timer_t*> paused_timers_;
```

In `src/remote/remote_control_server.cpp`, register both:

```cpp
    handlers_["freeze"] = [this](const nlohmann::json& p) { return handle_freeze(p); };
    handlers_["unfreeze"] = [this](const nlohmann::json& p) { return handle_unfreeze(p); };
```

```cpp
nlohmann::json RemoteControlServer::handle_freeze(const nlohmann::json& /*params*/) {
    return execute_on_ui_thread([this]() -> nlohmann::json {
        // Stop animations already in flight, then prevent new ones. The setting
        // is honored at ~51 call sites, so this is the supported lever.
        lv_anim_delete_all();
        DisplaySettingsManager::instance().set_animations_enabled(false);

        // Pause periodic timers one at a time. A global lv_timer_enable(false)
        // would stop the UpdateQueue processor that this very handler was
        // dispatched through, wedging the control channel.
        lv_timer_t* const queue_timer = helix::ui::UpdateQueue::instance().timer();
        lv_timer_t* const refr_timer = lv_display_get_refr_timer(lv_display_get_default());

        paused_timers_.clear();
        lv_timer_t* t = lv_timer_get_next(nullptr);
        while (t) {
            lv_timer_t* next = lv_timer_get_next(t);
            const bool skip = (t == queue_timer) || (t == refr_timer);
            // lv_timer_pause on an already-paused timer is a no-op, but resuming
            // it later would be wrong — only track what we actually changed.
            if (!skip && !lv_timer_get_paused(t)) {
                lv_timer_pause(t);
                paused_timers_.push_back(t);
            }
            t = next;
        }

        spdlog::debug("[RemoteControl] freeze: paused {} timers", paused_timers_.size());
        return {{"frozen", true},
                {"timers_paused", static_cast<int>(paused_timers_.size())}};
    });
}

nlohmann::json RemoteControlServer::handle_unfreeze(const nlohmann::json& /*params*/) {
    return execute_on_ui_thread([this]() -> nlohmann::json {
        int resumed = 0;
        for (lv_timer_t* t : paused_timers_) {
            // A timer may have been deleted while frozen (panel teardown), so
            // confirm it is still in LVGL's list before touching it.
            for (lv_timer_t* live = lv_timer_get_next(nullptr); live;
                 live = lv_timer_get_next(live)) {
                if (live == t) {
                    lv_timer_resume(t);
                    resumed++;
                    break;
                }
            }
        }
        paused_timers_.clear();
        DisplaySettingsManager::instance().set_animations_enabled(true);
        spdlog::debug("[RemoteControl] unfreeze: resumed {} timers", resumed);
        return {{"frozen", false}, {"timers_resumed", resumed}};
    });
}
```

`UpdateQueue` needs a public accessor for its timer. Add to `include/ui_update_queue.h`:

```cpp
    /// The processing timer, so callers that pause timers en masse can skip it.
    /// Pausing it stops all queued UI work, including remote-control dispatch.
    lv_timer_t* timer() const { return timer_; }
```

Add `#include "display_settings_manager.h"` to the server's includes if absent.

**Note on `test_unfreeze_resumes_exactly_what_freeze_paused`:** if a timer is deleted between
freeze and unfreeze the counts legitimately differ. If that test proves flaky, narrow it to
`thawed["timers_resumed"] <= frozen["timers_paused"]` and add a comment saying why — do not
delete the assertion.

- [ ] **Step 4: Add harness methods**

```python
    def freeze(self) -> dict:
        """Stop animations and periodic timers for deterministic capture."""
        return self.ctl("freeze")

    def unfreeze(self) -> dict:
        return self.ctl("unfreeze")
```

- [ ] **Step 5: Run the tests**

Run: `make -j && python3 -m pytest tests/ui/test_freeze.py -v`
Expected: all 4 PASS.

- [ ] **Step 6: Verify the deadlock guard is real**

Temporarily replace the per-timer loop with `lv_timer_enable(false)`, rebuild, and run
`test_freeze_then_unfreeze_keeps_the_channel_alive`.
Expected: it FAILS with a UI-thread timeout — confirming the test actually guards the defect.
Revert.

- [ ] **Step 7: Document + commit**

Add `freeze`/`unfreeze` to the HELIXCTL.md "Diagnostics & lifecycle" table with a sentence on
the skip list.

```bash
git add include/remote_control_server.h src/remote/remote_control_server.cpp \
        include/ui_update_queue.h tests/ui/ docs/devel/HELIXCTL.md
git commit -m "feat(helixctl): add freeze/unfreeze for deterministic capture" \
  -m "Stops running animations, disables new ones via the existing DisplaySettingsManager setting, and pauses periodic timers individually. The global lv_timer_enable(false) is deliberately avoided: UpdateQueue's processor is itself an lv_timer and execute_on_ui_thread dispatches through it, so a global disable would wedge the control channel including unfreeze. The display refresh timer is skipped too, or nothing would render."
```

---

### Task 6: `text` — read label content

**Files:**
- Modify: `include/remote_control_server.h`, `src/remote/remote_control_server.cpp`
- Modify: `tests/ui/helix/app.py`, `docs/devel/HELIXCTL.md`
- Test: `tests/ui/test_text.py`

**Interfaces:**
- Consumes: `HelixApp` (Task 2).
- Produces: RPC `text` → `{"text": "<string>", "path": "<@path>", "source": "label|textarea|dropdown"}`;
  `HelixApp.text(target: str) -> str`.

Today the server's only text extraction is `lv_textarea_get_text` at
`remote_control_server.cpp:1359`. Labels — the most common assertion target — are unreadable.

- [ ] **Step 1: Write the failing test**

Create `tests/ui/test_text.py`:

```python
"""Reading label text — the most common assertion in any UI test."""

import pytest

from helix.app import HelixCtlError


def test_reads_a_label_on_the_home_panel(helix_app):
    helix_app.navigate("home")
    helix_app.wait_idle()
    # Find any label with non-empty text and confirm `text` agrees with `ls`.
    listing = helix_app.ls()
    labeled = [w for w in listing["widgets"]
               if w.get("name") and w.get("type", "").endswith("label")]
    assert labeled, "no named labels on the home panel — pick a different anchor"
    value = helix_app.text(labeled[0]["name"])
    assert isinstance(value, str)


def test_descends_to_the_label_inside_a_composite(helix_app):
    # A button wrapping a label should report the label's text rather than
    # failing, mirroring how click() descends to a value-control.
    helix_app.navigate("controls")
    helix_app.wait_idle()
    value = helix_app.text("btn_motion")
    assert value.strip(), f"btn_motion reported empty text: {value!r}"


def test_unknown_target_raises(helix_app):
    with pytest.raises(HelixCtlError):
        helix_app.text("definitely_not_a_widget")


def test_widget_with_no_text_raises_rather_than_returning_empty(helix_app):
    # An empty string and "this widget has no text" are different facts, and
    # conflating them makes an assertion silently vacuous.
    helix_app.navigate("home")
    helix_app.wait_idle()
    listing = helix_app.ls()
    containers = [w for w in listing["widgets"]
                  if w.get("name") and w.get("type") == "lv_obj"]
    if not containers:
        pytest.skip("no plain container on screen to test against")
    with pytest.raises(HelixCtlError):
        helix_app.text(containers[0]["name"])
```

- [ ] **Step 2: Run to verify it fails**

Run: `python3 -m pytest tests/ui/test_text.py -v`
Expected: FAIL — no `text` method.

- [ ] **Step 3: Implement the handler**

Register `handlers_["text"]` and implement, reusing the existing target resolution helper that
`handle_click` uses (find it in `remote_control_server.cpp` — it is the function that turns a
name/glob/@path into an `lv_obj_t*` and already handles topmost-visible resolution):

```cpp
namespace {

/// Read text out of whichever widget type carries it. Returns false when the
/// widget has no text concept at all — distinct from having empty text.
bool read_widget_text(lv_obj_t* o, std::string& out, std::string& source) {
    if (lv_obj_check_type(o, &lv_label_class)) {
        const char* t = lv_label_get_text(o);
        out = t ? t : "";
        source = "label";
        return true;
    }
    if (lv_obj_check_type(o, &lv_textarea_class)) {
        const char* t = lv_textarea_get_text(o);
        out = t ? t : "";
        source = "textarea";
        return true;
    }
    if (lv_obj_check_type(o, &lv_dropdown_class)) {
        char buf[128];
        lv_dropdown_get_selected_str(o, buf, sizeof(buf));
        out = buf;
        source = "dropdown";
        return true;
    }
    return false;
}

/// Depth-first search for the first descendant carrying text. Mirrors how
/// click() descends into a composite row to find its value-control.
lv_obj_t* find_text_descendant(lv_obj_t* root) {
    uint32_t n = lv_obj_get_child_count(root);
    for (uint32_t i = 0; i < n; i++) {
        lv_obj_t* child = lv_obj_get_child(root, i);
        std::string ignored_text, ignored_source;
        if (read_widget_text(child, ignored_text, ignored_source)) {
            return child;
        }
        if (lv_obj_t* deeper = find_text_descendant(child)) {
            return deeper;
        }
    }
    return nullptr;
}

} // namespace

nlohmann::json RemoteControlServer::handle_text(const nlohmann::json& params) {
    std::string target = params.value("target", "");
    if (target.empty()) {
        throw std::runtime_error("text requires a target");
    }
    return execute_on_ui_thread([this, target]() -> nlohmann::json {
        lv_obj_t* widget = resolve_target(target); // existing helper used by handle_click
        if (!widget) {
            throw std::runtime_error("No widget matching: " + target);
        }
        std::string value, source;
        lv_obj_t* holder = widget;
        if (!read_widget_text(holder, value, source)) {
            holder = find_text_descendant(widget);
            if (!holder || !read_widget_text(holder, value, source)) {
                throw std::runtime_error("Widget has no text: " + target);
            }
        }
        return {{"text", value},
                {"path", widget_path(holder)}, // existing helper that builds @s/... paths
                {"source", source}};
    });
}
```

If `resolve_target` and `widget_path` have different names in the file, use the actual ones —
find them by reading `handle_click` and the `describe_screen` entry builder around line 1364.

- [ ] **Step 4: Add the harness method**

```python
    def text(self, target: str) -> str:
        """Read a widget's text. Raises if the widget carries no text at all."""
        return self.ctl("text", target)["text"]
```

- [ ] **Step 5: Run the tests**

Run: `make -j && python3 -m pytest tests/ui/test_text.py -v`
Expected: all PASS (one may skip).

- [ ] **Step 6: Mutation-verify**

Change `throw std::runtime_error("Widget has no text: ...")` to `return {{"text", ""}, ...}`.
Re-run.
Expected: `test_widget_with_no_text_raises_rather_than_returning_empty` FAILS. Revert.

- [ ] **Step 7: Document + commit**

Add `text <target>` to the HELIXCTL.md "Introspection & widget interaction" table.

```bash
git add include/remote_control_server.h src/remote/remote_control_server.cpp \
        tests/ui/ docs/devel/HELIXCTL.md
git commit -m "feat(helixctl): add text command to read label content" \
  -m "Labels were the one thing ls could not report — it carries value for switches, sliders, dropdowns and textareas, but nothing for the labels that most assertions actually target. text resolves a target the same way click does, descends into a composite to find the first text-carrying descendant, and raises when a widget has no text concept rather than returning an empty string, since those are different facts."
```

---

### Task 7: `reset`

**Files:**
- Modify: `include/remote_control_server.h`, `src/remote/remote_control_server.cpp`
- Modify: `tests/ui/helix/app.py`, `tests/ui/conftest.py`, `docs/devel/HELIXCTL.md`
- Test: `tests/ui/test_reset.py`

**Interfaces:**
- Consumes: `wait_idle` (Task 4), `HelixApp` (Task 2).
- Produces: RPC `reset` → `{"panel": "home", "overlays_popped": <int>, "modals_cleared": <int>}`;
  `HelixApp.reset() -> dict`; autouse fixture `clean_screen`.

- [ ] **Step 1: Write the failing test**

Create `tests/ui/test_reset.py`:

```python
"""reset returns the app to a known state so one instance can serve many tests."""


def test_reset_from_a_nested_overlay_lands_on_home(helix_app):
    helix_app.navigate("controls")
    helix_app.click("btn_motion")
    helix_app.wait_idle()
    assert helix_app.current()["overlays"], "setup failed — no overlay pushed"

    result = helix_app.reset()
    assert result["panel"] == "home"
    assert result["overlays_popped"] >= 1

    state = helix_app.current()
    assert state["panel"] == "home"
    assert state["overlays"] == []


def test_reset_is_idempotent(helix_app):
    helix_app.reset()
    second = helix_app.reset()
    assert second["panel"] == "home"
    assert second["overlays_popped"] == 0


def test_reset_dismisses_a_modal(helix_app):
    helix_app.ctl("demo", "runout-modal")
    helix_app.wait_idle()
    helix_app.reset()
    # A lingering modal swallows input while every later command still reports
    # success — the exact failure the design doc warns about.
    assert helix_app.current()["overlays"] == []
    helix_app.navigate("settings")
    assert helix_app.current()["panel"] == "settings"
```

- [ ] **Step 2: Run to verify it fails**

Run: `python3 -m pytest tests/ui/test_reset.py -v`
Expected: FAIL — no `reset` method.

- [ ] **Step 3: Implement the handler**

Register `handlers_["reset"]`. Implementation walks the navigation stack down and clears the
modal stack, then navigates home:

```cpp
nlohmann::json RemoteControlServer::handle_reset(const nlohmann::json& /*params*/) {
    return execute_on_ui_thread([]() -> nlohmann::json {
        int modals = static_cast<int>(ModalStack::instance().depth());
        ModalStack::instance().clear();

        auto& nav = NavigationManager::instance();
        int popped = 0;
        // Bounded rather than while(true): a nav stack that will not drain is a
        // bug, and spinning forever on the UI thread turns it into a hang.
        constexpr int kMaxDepth = 32;
        while (nav.overlay_depth() > 0 && popped < kMaxDepth) {
            nav.go_back();
            popped++;
        }
        nav.navigate_to(PanelId::HOME);

        return {{"panel", "home"},
                {"overlays_popped", popped},
                {"modals_cleared", modals}};
    });
}
```

Check the real method names on `NavigationManager` and `ModalStack` before writing this —
`overlay_depth()`, `go_back()`, `navigate_to()`, `ModalStack::depth()`, and `clear()` are the
expected shapes but must be confirmed against `include/ui_nav_manager.h` and the ModalStack
header. Use whatever exists; do not add new methods to those classes for this.

Toasts: check whether `ToastManager` exposes a dismiss-all. If it does, call it and add a
`toasts_cleared` field. If it does not, leave toasts alone — do **not** add an API to
ToastManager as part of this task; note the gap in HELIXCTL.md and let Tier 2's `toasts`
command handle it.

- [ ] **Step 4: Add the harness method and autouse fixture**

In `app.py`:

```python
    def reset(self) -> dict:
        """Return to home with no overlays or modals. Cheaper than a reboot."""
        return self.ctl("reset")
```

In `conftest.py`:

```python
@pytest.fixture(autouse=True)
def clean_screen(request):
    """Reset to a known screen before each test that uses the shared instance.

    Autouse so a test that navigates somewhere cannot leak that state into the
    next one. Tests using `fresh_helix_app` get a new process and skip this.
    """
    if "helix_app" not in request.fixturenames:
        return
    app = request.getfixturevalue("helix_app")
    app.reset()
    app.wait_idle()
```

- [ ] **Step 5: Run the whole suite**

Run: `make -j && python3 -m pytest tests/ui/ -v`
Expected: all PASS, including earlier tasks' tests now running under the autouse reset.

- [ ] **Step 6: Verify isolation actually works**

Run the suite in reverse order: `python3 -m pytest tests/ui/ -p no:randomly --co -q` then
`python3 -m pytest tests/ui/ -v` twice in a row.
Expected: identical results both runs. Order-dependent passes mean `reset` is not resetting
something it should.

- [ ] **Step 7: Document + commit**

```bash
git add include/remote_control_server.h src/remote/remote_control_server.cpp \
        tests/ui/ docs/devel/HELIXCTL.md
git commit -m "feat(helixctl): add reset to return to a known screen" \
  -m "Pops the overlay stack, clears modals, and navigates home, so one app instance can serve a whole test session instead of paying a two-second boot per test. The pop loop is depth-bounded rather than unbounded: a nav stack that will not drain is a bug, and spinning on the UI thread would turn it into a hang."
```

---

### Task 8: Capture refactor + `screenshot --stable --target`

**Files:**
- Modify: `include/screenshot.h`, `src/system/screenshot.cpp`
- Modify: `include/remote_control_server.h`, `src/remote/remote_control_server.cpp:handle_screenshot`
- Modify: `tests/ui/helix/app.py`, `docs/devel/HELIXCTL.md`
- Test: `tests/ui/test_screenshot.py`

**Interfaces:**
- Consumes: `freeze` (Task 5), `wait_idle` (Task 4).
- Produces:
  - `helix::CapturedFrame { std::vector<uint8_t> rgba; int width; int height; }`
  - `bool helix::capture_frame(CapturedFrame& out, lv_obj_t* crop_to = nullptr)`
  - `uint64_t helix::frame_hash(const CapturedFrame&)`
  - RPC `screenshot` gains optional params `stable` (bool) and `target` (string); result adds
    `"stable_frames"` and `"w"`/`"h"`
  - `HelixApp.screenshot(path, target=None, stable=False) -> str` and
    `HelixApp.capture(target=None, stable=True) -> PIL.Image.Image`

The existing `save_screenshot` (`src/system/screenshot.cpp:138`) already does the hard part:
`lv_snapshot_take` on the active screen, then alpha-compositing the top layer over it. That
logic must be reused, not duplicated — extract it rather than writing a second compositor.

- [ ] **Step 1: Write the failing test**

Create `tests/ui/test_screenshot.py`:

```python
"""Stable capture and widget cropping."""

from PIL import Image


def test_stable_capture_is_reproducible_when_frozen(helix_app, tmp_path):
    helix_app.navigate("home")
    helix_app.wait_idle()
    helix_app.freeze()
    try:
        a = helix_app.capture(stable=True)
        b = helix_app.capture(stable=True)
    finally:
        helix_app.unfreeze()
    assert a.tobytes() == b.tobytes(), "two frozen captures of the same screen differ"


def test_target_crop_is_smaller_than_the_full_screen(helix_app):
    helix_app.navigate("controls")
    helix_app.wait_idle()
    helix_app.freeze()
    try:
        full = helix_app.capture(stable=True)
        cropped = helix_app.capture(target="btn_motion", stable=True)
    finally:
        helix_app.unfreeze()
    assert cropped.width < full.width or cropped.height < full.height
    assert cropped.width > 0 and cropped.height > 0


def test_crop_matches_the_widgets_reported_geometry(helix_app):
    helix_app.navigate("controls")
    helix_app.wait_idle()
    g = helix_app.geom("btn_motion")
    helix_app.freeze()
    try:
        cropped = helix_app.capture(target="btn_motion", stable=True)
    finally:
        helix_app.unfreeze()
    assert (cropped.width, cropped.height) == (g["w"], g["h"])


def test_png_path_still_writes_a_readable_file(helix_app, tmp_path):
    out = tmp_path / "shot.png"
    helix_app.screenshot(str(out))
    with Image.open(out) as img:
        assert img.width > 0 and img.height > 0
```

- [ ] **Step 2: Run to verify it fails**

Run: `python3 -m pytest tests/ui/test_screenshot.py -v`
Expected: FAIL — no `capture` method.

- [ ] **Step 3: Extract capture from encode**

In `include/screenshot.h`:

```cpp
/// A captured frame in RGBA8888, already composited (active screen + top layer).
struct CapturedFrame {
    std::vector<uint8_t> rgba;
    int width = 0;
    int height = 0;
};

/// Capture the current screen. When @p crop_to is non-null, the result is that
/// widget's bounding box rather than the whole screen.
bool capture_frame(CapturedFrame& out, lv_obj_t* crop_to = nullptr);

/// Order-independent-free hash of a frame's pixels, for stability comparison.
uint64_t frame_hash(const CapturedFrame& frame);
```

In `src/system/screenshot.cpp`, move the `lv_snapshot_take` + top-layer compositing body out of
`save_screenshot` into `capture_frame`, add the crop, and reduce `save_screenshot` to
`capture_frame` + `write_png`/`write_bmp`. The crop:

```cpp
    if (crop_to) {
        lv_area_t a;
        lv_obj_get_coords(crop_to, &a);
        // Clamp to the captured buffer — a widget can extend past the screen
        // edge, and a partially-offscreen crop must not read out of bounds.
        int x0 = std::max<int>(0, a.x1);
        int y0 = std::max<int>(0, a.y1);
        int x1 = std::min<int>(out.width, a.x2 + 1);
        int y1 = std::min<int>(out.height, a.y2 + 1);
        if (x1 <= x0 || y1 <= y0) {
            spdlog::error("[Screenshot] Crop region is empty");
            return false;
        }
        // ... row-copy into a new buffer, then swap into `out` ...
    }
```

`frame_hash` is FNV-1a over `out.rgba`:

```cpp
uint64_t frame_hash(const CapturedFrame& frame) {
    uint64_t h = 1469598103934665603ULL;
    for (uint8_t b : frame.rgba) {
        h ^= b;
        h *= 1099511628211ULL;
    }
    return h;
}
```

- [ ] **Step 4: Extend the screenshot handler**

Replace `handle_screenshot`'s body so it resolves an optional target, and when `stable` is set,
polls until three consecutive frame hashes match:

```cpp
nlohmann::json RemoteControlServer::handle_screenshot(const nlohmann::json& params) {
    std::string out_path = params.value("path", "");
    std::string target = params.value("target", "");
    bool stable = params.value("stable", false);

    // Stability is sampled across frames, so this loop must live on the
    // transport thread and hop to the UI thread per sample.
    int stable_frames = 0;
    if (stable) {
        constexpr int kRequired = 3;
        constexpr int kMaxSamples = 180; // ~3s at 16ms
        uint64_t last = 0;
        int run = 0;
        for (int i = 0; i < kMaxSamples; i++) {
            uint64_t h = execute_on_ui_thread([&target]() -> nlohmann::json {
                lv_obj_t* crop = target.empty() ? nullptr : resolve_target(target);
                if (!target.empty() && !crop) {
                    throw std::runtime_error("No widget matching: " + target);
                }
                helix::CapturedFrame f;
                if (!helix::capture_frame(f, crop)) {
                    throw std::runtime_error("Frame capture failed");
                }
                return helix::frame_hash(f);
            }).get<uint64_t>();

            run = (i > 0 && h == last) ? run + 1 : 1;
            last = h;
            if (run >= kRequired) {
                stable_frames = run;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }
        if (stable_frames < kRequired) {
            throw std::runtime_error(
                "Screen never stabilized: no " + std::to_string(kRequired) +
                " identical consecutive frames within 3s. Try `freeze` first.");
        }
    }

    return execute_on_ui_thread([out_path, target, stable_frames]() -> nlohmann::json {
        lv_obj_t* crop = target.empty() ? nullptr : resolve_target(target);
        if (!target.empty() && !crop) {
            throw std::runtime_error("No widget matching: " + target);
        }
        helix::CapturedFrame f;
        if (!helix::capture_frame(f, crop)) {
            throw std::runtime_error("Frame capture failed");
        }
        std::string written = helix::write_frame(f, out_path);
        if (written.empty()) {
            throw std::runtime_error("Screenshot failed");
        }
        return {{"saved", true}, {"path", written},
                {"w", f.width}, {"h", f.height},
                {"stable_frames", stable_frames}};
    });
}
```

Add `helix::write_frame(const CapturedFrame&, const std::string& out_path) -> std::string` to
`screenshot.h/.cpp` — the path/suffix logic currently inline in `save_screenshot`, taking a
frame instead of capturing one. Keep `save_screenshot` as a thin wrapper so its three existing
callers in `application.cpp` are untouched.

Add the `--stable` and `--target` tokens to the client's `build_request_from_tokens`.

- [ ] **Step 5: Add harness methods**

```python
    def screenshot(self, path: str, target: str | None = None,
                   stable: bool = False) -> str:
        args = ["screenshot", path]
        if target:
            args += ["--target", target]
        if stable:
            args += ["--stable"]
        return self.ctl(*args)["path"]

    def capture(self, target: str | None = None, stable: bool = True):
        """Capture to a temp PNG and return it as a Pillow image."""
        from PIL import Image

        with tempfile.NamedTemporaryFile(suffix=".png", delete=False) as tmp:
            path = tmp.name
        try:
            self.screenshot(path, target=target, stable=stable)
            with Image.open(path) as img:
                return img.convert("RGBA").copy()
        finally:
            os.unlink(path)
```

Add `import tempfile` to `app.py`.

- [ ] **Step 6: Run the tests**

Run: `make -j && python3 -m pytest tests/ui/test_screenshot.py -v`
Expected: all 4 PASS.

- [ ] **Step 7: Verify the existing screenshot pipeline still works**

Run: `./scripts/screenshot.sh helix-screen scratch-home home --test`
Expected: a PNG is produced exactly as before. The refactor touched `save_screenshot`, which
that pipeline and three `application.cpp` callers depend on — a passing pytest suite does not
prove those still work.

- [ ] **Step 8: Commit**

```bash
git add include/screenshot.h src/system/screenshot.cpp \
        include/remote_control_server.h src/remote/remote_control_server.cpp \
        src/remote/remote_client.cpp tests/ui/ docs/devel/HELIXCTL.md
git commit -m "feat(helixctl): add stable and widget-cropped screenshot capture" \
  -m "Splits save_screenshot into capture_frame (snapshot + top-layer composite, now with an optional widget crop) and write_frame, so the server can hash frames without encoding them. --stable polls until three consecutive frame hashes match before capturing, which covers the async sources wait_idle cannot enumerate. --target crops to a widget's bounds so a golden only changes when that widget does."
```

---

### Task 9: Golden comparison

**Files:**
- Create: `tests/ui/helix/goldens.py`
- Modify: `tests/ui/conftest.py`
- Test: `tests/ui/test_goldens_unit.py`

**Interfaces:**
- Consumes: `HelixApp.capture` (Task 8).
- Produces:
  - `compare(actual: Image, golden_path: Path) -> ComparisonResult` where
    `ComparisonResult` has `.matches: bool`, `.reason: str`, `.diff: Image | None`
  - `assert_golden(actual: Image, name: str, *, goldens_dir: Path, artifacts_dir: Path, accept: bool) -> None`
  - pytest option `--accept-goldens`, fixture `golden` returning a callable
    `(image, name) -> None`

`goldens.py` must not import `app.py`. It compares images; it knows nothing about HelixScreen.

- [ ] **Step 1: Write the failing unit test**

Create `tests/ui/test_goldens_unit.py` — this tests the comparison logic with synthetic images,
so it needs no running app and stays fast:

```python
"""Unit tests for golden comparison. No app required."""

import pytest
from PIL import Image

from helix.goldens import GoldenMismatch, assert_golden, compare


def _solid(w, h, color):
    return Image.new("RGBA", (w, h), color)


def test_identical_images_match(tmp_path):
    golden = tmp_path / "g.png"
    _solid(4, 4, (10, 20, 30, 255)).save(golden)
    result = compare(_solid(4, 4, (10, 20, 30, 255)), golden)
    assert result.matches


def test_single_pixel_difference_fails(tmp_path):
    golden = tmp_path / "g.png"
    _solid(4, 4, (10, 20, 30, 255)).save(golden)
    actual = _solid(4, 4, (10, 20, 30, 255))
    actual.putpixel((0, 0), (11, 20, 30, 255))
    result = compare(actual, golden)
    assert not result.matches
    assert "1 pixel" in result.reason


def test_size_mismatch_reports_both_sizes(tmp_path):
    golden = tmp_path / "g.png"
    _solid(4, 4, (0, 0, 0, 255)).save(golden)
    result = compare(_solid(8, 4, (0, 0, 0, 255)), golden)
    assert not result.matches
    assert "8x4" in result.reason and "4x4" in result.reason


def test_missing_golden_raises_rather_than_creating_it(tmp_path):
    # A silently created golden asserts nothing on its first run.
    with pytest.raises(GoldenMismatch) as exc:
        assert_golden(_solid(4, 4, (0, 0, 0, 255)), "newthing",
                      goldens_dir=tmp_path / "goldens",
                      artifacts_dir=tmp_path / "artifacts",
                      accept=False)
    assert "--accept-goldens" in str(exc.value)
    assert not (tmp_path / "goldens" / "newthing.png").exists()


def test_accept_writes_the_golden(tmp_path):
    assert_golden(_solid(4, 4, (1, 2, 3, 255)), "newthing",
                  goldens_dir=tmp_path / "goldens",
                  artifacts_dir=tmp_path / "artifacts",
                  accept=True)
    assert (tmp_path / "goldens" / "newthing.png").exists()


def test_mismatch_writes_actual_and_diff_artifacts(tmp_path):
    golden_dir = tmp_path / "goldens"
    golden_dir.mkdir()
    _solid(4, 4, (0, 0, 0, 255)).save(golden_dir / "thing.png")
    with pytest.raises(GoldenMismatch):
        assert_golden(_solid(4, 4, (255, 255, 255, 255)), "thing",
                      goldens_dir=golden_dir,
                      artifacts_dir=tmp_path / "artifacts",
                      accept=False)
    assert (tmp_path / "artifacts" / "thing.actual.png").exists()
    assert (tmp_path / "artifacts" / "thing.diff.png").exists()
```

- [ ] **Step 2: Run to verify it fails**

Run: `python3 -m pytest tests/ui/test_goldens_unit.py -v`
Expected: FAIL — no `helix.goldens` module.

- [ ] **Step 3: Implement `goldens.py`**

```python
"""Golden-image comparison. Knows nothing about HelixScreen — just images."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import numpy as np
from PIL import Image


class GoldenMismatch(AssertionError):
    """An image did not match its golden, or no golden exists yet."""


@dataclass
class ComparisonResult:
    matches: bool
    reason: str
    diff: Image.Image | None = None


def compare(actual: Image.Image, golden_path: Path) -> ComparisonResult:
    """Exact pixel comparison. Freeze + stable capture is what makes this viable."""
    with Image.open(golden_path) as g:
        golden = g.convert("RGBA").copy()
    actual = actual.convert("RGBA")

    if actual.size != golden.size:
        return ComparisonResult(
            False,
            f"size differs: actual {actual.width}x{actual.height}, "
            f"golden {golden.width}x{golden.height}",
        )

    a = np.asarray(actual, dtype=np.int16)
    b = np.asarray(golden, dtype=np.int16)
    delta = np.abs(a - b)
    differing = int(np.count_nonzero(delta.any(axis=2)))
    if differing == 0:
        return ComparisonResult(True, "identical")

    total = actual.width * actual.height
    # Amplify so a 1-value channel drift is visible to a human opening the file.
    amplified = np.clip(delta.sum(axis=2) * 8, 0, 255).astype(np.uint8)
    diff_img = Image.fromarray(amplified, mode="L").convert("RGBA")
    return ComparisonResult(
        False,
        f"{differing} pixel{'s' if differing != 1 else ''} differ "
        f"({differing / total:.4%} of {total})",
        diff_img,
    )


def assert_golden(actual: Image.Image, name: str, *, goldens_dir: Path,
                  artifacts_dir: Path, accept: bool) -> None:
    """Compare against `<goldens_dir>/<name>.png`, or write it when accepting."""
    golden_path = Path(goldens_dir) / f"{name}.png"

    if accept:
        golden_path.parent.mkdir(parents=True, exist_ok=True)
        actual.save(golden_path)
        return

    if not golden_path.exists():
        # Deliberately not auto-created: a golden written on its first run
        # asserts nothing, and nobody ever goes back to review it.
        raise GoldenMismatch(
            f"No golden at {golden_path}. Review the screen, then create it with "
            f"`pytest --accept-goldens`."
        )

    result = compare(actual, golden_path)
    if result.matches:
        return

    artifacts_dir = Path(artifacts_dir)
    artifacts_dir.mkdir(parents=True, exist_ok=True)
    actual_path = artifacts_dir / f"{name}.actual.png"
    actual.save(actual_path)
    detail = f"  actual: {actual_path}"
    if result.diff is not None:
        diff_path = artifacts_dir / f"{name}.diff.png"
        result.diff.save(diff_path)
        detail += f"\n  diff:   {diff_path}"

    raise GoldenMismatch(
        f"{name} does not match its golden: {result.reason}\n"
        f"  golden: {golden_path}\n{detail}\n"
        f"If the change is intended, re-run with --accept-goldens."
    )
```

- [ ] **Step 4: Wire the pytest option and fixture**

Append to `tests/ui/conftest.py`:

```python
GOLDENS_DIR = Path(__file__).parent / "goldens"


def pytest_addoption(parser):
    parser.addoption(
        "--accept-goldens", action="store_true", default=False,
        help="Overwrite golden images with the current rendering. Review the diff first.",
    )


@pytest.fixture
def golden(request):
    """Assert an image matches its golden. Name defaults to the test's name."""
    from helix.goldens import assert_golden

    accept = request.config.getoption("--accept-goldens")

    def _check(image, name: str | None = None) -> None:
        assert_golden(image, name or request.node.name,
                      goldens_dir=GOLDENS_DIR,
                      artifacts_dir=ARTIFACT_ROOT / request.node.name,
                      accept=accept)

    return _check
```

- [ ] **Step 5: Run the tests**

Run: `python3 -m pytest tests/ui/test_goldens_unit.py -v`
Expected: all 6 PASS.

- [ ] **Step 6: Commit**

```bash
git add tests/ui/helix/goldens.py tests/ui/conftest.py tests/ui/test_goldens_unit.py
git commit -m "test(ui): add golden-image comparison with explicit acceptance" \
  -m "Exact pixel comparison, viable because freeze plus stable capture removes the motion. A missing golden fails rather than being created, since a golden written on its first run asserts nothing and never gets reviewed; --accept-goldens is the only way to write one. Mismatches write the actual image plus an amplified diff so a one-value channel drift is visible when you open it."
```

---

### Task 10: Seed the golden corpus

**Files:**
- Create: `tests/ui/test_screens.py`, `tests/ui/goldens/*.png`
- Modify: `docs/devel/UI_TESTING.md`

**Interfaces:**
- Consumes: everything above.
- Produces: a parameterized screen-capture suite and its committed goldens.

Seed from the tokens already in `scripts/screenshot-recipes.sh` — that navigation is written
and debugged, so the corpus starts from known-reachable screens rather than guesses.

- [ ] **Step 1: Enumerate the reachable screens**

Run: `bash -c 'grep -oE "^\s+[a-z0-9-]+\)" scripts/screenshot-recipes.sh'`

If that yields nothing (the recipe file may use a different dispatch shape), read
`scripts/screenshot-recipes.sh` directly and list the tokens it accepts. Record the list — it
is the input to Step 2.

- [ ] **Step 2: Write the parameterized test**

Create `tests/ui/test_screens.py`. Replace `SCREENS` with the tokens found in Step 1, keeping
each entry's navigation as explicit harness calls rather than shelling to the recipe script —
the recipes are a shell dispatch and re-implementing the three or four steps per screen is
clearer than invoking bash from pytest:

```python
"""Golden captures for each reachable screen."""

import pytest

# (golden name, navigation steps). Steps are (method, *args) tuples applied in
# order. Seeded from scripts/screenshot-recipes.sh, which already knows how to
# reach each of these.
SCREENS = [
    ("home", [("navigate", "home")]),
    ("controls", [("navigate", "controls")]),
    ("settings", [("navigate", "settings")]),
    ("motion", [("navigate", "controls"), ("click", "btn_motion")]),
    ("print_select", [("navigate", "print_select")]),
    ("print_status", [("ctl", "demo", "print-status")]),
    ("ams", [("ctl", "demo", "ams")]),
    ("preflight", [("ctl", "demo", "preflight-check")]),
]


@pytest.mark.parametrize("name,steps", SCREENS, ids=[s[0] for s in SCREENS])
def test_screen_matches_golden(helix_app, golden, name, steps):
    for method, *args in steps:
        getattr(helix_app, method)(*args)
    helix_app.wait_idle()
    helix_app.freeze()
    try:
        image = helix_app.capture(stable=True)
    finally:
        helix_app.unfreeze()
    golden(image, name)
```

- [ ] **Step 3: Run without goldens to confirm it fails loudly**

Run: `python3 -m pytest tests/ui/test_screens.py -v`
Expected: every case FAILS with "No golden at ... create it with `pytest --accept-goldens`".
This confirms the fail-on-missing rule is doing its job.

- [ ] **Step 4: Generate candidate goldens into a scratch directory — do NOT commit them**

**Golden acceptance is a human gate. You must not accept goldens yourself.** A golden
generated from a broken screen locks that breakage in permanently and every later run passes
against it, so the person who knows what these screens should look like has to approve them.

Generate candidates outside the repo so nothing can be committed by accident:

```bash
HELIX_UI_ARTIFACTS=/tmp/golden-candidates \
  ./.venv/bin/python -m pytest tests/ui/test_screens.py --accept-goldens \
  --override-ini "addopts=" -q
```

If `--accept-goldens` writes into `tests/ui/goldens/` regardless (it does — `GOLDENS_DIR` is
fixed in `conftest.py`), then instead: run it, immediately `mv tests/ui/goldens
/tmp/golden-candidates`, and confirm `git status --short tests/ui/goldens` shows nothing
staged or untracked.

- [ ] **Step 5: Verify capture is actually deterministic**

With the candidates restored to `tests/ui/goldens/` **in your working tree only** (still
unstaged), run the suite twice:

```bash
./.venv/bin/python -m pytest tests/ui/test_screens.py -v
./.venv/bin/python -m pytest tests/ui/test_screens.py -v
```

Expected: all PASS both runs. A second run that fails means capture is not deterministic and
the freeze/stable path needs work — report that as a concern rather than proceeding.

- [ ] **Step 6: Verify the goldens are load-bearing**

Open one golden, change a handful of pixels with Pillow, and re-run that single test.
Expected: FAIL, with `.actual.png` and `.diff.png` written into `ui-artifacts/`. Restore it
from the candidate directory afterward.

- [ ] **Step 6b: Hand the candidates off — stop here**

Commit **only** `test_screens.py` and the docs (Step 7), with the goldens left uncommitted in
`/tmp/golden-candidates/`. In your report, list every candidate PNG with its full path and
pixel dimensions, and note anything that looked wrong to you. Report status
DONE_WITH_CONCERNS and state plainly that the goldens await human review. Do **not** `git add
tests/ui/goldens`.

- [ ] **Step 7: Document the harness**

In `docs/devel/UI_TESTING.md`, add a section above the existing headless-LVGL content:

```markdown
## Out-of-process UI tests (`tests/ui/`)

The Catch2 tests below build widgets inside the test process. The pytest suite in
`tests/ui/` instead drives a real running instance through `helix-screen ctl --json`,
so it covers app lifecycle, navigation, and async population — including panels that
have no safe in-process lifetime.

    make -j                                    # the harness needs the binary
    python3 -m pytest tests/ui/ -v
    python3 -m pytest tests/ui/ --accept-goldens   # after reviewing a diff

Failures write a screenshot, the app's log tail, and a screen-state dump to
`ui-artifacts/<test-name>/`.

Golden images are **local-only** for now. They are sensitive to renderer and font
rasterization, so a golden captured on a desktop will not match a CI runner; see the
design spec's Risks section.
```

- [ ] **Step 8: Commit**

Goldens are deliberately excluded from this commit — see Step 6b.

```bash
git add tests/ui/test_screens.py docs/devel/UI_TESTING.md
git commit -m "test(ui): seed the golden corpus from the screenshot recipes" \
  -m "Parameterized captures for the screens scripts/screenshot-recipes.sh already knows how to reach, each frozen and stable-captured before comparison. Goldens live in tests/ui/goldens/ and are deliberately separate from docs/images/: doc screenshots should change whenever the UI improves, regression goldens should not, and merging the two meanings trains you to accept every diff."
```

---

## Self-Review

**Spec coverage:**

| Spec section | Task |
|---|---|
| `--json` | 1 |
| `wait_idle` | 4 |
| `freeze`/`unfreeze` + skip list | 5 |
| `screenshot --stable --target` | 8 |
| `text` | 6 |
| `reset` | 7 |
| Harness layout, `HelixApp` | 2 |
| Failure diagnostics | 3 |
| Golden mechanics (exact, fail-on-missing, `--accept-goldens`, actual+diff artifacts) | 9 |
| Initial corpus from recipes | 10 |
| Frame-hash gate | 8 |
| Local-only goldens / portability risk | 10 (documented) |
| Tier 2 (`press`, `toasts`, `set_clock`) | **Deliberately out of scope** — noted in File Structure |
| Tier 3 (socket transport, `AsyncWorkRegistry`) | **Deliberately out of scope** |
| Size/theme golden variants | **Gap** — the spec calls for them as pytest params; add once the base corpus is stable, since parameterizing before a single-variant corpus is green multiplies the review burden. Tracked here rather than silently dropped. |

**Known verification points where the plan tells the implementer to check reality first:**
- The `ls` result key (`"widgets"`) in Tasks 4, 6 — confirm against a live `ctl --json ls`.
- `resolve_target` / `widget_path` helper names in Tasks 6, 8 — read `handle_click`.
- `NavigationManager` / `ModalStack` method names in Task 7.
- Whether `ToastManager` has a dismiss-all in Task 7 (do not add one).

These are named rather than guessed because a plan that invents an API produces code that
compiles against nothing.
