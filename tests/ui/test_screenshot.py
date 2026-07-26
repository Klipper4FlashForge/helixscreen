# SPDX-License-Identifier: GPL-3.0-or-later

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
    g = helix_app.geom("btn_motion")["widgets"][0]
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
