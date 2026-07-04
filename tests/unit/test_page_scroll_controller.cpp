// SPDX-License-Identifier: GPL-3.0-or-later
#include "../lvgl_ui_test_fixture.h"
#include "page_scroll_controller.h"

#include "../catch_amalgamated.hpp"

using namespace helix::ui;

namespace {
// Build a column-flex scrollable container with `n` fixed-height rows.
lv_obj_t* make_scroll_container(lv_obj_t* parent, int n, int32_t h) {
    lv_obj_t* c = lv_obj_create(parent);
    lv_obj_set_size(c, 400, 200);
    lv_obj_add_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    for (int i = 0; i < n; ++i) {
        lv_obj_t* row = lv_obj_create(c);
        lv_obj_set_size(row, lv_pct(100), h);
    }
    lv_obj_update_layout(c);
    return c;
}
} // namespace

TEST_CASE_METHOD(LVGLUITestFixture, "PageScrollController attaches gutter to overflowing container",
                 "[page_scroll_buttons][ui]") {
    lv_obj_t* c = make_scroll_container(test_screen(), 20, 60); // 1200px >> 200px
    REQUIRE(lv_obj_get_scroll_bottom(c) > 0);

    PageScrollController ctl;
    REQUIRE(ctl.attach(c));
    CHECK(ctl.gutter() != nullptr);
    CHECK(ctl.up_button() != nullptr);
    CHECK(ctl.down_button() != nullptr);

    lv_obj_update_layout(c);
    // Content reflowed narrower than the container's inner width.
    CHECK(lv_obj_get_content_width(c) < 400);
    // Native scrollbar suppressed while the gutter is active.
    CHECK(lv_obj_get_scrollbar_mode(c) == LV_SCROLLBAR_MODE_OFF);
}

TEST_CASE_METHOD(LVGLUITestFixture, "PageScrollController disables up at top and pages down",
                 "[page_scroll_buttons][ui]") {
    lv_obj_t* c = make_scroll_container(test_screen(), 20, 60);
    PageScrollController ctl;
    REQUIRE(ctl.attach(c));

    // At top: up disabled, down enabled.
    CHECK(lv_obj_has_state(ctl.up_button(), LV_STATE_DISABLED));
    CHECK_FALSE(lv_obj_has_state(ctl.down_button(), LV_STATE_DISABLED));

    int32_t before = lv_obj_get_scroll_y(c);
    ctl.page_down(); // animated=off internally for determinism? see impl
    lv_obj_update_layout(c);
    CHECK(lv_obj_get_scroll_y(c) > before);

    // Scroll to the very bottom: down disabled.
    lv_obj_scroll_to_y(c, lv_obj_get_scroll_bottom(c) + lv_obj_get_scroll_y(c), LV_ANIM_OFF);
    ctl.refresh_reach_state();
    CHECK(lv_obj_has_state(ctl.down_button(), LV_STATE_DISABLED));
    CHECK_FALSE(lv_obj_has_state(ctl.up_button(), LV_STATE_DISABLED));
}

TEST_CASE_METHOD(LVGLUITestFixture, "PageScrollController hides gutter when content fits",
                 "[page_scroll_buttons][ui]") {
    lv_obj_t* c = make_scroll_container(test_screen(), 1, 40); // 40px << 200px, no overflow
    // LVGL doesn't clamp lv_obj_get_scroll_bottom() to 0 when content is smaller
    // than the viewport — it goes negative (see lv_obj_scroll_by_bounded's own
    // `if (scroll_max < 0) scroll_max = 0;` clamp). <=0 is the real "no room
    // below" invariant, matching page_scroll_math.h's documented contract.
    REQUIRE(lv_obj_get_scroll_bottom(c) <= 0);

    PageScrollController ctl;
    REQUIRE(ctl.attach(c));
    CHECK(lv_obj_has_flag(ctl.gutter(), LV_OBJ_FLAG_HIDDEN));
    // No reserved padding while hidden — content keeps full width.
    CHECK(lv_obj_get_style_pad_right(c, LV_PART_MAIN) == 0);
}

TEST_CASE_METHOD(LVGLUITestFixture, "PageScrollController restores container on detach",
                 "[page_scroll_buttons][ui]") {
    lv_obj_t* c = make_scroll_container(test_screen(), 20, 60);
    lv_scrollbar_mode_t original = lv_obj_get_scrollbar_mode(c);

    PageScrollController ctl;
    REQUIRE(ctl.attach(c));
    ctl.detach();
    process_lvgl(20); // flush lv_obj_delete_async

    CHECK(ctl.gutter() == nullptr);
    CHECK(lv_obj_get_style_pad_right(c, LV_PART_MAIN) == 0);
    CHECK(lv_obj_get_scrollbar_mode(c) == original);
}
