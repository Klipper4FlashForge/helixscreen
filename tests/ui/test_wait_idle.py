# SPDX-License-Identifier: GPL-3.0-or-later

"""wait_idle must actually gate on async work, not just return immediately."""

import re

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
    # With timeout=0.0 the raise itself is unconditional: `last` is seeded
    # non-idle so the two-consecutive-samples rule can never pass on the
    # first (only) sample, and the deadline has already elapsed by the time
    # it's checked. That makes `pytest.raises` alone a tautology — it would
    # pass even if wait_idle always embedded a hardcoded "update_queue=0" in
    # its message. The property actually worth testing is that the embedded
    # counter is a REAL nonzero reading, not just present-looking text.
    #
    # Applying a scenario pushes a burst of subject changes through
    # deferred observe_int_sync/observe_string callbacks, which land in
    # UpdateQueue fresh — but that shows up as nonzero for at most one UI
    # tick (~16ms), raced against this process's own subprocess-spawn +
    # socket round trip. Empirically that race is won the vast majority of
    # the time (~95%+ in manual testing) but not deterministically, so retry
    # a few times with alternating scenarios (guaranteeing a real state
    # transition, not a same-value no-op, on every attempt) rather than
    # accept a single flaky sample.
    counter_pattern = re.compile(r"(update_queue|http)=[1-9]\d*")
    scenarios = ("printing", "idle")
    last_message = None
    try:
        for attempt in range(5):
            helix_app.ctl("scenario", scenarios[attempt % 2])
            with pytest.raises(HelixCtlError) as exc:
                helix_app.wait_idle(timeout=0.0)
            last_message = exc.value.message
            if counter_pattern.search(last_message):
                return
        pytest.fail(
            "wait_idle never reported a genuinely nonzero counter across 5 "
            f"attempts (last message: {last_message!r}) — either this harness "
            "can't reliably win the sub-tick race, or the counter logic "
            "regressed to always reporting zero"
        )
    finally:
        # `reset()` (the autouse clean_screen fixture) deliberately doesn't
        # touch mock-scenario state — it's a home/overlay/modal reset, not a
        # printer-state reset. Without this, a run that returns on the
        # "printing" attempt leaves the shared session mid-mock-print for
        # whichever test happens to run next, with nothing but alphabetical
        # file ordering keeping that invisible.
        helix_app.ctl("scenario", "idle")
