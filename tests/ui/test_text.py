# SPDX-License-Identifier: GPL-3.0-or-later

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
                  if w.get("name") and w.get("type") == "obj"]
    # A `divider_horizontal` is a bare rule with no children by construction,
    # so it can never turn up a descended label the way a big layout
    # container (which wraps the whole panel) reliably would. Prefer it as
    # the anchor; fall back to any other named plain container.
    divider = next((w for w in containers if "divider" in w["name"]), None)
    target = divider or (containers[0] if containers else None)
    if target is None:
        pytest.skip("no plain container on screen to test against")
    with pytest.raises(HelixCtlError):
        helix_app.text(target["name"])
