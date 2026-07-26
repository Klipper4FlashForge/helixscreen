# SPDX-License-Identifier: GPL-3.0-or-later

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


def test_freeze_twice_does_not_lose_track_of_the_paused_set(helix_app):
    # A naive re-scan on a second freeze() would find every timer already
    # paused, track none of them as newly-paused, and so overwrite the
    # server's bookkeeping with an empty set — orphaning the originally
    # paused timers, which unfreeze() could then never resume.
    first = helix_app.freeze()
    try:
        second = helix_app.freeze()
        assert second["frozen"] is True
        assert second["timers_paused"] == first["timers_paused"]
    finally:
        thawed = helix_app.unfreeze()
    assert thawed["timers_resumed"] == first["timers_paused"]


def test_unfreeze_without_freeze_is_a_noop(helix_app):
    # A defensive `finally: unfreeze()` (as several tests here use) must not
    # raise or misbehave if freeze() itself never ran.
    result = helix_app.unfreeze()
    assert result == {"frozen": False, "timers_resumed": 0}


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
