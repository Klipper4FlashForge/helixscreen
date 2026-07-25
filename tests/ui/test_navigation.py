# SPDX-License-Identifier: GPL-3.0-or-later

"""Navigation smoke tests — the automated replacement for test-navigation.sh."""

import pytest


def test_app_responds_to_ping(helix_app):
    assert helix_app.ctl("ping") == "pong"


def test_lists_the_expected_base_panels(helix_app):
    panels = helix_app.ctl("list_panels")["panels"]
    # list_panels returns the fixed PanelId set; these four are load-bearing
    # enough that losing one is a regression worth failing on.
    for expected in ("home", "controls", "settings", "print-select"):
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
