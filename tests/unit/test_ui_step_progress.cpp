// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_ui_step_progress.cpp
 * @brief Rendering tests for the step-progress widget's connector lines.
 *
 * The vertical connector lines between step circles are absolutely-positioned
 * IGNORE_LAYOUT rects sized from a layout pass. When the widget is created while
 * an ancestor is hidden, the layout is degenerate (0-size) and the connectors
 * used to be skipped permanently. These tests pin the two contracts:
 *   1. connectors exist with a positive extent when created while visible, and
 *   2. connectors are (re)created + sized once the widget becomes visible, even
 *      if it was built while hidden — driven by an LV_EVENT_SIZE_CHANGED reflow.
 */

#include "ui_step_progress.h"

#include "lvgl/lvgl.h"

#include "../lvgl_test_fixture.h"

#include "../catch_amalgamated.hpp"

#include <array>

namespace {

// A 4-step vertical model → 3 connectors between the circles.
std::array<ui_step_t, 4> make_vertical_steps() {
    return {{
        {"Heat nozzle", helix::StepState::Pending},
        {"Feed filament", helix::StepState::Pending},
        {"Purge", helix::StepState::Pending},
        {"Clean", helix::StepState::Pending},
    }};
}

lv_obj_t* make_sized_parent(lv_obj_t* screen) {
    lv_obj_t* parent = lv_obj_create(screen);
    lv_obj_set_size(parent, 300, 400);
    lv_obj_set_style_pad_all(parent, 0, 0);
    lv_obj_set_style_border_width(parent, 0, 0);
    return parent;
}

} // namespace

TEST_CASE_METHOD(LVGLTestFixture, "step progress creates vertical connectors when visible",
                 "[ui][step]") {
    lv_obj_t* parent = make_sized_parent(test_screen());

    auto steps = make_vertical_steps();
    lv_obj_t* widget =
        ui_step_progress_create(parent, steps.data(), static_cast<int>(steps.size()),
                                /*horizontal=*/false, nullptr);
    REQUIRE(widget != nullptr);

    lv_obj_update_layout(parent);

    for (int i = 0; i < static_cast<int>(steps.size()) - 1; i++) {
        lv_obj_t* conn = ui_step_progress_test_get_connector(widget, i);
        INFO("connector " << i);
        REQUIRE(conn != nullptr);
        CHECK(lv_obj_get_height(conn) > 0);
    }
}

TEST_CASE_METHOD(LVGLTestFixture, "step progress recomputes connectors on reflow (SIZE_CHANGED)",
                 "[ui][step]") {
    // Connectors are absolutely-positioned rects sized from the layout. When the
    // widget reflows — a becoming-visible container or a growing live-temp label
    // shifting the circles — the connectors must be recomputed, not frozen at
    // their create-time geometry. A horizontal bar makes this deterministic: the
    // step items flex-grow to fill the width, so circle spacing (and therefore
    // connector width) tracks the container width directly.
    lv_obj_t* parent = make_sized_parent(test_screen());
    lv_obj_set_width(parent, 600);

    auto steps = make_vertical_steps(); // labels/states reused; horizontal layout
    lv_obj_t* widget =
        ui_step_progress_create(parent, steps.data(), static_cast<int>(steps.size()),
                                /*horizontal=*/true, nullptr);
    REQUIRE(widget != nullptr);
    lv_obj_update_layout(parent);

    lv_obj_t* conn0_wide = ui_step_progress_test_get_connector(widget, 0);
    REQUIRE(conn0_wide != nullptr);
    const int32_t wide_w = lv_obj_get_width(conn0_wide);
    CHECK(wide_w > 0);

    // Halve the available width and drive a reflow, as an unhide/resize would.
    lv_obj_set_width(parent, 300);
    lv_obj_update_layout(parent);
    lv_obj_send_event(widget, LV_EVENT_SIZE_CHANGED, nullptr);
    lv_obj_update_layout(parent);

    lv_obj_t* conn0_narrow = ui_step_progress_test_get_connector(widget, 0);
    REQUIRE(conn0_narrow != nullptr);
    const int32_t narrow_w = lv_obj_get_width(conn0_narrow);

    INFO("wide_w=" << wide_w << " narrow_w=" << narrow_w);
    // A recomputed connector must shrink with the container; a frozen one keeps
    // its create-time width and overshoots the narrower layout.
    CHECK(narrow_w < wide_w);
    CHECK(narrow_w > 0);
}
