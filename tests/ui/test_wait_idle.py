# SPDX-License-Identifier: GPL-3.0-or-later

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
    helix_app.navigate("print-select")
    with pytest.raises(HelixCtlError) as exc:
        helix_app.wait_idle(timeout=0.0)
    # The message must name a counter, not just say "timeout" — a bare timeout
    # costs an hour of guessing which subsystem was busy.
    assert any(k in exc.value.message
               for k in ("update_queue", "animations", "http")), exc.value.message
