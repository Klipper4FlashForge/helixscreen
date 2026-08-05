// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_grid_edit_drag_path.cpp
 * @brief Drives a real drag through GridEditMode's instance methods.
 *
 * The four public statics (screen_to_grid_cell, round_to_grid_cell,
 * compute_resize_result, clamp_span) are well covered elsewhere, but nothing
 * exercises the geometry as the live drag path actually calls it —
 * handle_drag_move() reads current_metrics() and CellMetrics::gutter directly,
 * and no existing test drives that call. This test seeds a real pointer
 * device, drags a widget through GridEditMode's own event handlers, and
 * asserts the snap target the drag actually lands on, so that corrupting the
 * gutter inside handle_drag_move() is caught (it previously was not: the same
 * mutation passed all 67 pre-existing [grid_edit] tests).
 */

#include "ui_breakpoint.h"

#include "../test_fixtures.h"
#include "../test_helpers/grid_edit_mode_test_access.h"
#include "config.h"
#include "grid_edit_mode.h"
#include "grid_layout.h"
#include "panel_widget_config.h"
#include "panel_widget_manager.h"
#include "theme_manager.h"

#include <cmath>

#include "../catch_amalgamated.hpp"

using namespace helix;

namespace {

/// State consumed by the synthetic indev's read callback. File-scope, not a
/// stack local: LVGL retains the indev (and therefore this callback) for as
/// long as the indev exists, and ScopedTestIndev below is what bounds that
/// lifetime to the test.
struct DragIndevState {
    lv_point_t point{0, 0};
    lv_indev_state_t state{LV_INDEV_STATE_RELEASED};
};

DragIndevState g_drag_indev_state;

void drag_indev_read_cb(lv_indev_t* /*indev*/, lv_indev_data_t* data) {
    data->point = g_drag_indev_state.point;
    data->state = g_drag_indev_state.state;
}

/// Owns a hand-rolled pointer indev for the duration of one test.
///
/// lv_indev_active() (which handle_drag_start()/handle_drag_move() call
/// through lv_indev_get_point()) is non-null ONLY while lv_indev_read() is on
/// the stack (lib/lvgl/src/indev/lv_indev.c:229-296) — there is no public
/// setter — so every call into GridEditMode's handlers has to happen from
/// inside send() below, not from a bare event dispatch.
///
/// Unlike the file-static virtual_indev in ui_test_utils.cpp (deliberately
/// left for the whole binary's lifetime, cleaned up by lv_deinit()), this one
/// is deleted at scope exit: lv_indev_create() also arms a periodic read
/// timer, and an un-deleted indev would keep polling g_drag_indev_state on
/// every later test's process_lvgl()/lv_timer_handler() call for the rest of
/// the binary's run.
class ScopedTestIndev {
  public:
    ScopedTestIndev() {
        indev_ = lv_indev_create();
        lv_indev_set_type(indev_, LV_INDEV_TYPE_POINTER);
        lv_indev_set_read_cb(indev_, drag_indev_read_cb);
    }
    ~ScopedTestIndev() {
        g_drag_indev_state.state = LV_INDEV_STATE_RELEASED;
        lv_indev_delete(indev_);
    }
    ScopedTestIndev(const ScopedTestIndev&) = delete;
    ScopedTestIndev& operator=(const ScopedTestIndev&) = delete;

    /// Set the point/state and drive one read cycle. Dispatches PRESSED and/or
    /// PRESSING (or RELEASED/CLICKED) synchronously, bubbling from whatever the
    /// point hits up to the container — see the LV_OBJ_FLAG_EVENT_BUBBLE note
    /// on dots_overlay_ below for why that reaches our forwarding callback.
    void send(int x, int y, lv_indev_state_t state) {
        g_drag_indev_state.point = {x, y};
        g_drag_indev_state.state = state;
        lv_indev_read(indev_);
    }

  private:
    lv_indev_t* indev_ = nullptr;
};

/// Forwards LV_EVENT_PRESSING to the GridEditMode instance under test — the
/// same wiring HomePanel::on_home_grid_pressing uses in production
/// (src/ui/ui_panel_home.cpp), minus the safe-event-cb exception handling this
/// test doesn't need.
void forward_pressing(lv_event_t* e) {
    auto* em = static_cast<GridEditMode*>(lv_event_get_user_data(e));
    em->handle_pressing(e);
}

} // namespace

