# SPDX-License-Identifier: GPL-3.0-or-later

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
