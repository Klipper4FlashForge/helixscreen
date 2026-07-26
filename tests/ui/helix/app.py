# SPDX-License-Identifier: GPL-3.0-or-later

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
        # Default to SDL's headless driver: a visible window steals focus and
        # swallows the developer's keystrokes every time a test spawns an app,
        # and a suite run spawns many. Verified that dummy renders normally —
        # navigate and screenshot both work and produce correct pixels, so the
        # golden-image tests are unaffected.
        #
        # Explicitly exporting SDL_VIDEODRIVER still wins, so a visible
        # instance remains one env var away when you want to watch one. In
        # that case honour the Wayland rule from scripts/screenshot.sh:
        # XWayland's GLX path crashes, so a Wayland session needs SDL's
        # native driver rather than the default.
        if not env.get("SDL_VIDEODRIVER"):
            env["SDL_VIDEODRIVER"] = "dummy"
        headless = env["SDL_VIDEODRIVER"] == "dummy"
        if not headless and env.get("WAYLAND_DISPLAY"):
            env["SDL_VIDEODRIVER"] = "wayland"
        display_index = "0" if env.get("WAYLAND_DISPLAY") else "1"

        # Application::acquire_instance_lock() flocks a lock file resolved
        # from HELIX_CONFIG_DIR (default: "config", relative to whatever the
        # process's CWD happens to be) — taken unconditionally, even under
        # --test, before Config::init() ever runs (so it can't rely on that
        # to create the directory). Without an override every HelixApp
        # spawned from the same CWD (the shared instance, a fresh_helix_app,
        # and test_diagnostics.py's pytester sub-process) shares one lock
        # file and only one can ever hold it. socket_path already lives under
        # a private tmp_path per instance, so anchor the config dir there too
        # — it's unique for free — and create it ourselves; open(O_CREAT)
        # makes the lock file but not its parent directory.
        if not env.get("HELIX_CONFIG_DIR"):
            config_dir = self.socket_path.parent / "helix-config"
            config_dir.mkdir(parents=True, exist_ok=True)
            env["HELIX_CONFIG_DIR"] = str(config_dir)

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
        try:
            self._await_ready()
        except Exception:
            # __enter__ raising skips __exit__, so a boot that never answers
            # ping would otherwise leak this process — kill it ourselves.
            if self.proc is not None and self.proc.poll() is None:
                self.proc.kill()
                self.proc.wait(timeout=5)
            raise
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
            if hasattr(self, "log_file"):
                self.log_file.close()
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
                self.proc.wait(timeout=5)
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
            except json.JSONDecodeError:
                err = None

            if err is not None:
                raise HelixCtlError(err.get("message", stderr),
                                    int(err.get("code", -1)), command)

            # Client-side usage error, or the app died. Distinguish them,
            # because "no instance at socket" and "you typo'd" need
            # different reactions from whoever reads the failure.
            if self.proc is not None and self.proc.poll() is not None:
                raise HelixAppError(
                    f"helix-screen died (exit {self.proc.returncode})\n"
                    f"{self._log_tail_from_file()}"
                )
            raise HelixCtlError(stderr or "ctl failed", -1, command)

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

    def text(self, target: str) -> str:
        """Read a widget's text. Raises if the widget carries no text at all."""
        return self.ctl("text", target)["text"]

    def get(self, subject: str) -> Any:
        return self.ctl("get", subject)

    def set(self, subject: str, value: Any) -> dict:
        return self.ctl("set", subject, value)

    def wait_for(self, subject: str, value: Any, timeout: float = 30.0) -> dict:
        """Block until a subject's value equals `value` (exact match), or raise on timeout.

        Event-driven: the server attaches an LVGL subject observer rather than
        polling, so this returns the instant the value changes to match —
        faster than a fixed sleep in the common case, and immune to the flake
        a fixed margin has under load.
        """
        return self.ctl("wait_for", subject, value, "--timeout", timeout)

    def wait_idle(self, timeout: float = 10.0) -> dict:
        """Block until the UI has settled.

        Best-effort: raw lv_async_call work, the gcode/thumbnail build threads,
        and the mock backends' own threads are invisible to this. See the design
        spec's determinism section.
        """
        return self.ctl("wait_idle", "--timeout", timeout)

    def freeze(self) -> dict:
        """Stop animations and pause periodic timers for deterministic capture.

        Skips the UpdateQueue processor and display-refresh timers so the
        control channel keeps working and screenshots still render — see
        docs/devel/HELIXCTL.md "Diagnostics & lifecycle".
        """
        return self.ctl("freeze")

    def unfreeze(self) -> dict:
        """Reverse freeze(): resume the timers it paused, re-enable animations."""
        return self.ctl("unfreeze")

    def current(self) -> dict:
        # CLI token is `current`; `get_current` is the wire method name.
        return self.ctl("current")

    def reset(self) -> dict:
        """Return to home with no overlays or modals. Cheaper than a reboot."""
        return self.ctl("reset")

    def log(self, n: int = 50) -> list[str]:
        result = self.ctl("log", "-n", n)
        return result.get("lines", []) if isinstance(result, dict) else []

    def screenshot(self, path: str) -> str:
        return self.ctl("screenshot", path)["path"]

    def shutdown(self) -> None:
        self.stop()
