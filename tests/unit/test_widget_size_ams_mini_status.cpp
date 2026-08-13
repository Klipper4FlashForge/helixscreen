// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_widget_size_ams_mini_status.cpp
 * @brief Pixel-width thresholds for the ams_mini_status widget.
 *
 * ams_mini_status is a pure-XML widget driven through a C API, not a
 * PanelWidget subclass, so it does not use PanelWidgetHarness<W>. It already
 * takes a plain `width_px` and derives everything -- bar-width band, visible
 * bar cap, BAR/SPOOL mode, and the spool-cell count in the wide view -- from
 * that single pixel value plus (for the spool view) the real laid-out width
 * of its own container.
 */

#include "ui_ams_mini_status.h"
#include "ui_update_queue.h"

#include "../lvgl_ui_test_fixture.h"
#include "../ui_test_utils.h"
#include "panel_widget_size.h"
#include "theme_manager.h"

#include <algorithm>
#include <vector>

#include "../catch_amalgamated.hpp"

namespace {

/// Fills `count` slots (1..8) with arbitrary-but-present data so bar/spool
/// rendering has something to draw.
void fill_slots(lv_obj_t* w, int count) {
    ui_ams_mini_status_set_slot_count(w, count);
    for (int i = 0; i < count; ++i) {
        ui_ams_mini_status_set_slot_full(w, i, 0xFF0000 + i, 50 + i, true, "PLA", 50 + i);
    }
}

} // namespace

TEST_CASE_METHOD(LVGLUITestFixture, "ams_mini bar mode: width bands pick bar width + visible cap",
                 "[ui][ams_mini][widget_size]") {
    ui_ams_mini_status_init();
    lv_obj_t* w = ui_ams_mini_status_create(test_screen(), 60);
    helix::ui::UpdateQueue::instance().drain(); // flush any stray create-time auto-sync
    // Resolve the container's real width BEFORE any slot data triggers the
    // first rebuild — rebuild_bars() reads lv_obj_get_content_width() at call
    // time, so an unresolved (still-zero) container would clamp every bar
    // down to MIN_BAR_WIDTH_PX regardless of the width_px band under test.
    lv_obj_update_layout(w);
    fill_slots(w, 8);

    // Tight band: width_px < 100 -> 8px bars, at most 6 of the 8 slots shown.
    ui_ams_mini_status_set_width(w, 90);
    helix::ui::UpdateQueue::instance().drain();
    lv_obj_update_layout(w);

    lv_obj_t* bars = UITest::find_by_name(w, "ams_bars_container");
    REQUIRE(bars != nullptr);
    REQUIRE_FALSE(lv_obj_has_flag(bars, LV_OBJ_FLAG_HIDDEN));
    REQUIRE(lv_obj_get_width(lv_obj_get_child(bars, 0)) == 8);

    // 8 slots, max 6 visible -> "+2" overflow badge, visible and non-empty.
    // overflow_label isn't named, so find it by type among the container's children.
    lv_obj_t* label = nullptr;
    for (uint32_t i = 0; i < lv_obj_get_child_count(w); ++i) {
        lv_obj_t* child = lv_obj_get_child(w, i);
        if (lv_obj_check_type(child, &lv_label_class)) {
            label = child;
            break;
        }
    }
    REQUIRE(label != nullptr);
    REQUIRE_FALSE(lv_obj_has_flag(label, LV_OBJ_FLAG_HIDDEN));
    REQUIRE(std::string(lv_label_get_text(label)) == "+2");

    // Medium band: 100 <= width_px < w_normal() -> 10px bars, all 8 slots shown
    // (the old <150 branch's min(max_visible, 8) was a no-op; removing it
    // must not change this — max_visible was already clamped to 8).
    ui_ams_mini_status_set_width(w, 110);
    helix::ui::UpdateQueue::instance().drain();
    lv_obj_update_layout(w);

    REQUIRE(lv_obj_get_width(lv_obj_get_child(bars, 0)) == 10);
    REQUIRE(lv_obj_has_flag(label, LV_OBJ_FLAG_HIDDEN)); // no overflow: all 8 fit

    lv_obj_delete(w);
}