TEST_CASE_METHOD(XMLTestFixture, "GridEditMode: real drag lands on the gutter-aware snap target",
                 "[grid_edit][grid_edit_drag]") {
    // theme_manager_get_spacing() reads "ui_xml" as a path relative to the
    // process's cwd. Run this test from anywhere but the repo root and the
    // token silently resolves to 0 — gutters vanish and the whole test
    // becomes vacuous (it would still pass, having proven nothing).
    const int gutter = theme_manager_get_spacing("space_xs");
    REQUIRE(gutter > 0);

    // current_metrics() derives cols/rows from the live ui_breakpoint subject,
    // not from the container's own grid descriptor — pin the breakpoint this
    // fixture's fixed 800x480 display resolves to (see
    // test_panel_widget_manager_cell_px.cpp for the same assumption) so the
    // hand computation below and the container's own descriptor agree with
    // what GridEditMode will actually read.
    lv_subject_t* bp_subj = theme_manager_get_breakpoint_subject();
    REQUIRE(bp_subj != nullptr);
    REQUIRE(as_breakpoint(lv_subject_get_int(bp_subj)) == UiBreakpoint::Medium);
    const int ncols = GridLayout::get_cols(UiBreakpoint::Medium);
    const int nrows = GridLayout::get_rows(UiBreakpoint::Medium);
    REQUIRE(ncols > 0);
    REQUIRE(nrows > 0);

    // Container sized so every track is an exact 30px cell (no LVGL remainder
    // distribution to muddy the arithmetic): content = cols*cell + (cols-1)*gutter.
    constexpr int kCellPx = 30;
    const int content_w = ncols * kCellPx + (ncols - 1) * gutter;
    const int content_h = nrows * kCellPx + (nrows - 1) * gutter;

    lv_obj_t* container = lv_obj_create(test_screen());
    lv_obj_remove_flag(container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(container, 0, 0);
    lv_obj_set_style_border_width(container, 0, 0);
    lv_obj_set_size(container, content_w, content_h);

    // Real grid descriptor + pad_column/pad_row = gutter, matching what
    // PanelWidgetManager installs on the live home-panel container
    // (src/ui/panel_widget_manager.cpp) — this is what current_metrics()
    // needs a REAL grid for: not its own cols/rows count (it does not read
    // the descriptor for that), but so LVGL's own grid engine positions our
    // child widget for real, which is what the press points below are read
    // from.
    auto col_dsc = GridLayout::make_col_dsc(UiBreakpoint::Medium);
    auto row_dsc = GridLayout::make_row_dsc(UiBreakpoint::Medium);
    lv_obj_set_grid_dsc_array(container, col_dsc.data(), row_dsc.data());
    lv_obj_set_style_pad_column(container, gutter, 0);
    lv_obj_set_style_pad_row(container, gutter, 0);

    // Dragged widget: 2x2 cells at the origin. Big enough (2*30+gutter ~= 65px
    // per side) that its center sits comfortably outside the 18px resize-edge
    // margin (EDGE_HIT_INWARD/EDGE_HIT_MARGIN in grid_edit_mode.cpp) — this
    // test wants a plain move, not a resize.
    constexpr int kColspan = 2;
    constexpr int kRowspan = 2;
    lv_obj_t* widget = lv_obj_create(container);
    lv_obj_set_name(widget, "temperature");
    lv_obj_remove_flag(widget, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_grid_cell(widget, LV_GRID_ALIGN_STRETCH, 0, kColspan, LV_GRID_ALIGN_STRETCH, 0,
                         kRowspan);
    lv_obj_update_layout(container);

    // Config: put the widget on a SECOND page. PanelWidgetConfig::load()
    // appends registry-default widgets onto page 0 only (parse_widget_array's
    // append_registry_defaults, gated on `pages_.empty()` in
    // src/system/panel_widget_config.cpp) — a second page is the same trick
    // test_panel_widget_manager_cell_px.cpp uses to keep the page's widget
    // list exactly what this test wrote.
    const std::string panel_id = "test_grid_edit_drag_path";
    auto* cfg = Config::get_instance();
    cfg->set<nlohmann::json>(
        cfg->df() + "panel_widgets/" + panel_id,
        nlohmann::json{{"main_page_index", 0},
                       {"next_page_id", 2},
                       {"pages",
                        {{{"id", "main"}, {"widgets", nlohmann::json::array()}},
                         {{"id", "spy"},
                          {"widgets",
                           {{{"id", "temperature"},
                             {"enabled", true},
                             {"col", 0},
                             {"row", 0},
                             {"colspan", kColspan},
                             {"rowspan", kRowspan}}}}}}}});

    auto& mgr = PanelWidgetManager::instance();
    mgr.get_widget_config(panel_id).mark_dirty();
    mgr.clear_panel_config(panel_id);
    auto& config = mgr.get_widget_config(panel_id);
    constexpr int kPageIndex = 1; // "spy" page above

    // Sanity check on the config wiring itself, not the drag: "temperature" is
    // a real registered widget ID (panel_widget_registry.cpp) — an ID
    // parse_widget_array() doesn't recognize is silently dropped
    // (find_widget_def() == nullptr), which would make the drag below fail
    // with "widget not in config" rather than a snap-target mismatch.
    const auto& spy_entries = config.page_entries(static_cast<size_t>(kPageIndex));
    REQUIRE(spy_entries.size() == 1);
    REQUIRE(spy_entries[0].id == "temperature");

    GridEditMode em;
    em.enter(container, &config, kPageIndex);
    em.select_widget(widget);
    REQUIRE(em.selected_widget() == widget);

    lv_obj_add_event_cb(container, forward_pressing, LV_EVENT_PRESSING, &em);

    // --- Hand-computed expected snap target -----------------------------
    //
    // cell = (content - (n-1)*gutter) / n; pitch = cell + gutter. This is the
    // same formula grid_cell_metrics()/round_to_grid_cell() implement, but
    // re-derived here rather than called — calling the production helper to
    // build the expectation could not tell a correct helper from a broken
    // one.
    //
    // Column target = 3, landing point picked so the CORRECT pitch rounds it
    // down to 3 with real margin from the 3/4 cell boundary, while the
    // GUTTER-BLIND pitch (m.gutter treated as 0, content/cols instead of
    // (content-(n-1)*gutter)/n + gutter) rounds the SAME pixel up to 4 —
    // exactly the mutation Step 6 introduces. Both 3 and 4 are <= the max
    // valid target_col (ncols - colspan), so neither result gets clamped back
    // to the other — the mutation is genuinely observable, not masked.
    const float pitch_correct_col =
        static_cast<float>(content_w + gutter) / static_cast<float>(ncols);
    const float pitch_buggy_col = static_cast<float>(content_w) / static_cast<float>(ncols);
    constexpr int kExpectedCol = 3;
    const int target_px_x = static_cast<int>(
        std::lround((kExpectedCol + 0.5f) * (pitch_correct_col + pitch_buggy_col) / 2.0f));

    // Row target = 1, landing exactly on the correct track origin. The row
    // axis isn't the boundary-straddling case above (that needs only one
    // axis to prove the point) but it still exercises real gutter-aware
    // pixel math, and a bug that only broke rows would still fail it.
    constexpr int kExpectedRow = 1;
    const int target_px_y = kExpectedRow * (kCellPx + gutter);

    lv_area_t content_area;
    lv_obj_get_content_coords(container, &content_area);
    const int target_x = content_area.x1 + target_px_x;
    const int target_y = content_area.y1 + target_px_y;

    // --- Drive the drag ---------------------------------------------------
    ScopedTestIndev indev;

    lv_area_t sel_area;
    lv_obj_get_coords(widget, &sel_area);
    const lv_point_t center{(sel_area.x1 + sel_area.x2) / 2, (sel_area.y1 + sel_area.y2) / 2};

    // Frame 1: press at the widget's center. handle_pressing() selects the
    // drag-pending path (selected_ is already set) and records press_origin_.
    indev.send(center.x, center.y, LV_INDEV_STATE_PRESSED);
    REQUIRE_FALSE(em.is_catalog_open());

    // Frame 2: move to the widget's exact top-left corner (still >12px from
    // press_origin_, so this crosses DRAG_THRESHOLD_PX and starts the real
    // drag) — chosen so drag_offset_ becomes (0,0), which makes frame 3's
    // point equal the widget's new top-left directly, with no extra offset
    // arithmetic to carry through the hand computation above.
    // detect_resize_edge() in handle_drag_start() is checked against
    // press_origin_ (the center, from frame 1), not this point, so landing
    // exactly on the corner here does not trigger resize mode.
    indev.send(sel_area.x1, sel_area.y1, LV_INDEV_STATE_PRESSED);

    // Frame 3: move to the computed target. dragging_ is now true, so
    // handle_pressing() calls handle_drag_move() directly.
    indev.send(target_x, target_y, LV_INDEV_STATE_PRESSED);

    const int snap_col = GridEditModeTestAccess::snap_col(em);
    const int snap_row = GridEditModeTestAccess::snap_row(em);

    // A drag that never reached handle_drag_move() also reports -1 (both
    // snap_preview_col_/row_ start there) — assert >= 0 explicitly so that
    // failure mode cannot be mistaken for a passing target of -1.
    REQUIRE(snap_col >= 0);
    REQUIRE(snap_row >= 0);
    CHECK(snap_col == kExpectedCol);
    CHECK(snap_row == kExpectedRow);

    em.exit();
    mgr.clear_panel_config(panel_id);
}
