# SPDX-License-Identifier: GPL-3.0-or-later

"""Golden captures for each reachable screen.

Screen tokens and their navigation are sourced from `scripts/screenshot-recipes.sh`
(the `SCREENSHOT_RECIPE` table) rather than re-transcribed here, so this corpus
can't drift from the table `screenshot.sh`/`screenshot-all.sh` already use to
reach each screen. This isn't really "parsing bash from Python": bash itself
sources the file and dumps its own associative array, so the only thing done
here in Python is splitting `"navigate x; click y"` into steps. That's more
robust than hand-rolling a parser for bash's quoting/comment syntax, and it
means a renamed or added recipe token shows up here automatically instead of
needing a second edit that someone eventually forgets to make.

Overlay/panel transitions must render instantly, not animate, or `freeze()`
can catch one mid-slide (see `_SUBSET`'s comment below for the details this
corpus depends on). That used to require a local `settings_animations_enabled`
override in this file; it's now guaranteed by `HelixApp.start()` itself
(`helix/app.py` seeds each instance's private config dir from
`config/settings-test.json` before boot), so no per-test workaround remains
here — if animations ever come back on by default, the right fix is back in
`helix/app.py`, not a re-added fixture in this file.
"""

from __future__ import annotations

import shlex
import subprocess
from pathlib import Path

import pytest

REPO_ROOT = Path(__file__).resolve().parents[2]
RECIPES_SCRIPT = REPO_ROOT / "scripts" / "screenshot-recipes.sh"


def _load_recipes() -> dict[str, str]:
    """Source screenshot-recipes.sh and dump its SCREENSHOT_RECIPE table."""
    script = (
        f"source {shlex.quote(str(RECIPES_SCRIPT))}; "
        'for k in "${!SCREENSHOT_RECIPE[@]}"; do '
        'printf "%s\\t%s\\n" "$k" "${SCREENSHOT_RECIPE[$k]}"; done'
    )
    result = subprocess.run(["bash", "-c", script], capture_output=True, text=True,
                            check=True, cwd=REPO_ROOT)
    recipes: dict[str, str] = {}
    for line in result.stdout.splitlines():
        token, _, recipe = line.partition("\t")
        recipes[token] = recipe
    return recipes


def _steps_for(recipe: str) -> list[tuple]:
    """Turn 'navigate controls; click btn_motion' into ctl-call step tuples."""
    steps = []
    for clause in recipe.split(";"):
        parts = clause.split()
        if parts:
            steps.append(("ctl", *parts))
    return steps


_RECIPES = _load_recipes()

# Subset of scripts/screenshot-recipes.sh's ~38 tokens. Chosen to prove the
# mechanism on a spread that's actually stable, not just plausible-looking:
# every one of these was verified byte-identical across at least 6 independent
# app boots (not just 3 quick frames within one capture) before being kept.
#
# Deliberately left out of THIS pass — not dropped silently, see the task-10
# report for full evidence — because they carry content that `freeze()`
# cannot pin down:
#
#   - `home`, `controls`, `filament`, `fan`: the mock backend's
#     `simulation_thread_` (moonraker_client_mock.cpp) drifts nozzle/bed/
#     chamber temps and the motor-idle timer on its own raw background
#     thread, invisible to `freeze()` (which only pauses LVGL timers/
#     animations) and to `wait_idle()` (which only tracks UpdateQueue/
#     HttpExecutor). `fan` looked stable in quick back-to-back checks but
#     failed across independent boots once — it's `card_cooling` on the same
#     Controls panel, not a separate overlay, so it shows the same
#     temperature card. `filament` additionally renders a usage chart with a
#     real-wall-clock x-axis (e.g. "9:40 PM"). This is exactly the gap the
#     design spec's `wait_idle` source table already names ("Mock backends
#     ... Mock mode adds nondeterminism") — a real hole in the determinism
#     story for any screen with a live numeric readout, not a bug in this
#     test.
#   - `console`: the gcode console echoes lines stamped with the real
#     wall-clock time the mock print ran, so its content is never the same
#     twice.
#   - `preflight-check`: the modal's dim backdrop is the Home panel, which
#     inherits the same temperature-jitter problem faintly through the scrim.
#   - `camera`: the "Connecting Camera..." state's spinner animates via its
#     own always-running `lv_anim` (independent of `settings_animations_enabled`,
#     the same category of issue the design spec flags for the print-select
#     loading spinner), so `freeze()` catches it at a different arc position
#     each time — confirmed as a small (~15px) but real diff across runs.
#   - `print-select`: was in this list and briefly golden'd, then pulled after
#     discovering it isn't safe under normal full-suite execution.
#     `UsbBackendMock::start()` (usb_backend_mock.cpp) spawns a background
#     thread that inserts a demo USB drive exactly 1.5s after boot — a fixed
#     delay, not open-ended jitter, but one with no subject to `wait_for`:
#     `PrintSelectUsbSource::on_drive_inserted()` shows the Printer/USB source
#     selector row via a bare `lv_obj_remove_flag(..., LV_OBJ_FLAG_HIDDEN)`,
#     invisible to both `wait_idle()` and `freeze()`. A capture taken before
#     1.5s has elapsed since boot shows the plain file grid (no selector row,
#     content starts higher); one taken after shows the full selector row
#     (content shifted down) with correct full-color thumbnails. Isolated runs
#     of just this file reliably land before the 1.5s mark; a full `pytest
#     tests/ui/` run reliably lands after it (other files' setup alone burns
#     >1.5s). Confirmed with matching log lines ("Source selector configured
#     (hidden until USB drive inserted)" at boot, "USB drive inserted -
#     showing source selector" ~1.5s later) and byte-for-byte comparison of
#     both states. Needs a real decision (wait on a fixed, bounded delay vs.
#     exposing a subject vs. deferring) before it can be golden'd again.
#
# Kept: every base panel except the temp-bearing ones above, a representative
# handful of overlays reached through their real click handlers (each a
# full-screen replacement with no backdrop bleed-through), and the one `demo`
# screen (`ams`) that renders no live telemetry.
_SUBSET = [
    "settings", "advanced",
    "motion", "bed-mesh", "zoffset", "macros",
    "ams",
]

SCREENS = [(name, _steps_for(_RECIPES[name])) for name in _SUBSET]


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