TEST_CASE_METHOD(LVGLUITestFixture, "ams_mini mode dispatch: width_px vs w_normal() boundary",
                 "[ui][ams_mini][widget_size]") {
    ui_ams_mini_status_init();
    lv_obj_t* w = ui_ams_mini_status_create(test_screen(), 60);
    helix::ui::UpdateQueue::instance().drain();
    fill_slots(w, 2);

    // Just below w_normal(): bar view.
    ui_ams_mini_status_set_width(w, helix::widget_size::w_normal() - 1);
    helix::ui::UpdateQueue::instance().drain();
    REQUIRE(UITest::find_by_name(w, "ams_spools_container") == nullptr);
    lv_obj_t* bars = UITest::find_by_name(w, "ams_bars_container");
    REQUIRE(bars != nullptr);
    REQUIRE_FALSE(lv_obj_has_flag(bars, LV_OBJ_FLAG_HIDDEN));

    // At w_normal(): spool view.
    ui_ams_mini_status_set_width(w, helix::widget_size::w_normal());
    helix::ui::UpdateQueue::instance().drain();
    lv_obj_t* spools = UITest::find_by_name(w, "ams_spools_container");
    REQUIRE(spools != nullptr);
    REQUIRE_FALSE(lv_obj_has_flag(spools, LV_OBJ_FLAG_HIDDEN));
    REQUIRE(lv_obj_has_flag(bars, LV_OBJ_FLAG_HIDDEN));

    lv_obj_delete(w);
}

