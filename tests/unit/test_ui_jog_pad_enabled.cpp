// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_ui_jog_pad_enabled.cpp
 * @brief Tests for ui_jog_pad_set_enabled().
 *
 * The jog pad is a custom-drawn widget with no XML disabled binding. When the
 * printer is not ready, ui_jog_pad_set_enabled(pad, false) adds LV_STATE_DISABLED
 * so LVGL's input handling stops routing presses/clicks to the pad (verified in
 * lv_indev.c, which gates delivery on !lv_obj_has_state(obj, LV_STATE_DISABLED))
 * and the draw callback overlays a dimming scrim. This exercises that toggle.
 */

#include "../../include/ui_jog_pad.h"
#include "../lvgl_test_fixture.h"

#include "../catch_amalgamated.hpp"

TEST_CASE_METHOD(LVGLTestFixture, "ui_jog_pad_set_enabled toggles LV_STATE_DISABLED",
                 "[jog_pad][ui]") {
    lv_obj_t* pad = ui_jog_pad_create(lv_screen_active());
    REQUIRE(pad != nullptr);

    // Created enabled: input flows to the pad normally.
    CHECK_FALSE(lv_obj_has_state(pad, LV_STATE_DISABLED));

    // Disable: LV_STATE_DISABLED set -> indev skips press/click, scrim drawn.
    ui_jog_pad_set_enabled(pad, false);
    CHECK(lv_obj_has_state(pad, LV_STATE_DISABLED));

    // Re-enable: state cleared.
    ui_jog_pad_set_enabled(pad, true);
    CHECK_FALSE(lv_obj_has_state(pad, LV_STATE_DISABLED));

    // Idempotent: re-enabling an already-enabled pad is a no-op, not a crash.
    ui_jog_pad_set_enabled(pad, true);
    CHECK_FALSE(lv_obj_has_state(pad, LV_STATE_DISABLED));

    // Null-safe.
    ui_jog_pad_set_enabled(nullptr, false);

    lv_obj_delete(pad);
}
