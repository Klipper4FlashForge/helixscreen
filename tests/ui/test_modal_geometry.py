# SPDX-License-Identifier: GPL-3.0-or-later

"""Modal height budgets, measured through `ctl geom` on a real instance.

Klipper reports shutdown reasons as arbitrarily long text. The recovery card had
no height cap at any level, so a long reason grew it past both screen edges and
clipped its title and buttons off a 480x272 panel. These tests pin the card and
its last button inside the screen at three sizes, with a message long enough to
fill the 512-byte `recovery_message` subject buffer.

Geometry comes from `ctl geom`, which reports absolute screen coords. Measuring
the same thing in-process is a trap: `lv_obj_get_x/y()` is *parent-relative*, so
comparing a nested button's offset against an ancestor's screen-space edge
compares two coordinate spaces and passes regardless of the layout.

Mutation checks (measured 2026-08-14, not assumed):
  - raise the message container's style_max_height above #dialog_content_max
    -> the container stops scrolling and test_long_message_scrolls fails.
       This is the load-bearing guard.
  - dropping style_max_height from the *card* fails nothing here. Worth knowing
    why: recovery_message_buf_ is 512 bytes, which caps the rendered text at
    roughly 150px, and the restructured chrome (icon and title on one row, the
    two restarts in a modal_button_row) leaves enough room that 480x272 fits
    even uncapped. The card cap is a backstop for content that outgrows that —
    longer translations, larger fonts — not for anything this test can produce.

  - restore the pre-fix layout wholesale (`git show d44686c45:` this dialog — an
    lg icon on its own row above three full-width stacked buttons)
    -> test_recovery_card_fits fails at 480x272 AND 800x480, passes at 1024x600.
       That is the originally reported bug, and it is a small-screen bug.

The two assertions catch different failure modes, and neither subsumes the other:

  test_recovery_card_fits_the_screen   card grows past the screen (chrome too tall)
  test_dismiss_button_stays_inside     card pinned AT its cap, contents overflow it

The second does NOT fail on the pre-fix layout: there the card and its buttons
run off the screen together, so the button is still within the card's own bounds.
It exists for the case that actually bit during this fix — a card clamped at
max_height whose last child lands past the clamped edge, unreachable because the
card is scrollable=false. That was 4px at 85%, which is why the card is at 90%.
"""

from __future__ import annotations

import os
from pathlib import Path

import pytest

from helix.app import HelixApp

_BINARY = Path(os.environ.get(
    "HELIX_UI_BINARY",
    str(Path(__file__).resolve().parents[2] / "build" / "bin" / "helix-screen")))

# Sized to fill recovery_message_buf_[512] (511 usable), so the message container
# is pegged at its cap and the card is at its worst case. Measured: at 480x272 a
# 340-char message still fits the container without scrolling, so a shorter string
# here would assert nothing about the scroll path.
_LONG_REASON = (
    "Internal error during ready callback: No active exception to reraise. "
    "MCU shutdown: Timer too close. This often indicates the host computer is "
    "overloaded. Check for other processes consuming CPU. Once the underlying "
    "issue is corrected, use the FIRMWARE_RESTART command to reset the firmware, "
    "reload the config and restart the host software. Check the host for CPU "
    "contention from other services, verify no USB device is resetting, and "
    "review klippy.log around the shutdown timestamp for the first error."
)
assert 480 <= len(_LONG_REASON) <= 511, (
    f"message must fill the 512-byte subject buffer without being truncated "
    f"mid-test; got {len(_LONG_REASON)}")

# 480x272 is the smallest panel we ship (Qidi Q2, AD5M). The other two cover the
# breakpoints where #dialog_content_max steps up.
_SIZES = ["480x272", "800x480", "1024x600"]


def _geom(app: HelixApp, target: str) -> dict:
    """First widget geom record for `target`, or None when it is not on screen."""
    result = app.geom(target)
    widgets = result.get("widgets") if isinstance(result, dict) else None
    return widgets[0] if widgets else None


@pytest.fixture
def shutdown_app(request, tmp_path):
    """An instance at a given screen size, sitting on the recovery dialog."""
    size = request.param
    if not _BINARY.exists():
        pytest.skip(f"{_BINARY} not built — run `make -j`")

    before = os.environ.get("HELIX_SCREEN_SIZE")
    os.environ["HELIX_SCREEN_SIZE"] = size
    try:
        app = HelixApp(binary=_BINARY, socket_path=tmp_path / "control.sock",
                       log_path=tmp_path / "app.log")
        with app:
            # "error" puts klippy into an error state, which is what raises the
            # unified recovery dialog.
            app.ctl("scenario", "error")
            app.wait_idle()
            assert _geom(app, "klipper_recovery_card") is not None, (
                "recovery dialog never appeared; scenario 'error' may have changed")
            app.set("recovery_message", _LONG_REASON)
            app.wait_idle()
            yield app, size
    finally:
        if before is None:
            os.environ.pop("HELIX_SCREEN_SIZE", None)
        else:
            os.environ["HELIX_SCREEN_SIZE"] = before


@pytest.mark.parametrize("shutdown_app", _SIZES, indirect=True)
def test_recovery_card_fits_the_screen(shutdown_app):
    app, size = shutdown_app
    screen_h = int(size.split("x")[1])

    card = _geom(app, "klipper_recovery_card")
    assert card is not None
    top, bottom = card["y"], card["y"] + card["h"]
    assert top >= 0, f"{size}: card starts above the screen at y={top}"
    assert bottom <= screen_h, (
        f"{size}: card runs {bottom - screen_h}px past the bottom "
        f"(y={top} h={card['h']})")


@pytest.mark.parametrize("shutdown_app", _SIZES, indirect=True)
def test_dismiss_button_stays_inside_the_card(shutdown_app):
    """The last child is what a capped card clips first.

    The card is `scrollable=false`, so anything past its bottom edge is not
    merely off-view, it is unreachable — the user cannot dismiss the dialog.
    """
    app, size = shutdown_app

    card = _geom(app, "klipper_recovery_card")
    dismiss = _geom(app, "recovery_dismiss_btn")
    assert card is not None and dismiss is not None

    card_bottom = card["y"] + card["h"]
    dismiss_bottom = dismiss["y"] + dismiss["h"]
    assert dismiss_bottom <= card_bottom, (
        f"{size}: Dismiss runs {dismiss_bottom - card_bottom}px past the card "
        f"(card y={card['y']} h={card['h']}, button y={dismiss['y']} h={dismiss['h']})")


@pytest.mark.parametrize("shutdown_app", _SIZES[:1], indirect=True)
def test_long_message_scrolls_instead_of_growing_the_card(shutdown_app):
    """A capped container with no scroll range is an inert fix, not a fix.

    Only checked at 480x272: the larger breakpoints raise #dialog_content_max
    faster than the message grows, so there the text legitimately fits.
    """
    app, _ = shutdown_app

    scroll = _geom(app, "recovery_message_scroll")
    assert scroll is not None, "message container is missing its name"
    assert scroll["scrollable"] is True

    overflow = scroll["scroll"]["bottom"] + scroll["scroll"]["top"]
    assert overflow > 0, (
        "message container reports no scroll range, so the cap is truncating "
        f"content rather than making it reachable (h={scroll['h']}, "
        f"content_h={scroll['content_h']})")