// The wide spool view used to show exactly `colspan` cells across (2 at 2x, 4
// at 4x) -- a literal count that cannot survive a square-cell grid halving
// today's cell width. It now derives the count from the REAL laid-out
// container width against a minimum spool-cell width (MIN_SPOOL_W, a local
// literal in ui_ams_mini_status.cpp -- mirrored here since it isn't exported).
// This re-derives the same formula against the widget's actual on-screen
// geometry rather than trusting width_px (which the widget itself does not
// trust for this calculation -- see the avail_w comment in rebuild_spools).
TEST_CASE_METHOD(LVGLUITestFixture, "ams_mini spool mode: cell width derives from real width",
                 "[ui][ams_mini][widget_size]") {
    constexpr int kMinSpoolW = 60; // mirrors MIN_SPOOL_W in ui_ams_mini_status.cpp

    ui_ams_mini_status_init();

    // A real narrow parent (not just a width_px hint) so the spool container's
    // laid-out content width is genuinely constrained, matching how the grid
    // sizes the widget's parent cell in production.
    lv_obj_t* narrow_parent = lv_obj_create(test_screen());
    lv_obj_remove_flag(narrow_parent, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(narrow_parent, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(narrow_parent, 0, LV_PART_MAIN);
    lv_obj_set_size(narrow_parent, 300, 60);

    lv_obj_t* w = ui_ams_mini_status_create(narrow_parent, 60);
    helix::ui::UpdateQueue::instance().drain();
    fill_slots(w, 6); // plenty of slots so the count is width-limited, not data-limited

    ui_ams_mini_status_set_width(w, 300); // wide view
    helix::ui::UpdateQueue::instance().drain();
    lv_obj_update_layout(narrow_parent);

    lv_obj_t* sc = UITest::find_by_name(w, "ams_spools_container");
    REQUIRE(sc != nullptr);
    lv_obj_t* cell0 = UITest::find_by_name(w, "spool_cell_0");
    REQUIRE(cell0 != nullptr);

    int avail_w = lv_obj_get_content_width(sc);
    int gap = theme_manager_get_spacing("space_xxs");
    REQUIRE(avail_w > 0);

    int expected_visible = (avail_w + gap) / (kMinSpoolW + gap);
    expected_visible = std::clamp(expected_visible, 1, 6);
    int expected_cell_px = (avail_w - (expected_visible - 1) * gap - 2) / expected_visible;
    if (expected_cell_px < kMinSpoolW)
        expected_cell_px = kMinSpoolW;

    REQUIRE(lv_obj_get_width(cell0) == expected_cell_px);
    // With a 300px real container the row fits more than one spool -- this is
    // the case that distinguishes the derived formula from the old literal
    // colspan count (which had no notion of "real available width" at all).
    REQUIRE(expected_visible >= 2);

    lv_obj_delete(w);
    lv_obj_delete(narrow_parent);
}

TEST_CASE_METHOD(LVGLUITestFixture,
                 "ams_mini spool mode: sparse slot_count caps visible below width capacity",
                 "[ui][ams_mini][widget_size]") {
    // A container wide enough for several spool cells, but only 1 real slot:
    // the derived visible count must not exceed the actual slot_count (no
    // blank reserved columns for spools that don't exist).
    lv_obj_t* wide_parent = lv_obj_create(test_screen());
    lv_obj_remove_flag(wide_parent, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(wide_parent, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(wide_parent, 0, LV_PART_MAIN);
    lv_obj_set_size(wide_parent, 600, 60);

    ui_ams_mini_status_init();
    lv_obj_t* w = ui_ams_mini_status_create(wide_parent, 60);
    helix::ui::UpdateQueue::instance().drain();
    fill_slots(w, 1);

    ui_ams_mini_status_set_width(w, 600);
    helix::ui::UpdateQueue::instance().drain();
    lv_obj_update_layout(wide_parent);

    lv_obj_t* sc = UITest::find_by_name(w, "ams_spools_container");
    REQUIRE(sc != nullptr);
    REQUIRE(lv_obj_get_child_count(sc) == 1);

    lv_obj_t* cell0 = UITest::find_by_name(w, "spool_cell_0");
    REQUIRE(cell0 != nullptr);
    int avail_w = lv_obj_get_content_width(sc);
    // Single slot fills the whole row (minus the -2px safety margin), not a
    // width/MIN_SPOOL_W-sized fraction of it.
    REQUIRE(lv_obj_get_width(cell0) >= avail_w - 4);

    lv_obj_delete(w);
    lv_obj_delete(wide_parent);
}

namespace {

/// What PanelWidgetManager actually hands the mini-status on each shipping
/// panel, at the size the DEFAULT LAYOUT gives it.
///
/// `ams` carries no anchor in any table in assets/config/default_layout.json,
/// so it auto-places at its registry default of 2x2 tracks — one cell — and
/// the width below is `grid_track_extent(cell_w, gutter, 2)` for that panel.
///
/// Measured, not derived: each row is the `[AmsMiniStatus] Width set to Npx`
/// line from `helix-screen --test -s <WxH> -vv` with the default mock AMS.
/// Re-measure that way rather than recomputing by hand — the width depends on
/// the container's content box, the gutter and the nav width, and every
/// attempt in this plan to predict one of those arithmetically has been wrong.
struct PanelCase {
    const char* name;
    UiBreakpoint bp;
    int width_px;
};

/// Pins the tier the widget reads while a case runs.
///
/// The mini-status resolves its own mode boundary through
/// `widget_size::w_normal()`, which reads the AMBIENT breakpoint subject — the
/// display it is being drawn on. The test harness has one small fixed display,
/// so without this every case is judged against Micro's 116px band and the
/// wide panels wrongly come out in spool view. Setting the subject is what
/// makes each case actually represent its panel.
///
/// Deliberately a local copy: test_default_layout.cpp and test_grid_edit_mode.cpp
/// each carry their own, the same per-TU convention as LayoutManagerTestAccess.
class BreakpointGuard {
  public:
    explicit BreakpointGuard(UiBreakpoint bp) {
        subj_ = theme_manager_get_breakpoint_subject();
        if (subj_) {
            if (subj_->type != LV_SUBJECT_TYPE_INT) {
                lv_subject_init_int(subj_, 0);
            }
            original_ = lv_subject_get_int(subj_);
            lv_subject_set_int(subj_, static_cast<int>(bp));
        }
    }
    ~BreakpointGuard() {
        if (subj_)
            lv_subject_set_int(subj_, original_);
    }
    BreakpointGuard(const BreakpointGuard&) = delete;
    BreakpointGuard& operator=(const BreakpointGuard&) = delete;

  private:
    lv_subject_t* subj_ = nullptr;
    int original_ = 0;
};

const std::vector<PanelCase> kShippingPanels = {
    {"micro 480x272", UiBreakpoint::Micro, 70},
    {"tiny 480x320", UiBreakpoint::Tiny, 82},
    {"small 480x400", UiBreakpoint::Small, 79},
    {"medium 800x480", UiBreakpoint::Medium, 114},
    {"large 1024x600", UiBreakpoint::Large, 107},
    {"xlarge 1280x720", UiBreakpoint::XLarge, 134},
    {"xxlarge 1920x1080", UiBreakpoint::XXLarge, 182},
    {"micro portrait 272x480", UiBreakpoint::Micro, 64},
    {"tiny portrait 320x480", UiBreakpoint::Tiny, 76},
    {"medium portrait 480x800", UiBreakpoint::Medium, 112},
    {"large portrait 600x1024", UiBreakpoint::Large, 112},
    {"tall 320x1480", UiBreakpoint::Tiny, 76},
    {"ultrawide 1480x320", UiBreakpoint::Tiny, 76},
    {"ultrawide 1920x440", UiBreakpoint::Small, 75},
};

} // namespace

// The answer to "how many spools does each panel show" is: none of them show
// spools at all. A one-cell widget is ~55-70% of w_normal() on every tier, and
// the spool view needs w_normal(), so MIN_SPOOL_W does not participate in the
// shipped layout — it only starts mattering once a user resizes the widget to
// two cells or more.
//
// Pinning it because it is load-bearing in the other direction: the bar view is
// what every AMS owner actually sees on the home screen, and a grid change that
// silently promoted one panel into the spool view would change the widget's
// whole character on that panel without touching this widget's code.
TEST_CASE_METHOD(LVGLUITestFixture, "ams_mini: every shipping panel gets the bar view",
                 "[ui][ams_mini][widget_size][1126]") {
    ui_ams_mini_status_init();

    for (const auto& c : kShippingPanels) {
        BreakpointGuard bp_guard(c.bp);
        INFO(c.name << " width " << c.width_px << "px, w_normal(" << static_cast<int>(c.bp)
                    << ") = " << helix::widget_size::w_normal(c.bp));

        // The mode boundary the widget itself uses, evaluated for that panel's
        // tier rather than the tier the test binary happens to run at.
        CHECK(c.width_px < helix::widget_size::w_normal(c.bp));

        lv_obj_t* w = ui_ams_mini_status_create(test_screen(), 60);
        helix::ui::UpdateQueue::instance().drain();
        lv_obj_update_layout(w);
        fill_slots(w, 4); // the default mock AMS: Happy Hare, 4 slots

        ui_ams_mini_status_set_width(w, c.width_px);
        helix::ui::UpdateQueue::instance().drain();
        lv_obj_update_layout(w);

        CHECK(UITest::find_by_name(w, "ams_spools_container") == nullptr);
        lv_obj_t* bars = UITest::find_by_name(w, "ams_bars_container");
        REQUIRE(bars != nullptr);
        CHECK_FALSE(lv_obj_has_flag(bars, LV_OBJ_FLAG_HIDDEN));

        // All four slots fit at every shipping width — the <100px visible cap
        // is 6, so it never bites on a 4-slot system.
        CHECK(lv_obj_get_child_count(bars) >= 4);

        lv_obj_delete(w);
    }
}

// The bar-width bands are absolute pixels (8 below 100, 10 below 150, else 16)
// while the widget's box scales with the tier, so the SAME widget is drawn at a
// different fraction of its box on each panel. This pins that ratio per panel
// rather than the constant, because the constant is not the thing that reads
// wrong on a screen.
TEST_CASE_METHOD(LVGLUITestFixture, "ams_mini: bar width band per shipping panel",
                 "[ui][ams_mini][widget_size][1126]") {
    ui_ams_mini_status_init();

    struct Expect {
        const char* name;
        int width_px;
        int bar_w;
    };
    // Derived from effective_max_bar_width()'s bands against the measured
    // widths above; the bar can come out NARROWER if the box cannot fit four
    // of them, which is why this asserts <= rather than ==.
    const std::vector<Expect> cases = {
        {"micro 480x272", 70, 8},     {"micro portrait 272x480", 64, 8},
        {"small 480x400", 79, 8},     {"ultrawide 1920x440", 75, 8},
        {"medium 800x480", 114, 10},  {"large 1024x600", 107, 10},
        {"xlarge 1280x720", 134, 10}, {"xxlarge 1920x1080", 182, 16},
    };

    for (const auto& c : cases) {
        lv_obj_t* parent = lv_obj_create(test_screen());
        lv_obj_remove_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_pad_all(parent, 0, LV_PART_MAIN);
        lv_obj_set_style_border_width(parent, 0, LV_PART_MAIN);
        lv_obj_set_size(parent, c.width_px, 64);

        lv_obj_t* w = ui_ams_mini_status_create(parent, 60);
        helix::ui::UpdateQueue::instance().drain();
        lv_obj_update_layout(parent);
        fill_slots(w, 4);
        ui_ams_mini_status_set_width(w, c.width_px);
        helix::ui::UpdateQueue::instance().drain();
        lv_obj_update_layout(parent);

        lv_obj_t* bars = UITest::find_by_name(w, "ams_bars_container");
        REQUIRE(bars != nullptr);
        REQUIRE(lv_obj_get_child_count(bars) >= 1);

        INFO(c.name << " width " << c.width_px << "px expects <= " << c.bar_w << "px bars");
        CHECK(lv_obj_get_width(lv_obj_get_child(bars, 0)) <= c.bar_w);
        CHECK(lv_obj_get_width(lv_obj_get_child(bars, 0)) >= 3); // MIN_BAR_WIDTH_PX

        lv_obj_delete(w);
        lv_obj_delete(parent);
    }
}
