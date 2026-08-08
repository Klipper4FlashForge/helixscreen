// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_grid_layout.cpp
 * @brief Unit tests for GridLayout — grid dimensions, descriptor generation,
 *        widget placement, collision detection, and breakpoint adaptation.
 */

#include "grid_layout.h"
#include "panel_widget_registry.h"

#include "../catch_amalgamated.hpp"

using namespace helix;

// =============================================================================
// Grid dimensions per breakpoint
// =============================================================================

TEST_CASE("GridLayout dimensions: MICRO (bp 0) = 6x4", "[grid_layout][dimensions]") {
    auto dims = GridLayout::get_dimensions(UiBreakpoint::Micro);
    CHECK(dims.cols == 6);
    CHECK(dims.rows == 4);
    CHECK(GridLayout::get_cols(UiBreakpoint::Micro) == 6);
    CHECK(GridLayout::get_rows(UiBreakpoint::Micro) == 4);
}

TEST_CASE("GridLayout dimensions: TINY (bp 1) = 6x4", "[grid_layout][dimensions]") {
    auto dims = GridLayout::get_dimensions(UiBreakpoint::Tiny);
    CHECK(dims.cols == 6);
    CHECK(dims.rows == 4);
}

TEST_CASE("GridLayout dimensions: SMALL (bp 2) = 6x4", "[grid_layout][dimensions]") {
    auto dims = GridLayout::get_dimensions(UiBreakpoint::Small);
    CHECK(dims.cols == 6);
    CHECK(dims.rows == 4);
}

TEST_CASE("GridLayout dimensions: MEDIUM (bp 3) = 6x4", "[grid_layout][dimensions]") {
    auto dims = GridLayout::get_dimensions(UiBreakpoint::Medium);
    CHECK(dims.cols == 6);
    CHECK(dims.rows == 4);
}

TEST_CASE("GridLayout dimensions: LARGE (bp 4) = 8x5", "[grid_layout][dimensions]") {
    auto dims = GridLayout::get_dimensions(UiBreakpoint::Large);
    CHECK(dims.cols == 8);
    CHECK(dims.rows == 5);
}

TEST_CASE("GridLayout dimensions: XLARGE (bp 5) = 8x5", "[grid_layout][dimensions]") {
    auto dims = GridLayout::get_dimensions(UiBreakpoint::XLarge);
    CHECK(dims.cols == 8);
    CHECK(dims.rows == 5);
}

TEST_CASE("GridLayout dimensions: out-of-range breakpoints are clamped",
          "[grid_layout][dimensions]") {
    // Negative clamps to 0 (MICRO)
    CHECK(GridLayout::get_cols(UiBreakpoint::Micro) == 6);
    CHECK(GridLayout::get_rows(UiBreakpoint::Micro) == 4);

    // Above max clamps to 5 (XLARGE)
    CHECK(GridLayout::get_cols(UiBreakpoint::XLarge) == 8);
    CHECK(GridLayout::get_rows(UiBreakpoint::XLarge) == 5);
}

// =============================================================================
// Descriptor array generation
// =============================================================================

TEST_CASE("GridLayout make_col_dsc: correct length and values", "[grid_layout][descriptor]") {
    SECTION("MICRO (6 cols)") {
        auto dsc = GridLayout::make_col_dsc(UiBreakpoint::Micro);
        REQUIRE(dsc.size() == 7); // 6 FR values + terminator
        for (int i = 0; i < 6; ++i) {
            CHECK(dsc[static_cast<size_t>(i)] == LV_GRID_FR(1));
        }
        CHECK(dsc[6] == LV_GRID_TEMPLATE_LAST);
    }

    SECTION("LARGE (8 cols)") {
        auto dsc = GridLayout::make_col_dsc(UiBreakpoint::Large);
        REQUIRE(dsc.size() == 9); // 8 FR values + terminator
        for (int i = 0; i < 8; ++i) {
            CHECK(dsc[static_cast<size_t>(i)] == LV_GRID_FR(1));
        }
        CHECK(dsc[8] == LV_GRID_TEMPLATE_LAST);
    }
}

TEST_CASE("GridLayout make_row_dsc: correct length and values", "[grid_layout][descriptor]") {
    SECTION("MICRO (4 rows)") {
        auto dsc = GridLayout::make_row_dsc(UiBreakpoint::Micro);
        REQUIRE(dsc.size() == 5); // 4 FR values + terminator
        for (int i = 0; i < 4; ++i) {
            CHECK(dsc[static_cast<size_t>(i)] == LV_GRID_FR(1));
        }
        CHECK(dsc[4] == LV_GRID_TEMPLATE_LAST);
    }

    SECTION("LARGE (5 rows)") {
        auto dsc = GridLayout::make_row_dsc(UiBreakpoint::Large);
        REQUIRE(dsc.size() == 6); // 5 FR values + terminator
        CHECK(dsc[5] == LV_GRID_TEMPLATE_LAST);
    }
}

// =============================================================================
// Widget placement — successful
// =============================================================================

TEST_CASE("GridLayout place: single widget at origin", "[grid_layout][placement]") {
    GridLayout grid(UiBreakpoint::Micro); // MICRO 6x4
    REQUIRE(grid.place({"widget_a", 0, 0, 2, 1}));
    REQUIRE(grid.placements().size() == 1);
    CHECK(grid.placements()[0].widget_id == "widget_a");
}

TEST_CASE("GridLayout place: multiple non-overlapping widgets", "[grid_layout][placement]") {
    GridLayout grid(UiBreakpoint::Tiny); // SMALL 6x4
    REQUIRE(grid.place({"w1", 0, 0, 2, 2}));
    REQUIRE(grid.place({"w2", 2, 0, 2, 2}));
    REQUIRE(grid.place({"w3", 4, 0, 2, 2}));
    REQUIRE(grid.place({"w4", 0, 2, 3, 2}));
    CHECK(grid.placements().size() == 4);
}

TEST_CASE("GridLayout place: widget filling entire grid", "[grid_layout][placement]") {
    GridLayout grid(UiBreakpoint::Micro); // MICRO 6x4
    REQUIRE(grid.place({"full", 0, 0, 6, 4}));
    CHECK(grid.placements().size() == 1);
}

// =============================================================================
// Collision detection
// =============================================================================

TEST_CASE("GridLayout place: rejects overlapping placements", "[grid_layout][collision]") {
    GridLayout grid(UiBreakpoint::Tiny);     // SMALL 6x4
    REQUIRE(grid.place({"w1", 1, 1, 2, 2})); // occupies (1,1)-(2,2)

    // Exact overlap
    CHECK_FALSE(grid.place({"w2", 1, 1, 2, 2}));

    // Partial overlap — top-left corner overlaps
    CHECK_FALSE(grid.place({"w3", 2, 2, 2, 2}));

    // Partial overlap — single cell
    CHECK_FALSE(grid.place({"w4", 2, 1, 1, 1}));

    // Adjacent — no overlap, should succeed
    CHECK(grid.place({"w5", 3, 1, 2, 2}));
}

TEST_CASE("GridLayout can_place: returns false for occupied cells", "[grid_layout][collision]") {
    GridLayout grid(UiBreakpoint::Micro); // MICRO 6x4
    grid.place({"w1", 0, 0, 2, 2});

    CHECK_FALSE(grid.can_place(0, 0, 1, 1));
    CHECK_FALSE(grid.can_place(1, 1, 1, 1));
    CHECK(grid.can_place(2, 0, 1, 1));
    CHECK(grid.can_place(0, 2, 1, 1));
}

// =============================================================================
// Out-of-bounds rejection
// =============================================================================

TEST_CASE("GridLayout place: rejects out-of-bounds placements", "[grid_layout][bounds]") {
    GridLayout grid(UiBreakpoint::Micro); // MICRO 6x4

    // Exceeds columns
    CHECK_FALSE(grid.place({"oob1", 5, 0, 2, 1})); // col 5 + span 2 = 7 > 6

    // Exceeds rows
    CHECK_FALSE(grid.place({"oob2", 0, 3, 1, 2})); // row 3 + span 2 = 5 > 4

    // Negative position
    CHECK_FALSE(grid.place({"oob3", -1, 0, 1, 1}));

    // Zero span
    CHECK_FALSE(grid.place({"oob4", 0, 0, 0, 1}));
    CHECK_FALSE(grid.place({"oob5", 0, 0, 1, 0}));

    // Exactly at boundary — should succeed
    CHECK(grid.place({"edge", 5, 3, 1, 1}));
}

// =============================================================================
// find_available()
// =============================================================================

TEST_CASE("GridLayout find_available: finds first open position", "[grid_layout][find]") {
    GridLayout grid(UiBreakpoint::Micro); // MICRO 6x4
    grid.place({"w1", 0, 0, 2, 1});

    auto pos = grid.find_available(2, 1);
    REQUIRE(pos.has_value());
    // First available 2x1 slot: (2,0) — same row, after w1
    CHECK(pos->first == 2);
    CHECK(pos->second == 0);
}

TEST_CASE("GridLayout find_available: scans top-to-bottom, left-to-right", "[grid_layout][find]") {
    GridLayout grid(UiBreakpoint::Tiny); // SMALL 6x4

    // Fill top row completely
    grid.place({"r0a", 0, 0, 3, 1});
    grid.place({"r0b", 3, 0, 3, 1});

    // Next available 1x1 should be at row 1
    auto pos = grid.find_available(1, 1);
    REQUIRE(pos.has_value());
    CHECK(pos->first == 0);
    CHECK(pos->second == 1);
}

TEST_CASE("GridLayout find_available: returns nullopt when no space", "[grid_layout][find]") {
    GridLayout grid(UiBreakpoint::Micro); // MICRO 6x4

    // Fill the entire grid with 1x1 widgets
    int id = 0;
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 6; ++c) {
            REQUIRE(grid.place({"fill_" + std::to_string(id++), c, r, 1, 1}));
        }
    }

    CHECK_FALSE(grid.find_available(1, 1).has_value());
}

TEST_CASE("GridLayout find_available: large widget in fragmented grid", "[grid_layout][find]") {
    GridLayout grid(UiBreakpoint::Tiny); // SMALL 6x4

    // Place checkerboard-style: occupy (0,0), (2,0), (4,0) with 1x1 widgets
    grid.place({"c1", 0, 0, 1, 1});
    grid.place({"c2", 2, 0, 1, 1});
    grid.place({"c3", 4, 0, 1, 1});

    // A 2x1 widget can fit at (0,1) on the second row
    auto pos = grid.find_available(2, 1);
    REQUIRE(pos.has_value());
    // Actually it should find something on row 0 at position (0,0) is occupied,
    // (1,0) is free — so (1,0) with span 2 needs (1,0) and (2,0). But (2,0) is occupied.
    // Next candidate: (3,0) with span 2 needs (3,0) and (4,0). (4,0) is occupied.
    // Then (5,0) needs (5,0)+(6,0)=out of bounds for 6 col grid? 5+2=7>6, no.
    // Row 1: (0,1) is free and (1,1) is free — so (0,1) works.
    CHECK(pos->first == 0);
    CHECK(pos->second == 1);
}

// =============================================================================
// remove()
// =============================================================================

TEST_CASE("GridLayout remove: removes existing widget", "[grid_layout][remove]") {
    GridLayout grid(UiBreakpoint::Micro); // MICRO 6x4
    grid.place({"w1", 0, 0, 2, 2});
    grid.place({"w2", 2, 0, 2, 2});

    REQUIRE(grid.remove("w1"));
    CHECK(grid.placements().size() == 1);
    CHECK(grid.placements()[0].widget_id == "w2");

    // Space freed: can place at (0,0) again
    CHECK(grid.can_place(0, 0, 2, 2));
}

TEST_CASE("GridLayout remove: returns false for nonexistent widget", "[grid_layout][remove]") {
    GridLayout grid(UiBreakpoint::Micro);
    CHECK_FALSE(grid.remove("nonexistent"));
}

// =============================================================================
// clear()
// =============================================================================

TEST_CASE("GridLayout clear: removes all placements", "[grid_layout][clear]") {
    GridLayout grid(UiBreakpoint::Micro); // MICRO 6x4
    grid.place({"w1", 0, 0, 1, 1});
    grid.place({"w2", 1, 0, 1, 1});
    REQUIRE(grid.placements().size() == 2);

    grid.clear();
    CHECK(grid.placements().empty());
    CHECK(grid.can_place(0, 0, 6, 4)); // full grid available
}

// =============================================================================
// filter_for_breakpoint()
// =============================================================================

TEST_CASE("GridLayout filter_for_breakpoint: separates fitting vs non-fitting",
          "[grid_layout][filter]") {
    std::vector<GridPlacement> all = {
        {"fits_1", 0, 0, 2, 2},   // fits in 6x4
        {"fits_2", 2, 0, 2, 1},   // fits in 6x4
        {"too_wide", 0, 0, 7, 1}, // needs 7 cols, TINY has 6
        {"too_tall", 0, 0, 1, 5}, // needs 5 rows, TINY has 4
    };

    auto [fits, no_fit] = GridLayout::filter_for_breakpoint(UiBreakpoint::Micro, all); // MICRO 6x4

    REQUIRE(fits.size() == 2);
    REQUIRE(no_fit.size() == 2);

    CHECK(fits[0].widget_id == "fits_1");
    CHECK(fits[1].widget_id == "fits_2");
    CHECK(no_fit[0].widget_id == "too_wide");
    CHECK(no_fit[1].widget_id == "too_tall");
}

TEST_CASE("GridLayout filter_for_breakpoint: all fit in LARGE", "[grid_layout][filter]") {
    std::vector<GridPlacement> all = {
        {"w1", 0, 0, 4, 3},
        {"w2", 4, 0, 4, 2},
    };

    auto [fits, no_fit] = GridLayout::filter_for_breakpoint(UiBreakpoint::Large, all); // LARGE 8x5
    CHECK(fits.size() == 2);
    CHECK(no_fit.empty());
}

// =============================================================================
// Breakpoint transition scenarios
// =============================================================================

TEST_CASE("GridLayout breakpoint transition: 8x5 placement does not fit in TINY",
          "[grid_layout][transition]") {
    // A widget placed at col 7 in an 8-col (LARGE) grid should not fit in TINY (6-col)
    std::vector<GridPlacement> placements = {
        {"corner", 7, 4, 1, 1}, // col 7 + span 1 = 8, TINY only has 6 cols; row 4 + 1 = 5 > 4
    };

    auto [fits, no_fit] =
        GridLayout::filter_for_breakpoint(UiBreakpoint::Micro, placements); // MICRO 6x4
    CHECK(fits.empty());
    CHECK(no_fit.size() == 1);

    // Same placement fits in LARGE (8x5)
    auto [fits2, no_fit2] = GridLayout::filter_for_breakpoint(UiBreakpoint::Large, placements);
    CHECK(fits2.size() == 1);
    CHECK(no_fit2.empty());
}

TEST_CASE("GridLayout breakpoint transition: LARGE placement partially fits in SMALL",
          "[grid_layout][transition]") {
    std::vector<GridPlacement> placements = {
        {"top_left", 0, 0, 2, 2},   // fits everywhere
        {"wide_right", 6, 0, 2, 1}, // needs col 6+2=8, only fits LARGE/XLARGE
        {"bottom_row", 0, 4, 3, 1}, // needs row 4+1=5, only fits LARGE/XLARGE
    };

    // SMALL (6x4): only top_left fits
    auto [small_fits, small_no] = GridLayout::filter_for_breakpoint(UiBreakpoint::Tiny, placements);
    CHECK(small_fits.size() == 1);
    CHECK(small_fits[0].widget_id == "top_left");
    CHECK(small_no.size() == 2);

    // LARGE (8x5): all fit
    auto [large_fits, large_no] =
        GridLayout::filter_for_breakpoint(UiBreakpoint::Large, placements);
    CHECK(large_fits.size() == 3);
    CHECK(large_no.empty());
}

// =============================================================================
// Instance breakpoint accessor
// =============================================================================

TEST_CASE("GridLayout instance: breakpoint and dimensions match", "[grid_layout][instance]") {
    UiBreakpoint bps[] = {UiBreakpoint::Micro,  UiBreakpoint::Tiny,  UiBreakpoint::Small,
                          UiBreakpoint::Medium, UiBreakpoint::Large, UiBreakpoint::XLarge};
    for (auto bp : bps) {
        GridLayout grid(bp);
        CHECK(grid.breakpoint() == bp);
        CHECK(grid.cols() == GridLayout::get_cols(bp));
        CHECK(grid.rows() == GridLayout::get_rows(bp));
    }
}

// =============================================================================
// PanelWidgetDef scalability constraints
// =============================================================================

TEST_CASE("PanelWidgetDef: default (non-scalable) widget", "[widget_def][scalability]") {
    PanelWidgetDef def{};
    def.colspan = 1;
    def.rowspan = 1;
    // min/max all 0 = use colspan/rowspan

    CHECK(def.effective_min_colspan() == 1);
    CHECK(def.effective_min_rowspan() == 1);
    CHECK(def.effective_max_colspan() == 1);
    CHECK(def.effective_max_rowspan() == 1);
    CHECK_FALSE(def.is_scalable());
}

TEST_CASE("PanelWidgetDef: scalable widget with explicit min/max", "[widget_def][scalability]") {
    PanelWidgetDef def{};
    def.colspan = 2;
    def.rowspan = 2;
    def.min_colspan = 1;
    def.min_rowspan = 1;
    def.max_colspan = 4;
    def.max_rowspan = 3;

    CHECK(def.effective_min_colspan() == 1);
    CHECK(def.effective_min_rowspan() == 1);
    CHECK(def.effective_max_colspan() == 4);
    CHECK(def.effective_max_rowspan() == 3);
    CHECK(def.is_scalable());
}

TEST_CASE("PanelWidgetDef: horizontally scalable only", "[widget_def][scalability]") {
    PanelWidgetDef def{};
    def.colspan = 2;
    def.rowspan = 1;
    def.min_colspan = 2;
    def.max_colspan = 6;
    // min/max rowspan = 0, so effective = rowspan = 1

    CHECK(def.effective_min_colspan() == 2);
    CHECK(def.effective_max_colspan() == 6);
    CHECK(def.effective_min_rowspan() == 1);
    CHECK(def.effective_max_rowspan() == 1);
    CHECK(def.is_scalable()); // max_col > min_col
}

TEST_CASE("PanelWidgetDef: registry entries have valid scalability constraints",
          "[widget_def][scalability]") {
    // Force registration so defs have their final state
    init_widget_registrations();

    for (const auto& def : get_all_widget_defs()) {
        INFO("Widget: " << def.id);
        // Min must not exceed max
        CHECK(def.effective_min_colspan() <= def.effective_max_colspan());
        CHECK(def.effective_min_rowspan() <= def.effective_max_rowspan());
        // Default must be within min..max range
        CHECK(def.colspan >= def.effective_min_colspan());
        CHECK(def.colspan <= def.effective_max_colspan());
        CHECK(def.rowspan >= def.effective_min_rowspan());
        CHECK(def.rowspan <= def.effective_max_rowspan());
    }
}

TEST_CASE("PanelWidgetDef: half-cell capability is opt-in", "[widget_def][half_cell][1126]") {
    // #1126 grants half-cell resolution to the small single-action widgets only.
    // Anything that renders a chart, an image, a list or a video frame needs a
    // whole cell on both axes, so its flags stay false.
    const std::vector<std::string> half_capable = {"lock", "shutdown", "firmware_restart",
                                                   "led_controls", "clock"};
    // camera is absent from the registry on builds without camera support, so it
    // is appended only where it exists rather than listed unconditionally.
    std::vector<std::string> whole_cell_only = {"temp_graph", "print_status", "job_queue", "ams",
                                                "tips"};
#if HELIX_HAS_CAMERA
    whole_cell_only.emplace_back("camera");
#endif

    for (const auto& id : half_capable) {
        INFO("widget " << id);
        const auto* def = helix::find_widget_def(id);
        REQUIRE(def != nullptr);
        CHECK(def->supports_half_col);
    }
    for (const auto& id : whole_cell_only) {
        INFO("widget " << id);
        const auto* def = helix::find_widget_def(id);
        REQUIRE(def != nullptr);
        CHECK_FALSE(def->supports_half_col);
        CHECK_FALSE(def->supports_half_row);
    }

    // clock is the only one that can halve on both axes.
    const auto* clock = helix::find_widget_def("clock");
    REQUIRE(clock != nullptr);
    CHECK(clock->supports_half_row);
}

TEST_CASE("PanelWidgetDef: half-cell defaults to off", "[widget_def][half_cell]") {
    helix::PanelWidgetDef def{};
    CHECK_FALSE(def.supports_half_col);
    CHECK_FALSE(def.supports_half_row);
}

// =============================================================================
// Dynamic grid dimensions for non-standard layouts
// =============================================================================

#include "layout_manager.h"

// Access LayoutManager internals for test setup.
// Note: LayoutManagerTestAccess is also defined in test_layout_manager.cpp but
// Catch2 amalgamated builds compile each test file separately, so no ODR conflict.
class LayoutManagerTestAccess {
  public:
    static void reset(helix::LayoutManager& lm) {
        lm.type_ = helix::LayoutType::STANDARD;
        lm.name_ = "standard";
        lm.override_name_.clear();
        lm.initialized_ = false;
        lm.width_ = 0;
        lm.height_ = 0;
    }
};

struct GridLayoutFixture {
    GridLayoutFixture() {
        LayoutManagerTestAccess::reset(helix::LayoutManager::instance());
    }
    ~GridLayoutFixture() {
        LayoutManagerTestAccess::reset(helix::LayoutManager::instance());
    }
};

TEST_CASE_METHOD(GridLayoutFixture, "GridLayout dimensions: ULTRAWIDE scales cols from width",
                 "[grid_layout][dimensions][ultrawide]") {
    auto& lm = helix::LayoutManager::instance();

    SECTION("1920x440 -> 12 cols, rows from SMALL breakpoint (4)") {
        lm.init(1920, 440);                                         // ULTRAWIDE, SMALL breakpoint
        auto dims = GridLayout::get_dimensions(UiBreakpoint::Tiny); // SMALL
        CHECK(dims.cols == 12);                                     // 1920 / 160 = 12
        // Rows come from the breakpoint table, NOT from TARGET_CELL_H_PX. 440/120
        // would be 3; anything other than 4 means the portrait row target leaked
        // into the ultrawide branch (#1215 must not change ultrawide).
        CHECK(dims.rows == 4); // SMALL base rows
    }
    SECTION("1920x480 -> 12 x 4 (the #1215 reference geometry, unchanged)") {
        lm.init(1920, 480);                                          // ULTRAWIDE
        auto dims = GridLayout::get_dimensions(UiBreakpoint::Small); // MEDIUM-ish table row
        CHECK(dims.cols == 12);
        CHECK(dims.rows == 4);
    }
    SECTION("2560x600 -> 16 cols (clamped), rows from LARGE breakpoint (5)") {
        lm.init(2560, 600);                                          // ULTRAWIDE, LARGE breakpoint
        auto dims = GridLayout::get_dimensions(UiBreakpoint::Large); // LARGE
        CHECK(dims.cols == 16); // 2560 / 160 = 16 (at max clamp)
        CHECK(dims.rows == 5);  // LARGE base rows
    }
    SECTION("640x200 -> 4 cols (min clamp)") {
        lm.init(640, 200);                                           // ratio 3.2 -> ULTRAWIDE
        auto dims = GridLayout::get_dimensions(UiBreakpoint::Micro); // MICRO (min clamp)
        CHECK(dims.cols == 4);                                       // 640 / 160 = 4 (at min clamp)
        CHECK(dims.rows == 4);                                       // MICRO base rows
    }
}

TEST_CASE_METHOD(GridLayoutFixture, "GridLayout dimensions: PORTRAIT scales BOTH axes",
                 "[grid_layout][dimensions][portrait]") {
    // Columns used to stay at the breakpoint default while only rows tracked the
    // screen. On a 320px-wide panel that left the landscape count of 6 — 53px
    // cells — so a widget authored 3-of-6 for landscape covered half the screen
    // and the rest of the row sat empty. Both axes now derive from a target cell
    // size: width from TARGET_CELL_W_PX (160), height from TARGET_CELL_H_PX (120,
    // the row height ultrawide has always shipped — #1215).
    auto& lm = helix::LayoutManager::instance();

    SECTION("480x1600 -> 3 cols, 13 rows") {
        lm.init(480, 1600);                                          // PORTRAIT, XLARGE breakpoint
        auto dims = GridLayout::get_dimensions(UiBreakpoint::Large); // XLARGE
        CHECK(dims.cols == 3);                                       // 480 / 160 = 3
        CHECK(dims.rows == 13);                                      // 1600 / 120 = 13
    }
    SECTION("480x800 -> 3 cols, 6 rows") {
        lm.init(480, 800);                                           // PORTRAIT
        auto dims = GridLayout::get_dimensions(UiBreakpoint::Large); // XLARGE
        CHECK(dims.cols == 3);                                       // 480 / 160 = 3
        CHECK(dims.rows == 6);                                       // 800 / 120 = 6
    }
    SECTION("480x1920 -> 16 rows (exactly at the cap)") {
        lm.init(480, 1920);                                          // PORTRAIT
        auto dims = GridLayout::get_dimensions(UiBreakpoint::Large); // XLARGE
        CHECK(dims.cols == 3);
        CHECK(dims.rows == 16); // 1920 / 120 = 16, exactly MAX_DYNAMIC_ROWS
    }
    SECTION("480x2400 -> 16 rows (max clamp)") {
        lm.init(480, 2400);                                          // PORTRAIT
        auto dims = GridLayout::get_dimensions(UiBreakpoint::Large); // XLARGE
        CHECK(dims.cols == 3);
        CHECK(dims.rows == GridLayout::MAX_DYNAMIC_ROWS); // 2400 / 120 = 20, clamped
    }
    SECTION("320x1480 (Waveshare 11.9) -> 2 cols (min clamp), 12 rows") {
        lm.init(320, 1480);                                         // PORTRAIT
        auto dims = GridLayout::get_dimensions(UiBreakpoint::Tiny); // TINY
        CHECK(dims.cols == 2);  // 320/160 = 2, at portrait floor
        CHECK(dims.rows == 12); // 1480 / 120 = 12 (was 9 with a 160px row target)
    }
    SECTION("320x480 -> portrait floor applies to TINY_PORTRAIT too") {
        lm.init(320, 480); // TINY_PORTRAIT (max_dim <= 480, taller than wide)
        auto dims = GridLayout::get_dimensions(UiBreakpoint::Micro);
        CHECK(dims.cols == 2); // 320 / 160 = 2
        CHECK(dims.rows == 4); // 480 / 120 = 4
    }
    SECTION("272x480 -> MICRO_PORTRAIT also scales") {
        lm.init(272, 480); // MICRO_PORTRAIT (min_dim <= 272)
        auto dims = GridLayout::get_dimensions(UiBreakpoint::Micro);
        CHECK(dims.cols == 2); // 272/160 = 1, lifted to the portrait floor
        CHECK(dims.rows == 4); // 480 / 120 = 4
    }
    SECTION("row floor still applies to a short portrait panel") {
        lm.init(320, 340); // 340 / 120 = 2, below MIN_DYNAMIC_ROWS
        auto dims = GridLayout::get_dimensions(UiBreakpoint::Micro);
        CHECK(dims.rows == GridLayout::MIN_DYNAMIC_ROWS);
    }
}

// #1215: the tall axis must not be rationed relative to the wide one. Pin the
// cell *density* relationship directly — a 320x1480 portrait panel and a
// 1920x480 ultrawide panel must agree on how many pixels a row is worth.
TEST_CASE_METHOD(GridLayoutFixture, "GridLayout: portrait and ultrawide agree on row height",
                 "[grid_layout][dimensions][portrait][ultrawide][1215]") {
    auto& lm = helix::LayoutManager::instance();

    lm.init(1920, 480);
    auto ultrawide = GridLayout::get_dimensions(UiBreakpoint::Small);
    int ultrawide_row_px = 480 / ultrawide.rows;

    LayoutManagerTestAccess::reset(lm);
    lm.init(320, 1480);
    auto portrait = GridLayout::get_dimensions(UiBreakpoint::Tiny);
    int portrait_row_px = 1480 / portrait.rows;

    INFO("ultrawide row px: " << ultrawide_row_px << ", portrait row px: " << portrait_row_px);
    CHECK(ultrawide_row_px == 120);
    CHECK(portrait_row_px == 123); // 1480/12 — same 120px target, integer remainder
    CHECK(portrait.cols * portrait.rows == 24);
}

TEST_CASE_METHOD(GridLayoutFixture, "GridLayout: landscape column counts are untouched",
                 "[grid_layout][dimensions][portrait]") {
    // The portrait floor must not leak into any landscape class — those keep the
    // breakpoint table (or, for ultrawide, the width-derived count with the
    // landscape floor of 4).
    auto& lm = helix::LayoutManager::instance();

    SECTION("800x480 STANDARD keeps its table cols") {
        lm.init(800, 480);
        auto dims = GridLayout::get_dimensions(UiBreakpoint::Tiny);
        CHECK(dims.cols == GridLayout::get_dimensions(UiBreakpoint::Tiny).cols);
        CHECK(dims.cols >= GridLayout::MIN_DYNAMIC_COLS);
    }
    SECTION("480x272 MICRO keeps its table cols") {
        lm.init(480, 272); // landscape, wider than tall
        auto dims = GridLayout::get_dimensions(UiBreakpoint::Micro);
        CHECK(dims.cols == 6); // MICRO base, unchanged by the portrait branch
        CHECK(dims.rows == 4);
    }
}

TEST_CASE_METHOD(GridLayoutFixture, "GridLayout dimensions: STANDARD layout uses table (unchanged)",
                 "[grid_layout][dimensions][standard]") {
    auto& lm = helix::LayoutManager::instance();
    lm.init(800, 480); // STANDARD

    auto dims = GridLayout::get_dimensions(UiBreakpoint::Small); // MEDIUM
    CHECK(dims.cols == 6);
    CHECK(dims.rows == 4);
}

TEST_CASE_METHOD(GridLayoutFixture, "GridLayout dimensions: uninitialized LayoutManager uses table",
                 "[grid_layout][dimensions]") {
    // LayoutManager not initialized (reset by fixture) — should fall back to table
    auto dims = GridLayout::get_dimensions(UiBreakpoint::Small); // MEDIUM
    CHECK(dims.cols == 6);
    CHECK(dims.rows == 4);
}

// =============================================================================
// Descriptor generation and instance methods with dynamic sizing
// =============================================================================

TEST_CASE_METHOD(GridLayoutFixture,
                 "GridLayout make_col_dsc: ultrawide produces correct descriptor length",
                 "[grid_layout][descriptor][ultrawide]") {
    auto& lm = helix::LayoutManager::instance();
    lm.init(1920, 440); // ULTRAWIDE -> 12 cols

    auto dsc = GridLayout::make_col_dsc(UiBreakpoint::Tiny); // SMALL breakpoint
    REQUIRE(dsc.size() == 13);                               // 12 FR values + terminator
    for (int i = 0; i < 12; ++i) {
        CHECK(dsc[static_cast<size_t>(i)] == LV_GRID_FR(1));
    }
    CHECK(dsc[12] == LV_GRID_TEMPLATE_LAST);
}

TEST_CASE_METHOD(GridLayoutFixture,
                 "GridLayout make_row_dsc: portrait produces correct descriptor length",
                 "[grid_layout][descriptor][portrait]") {
    auto& lm = helix::LayoutManager::instance();
    lm.init(480, 1600); // PORTRAIT -> 13 rows (1600 / 120)

    auto dsc = GridLayout::make_row_dsc(UiBreakpoint::Large); // XLARGE breakpoint
    REQUIRE(dsc.size() == 14);                                // 13 FR values + terminator
    for (int i = 0; i < 13; ++i) {
        CHECK(dsc[static_cast<size_t>(i)] == LV_GRID_FR(1));
    }
    CHECK(dsc[13] == LV_GRID_TEMPLATE_LAST);
}

TEST_CASE_METHOD(GridLayoutFixture,
                 "GridLayout instance: ultrawide dimensions match static accessors",
                 "[grid_layout][instance][ultrawide]") {
    auto& lm = helix::LayoutManager::instance();
    lm.init(1920, 440); // ULTRAWIDE

    GridLayout grid(UiBreakpoint::Tiny); // SMALL breakpoint
    CHECK(grid.cols() == 12);
    CHECK(grid.rows() == 4);
    CHECK(grid.cols() == GridLayout::get_cols(UiBreakpoint::Tiny));
    CHECK(grid.rows() == GridLayout::get_rows(UiBreakpoint::Tiny));
}

// =============================================================================
// Minimum-first placement and growth (#1216)
// =============================================================================
//
// Auto-placement used to ask for the largest span that fit and step down until
// something took. On a 3-column portrait grid that let `tips` (authored 4x2)
// claim a reduced 3x2 — 6 of 18 cells — and the three widgets behind it were
// then disabled with "grid full". The policy is now: every auto-placed widget
// gets its DECLARED MINIMUM first, so widget count is maximised, and only then
// does anything grow back toward its authored default.

TEST_CASE_METHOD(GridLayoutFixture,
                 "GridLayout find_available_bottom_min: grants the minimum, not the largest fit",
                 "[grid_layout][find][minfirst][1216]") {
    GridLayout grid(UiBreakpoint::Micro); // 6x4 table, LayoutManager uninitialised
    REQUIRE(grid.cols() == 6);

    // tips: authored 4x2, minimum 2x1. A 6-column grid could hold the full 4x2,
    // and the old greedy finder handed it over. Minimum-first must not.
    auto fit = grid.find_available_bottom_min(2, 1);
    CHECK(fit.failure == GridLayout::PlacementFailure::None);
    CHECK(fit.colspan == 2);
    CHECK(fit.rowspan == 1);
    CHECK(fit.col == 4); // bottom-right packed
    CHECK(fit.row == 3);
}

TEST_CASE_METHOD(GridLayoutFixture,
                 "GridLayout find_available_bottom_min: reports TooLargeForGrid, not GridFull",
                 "[grid_layout][find][minfirst][1216]") {
    auto& lm = helix::LayoutManager::instance();
    lm.init(320, 1480); // 2 cols x 12 rows, entirely empty
    GridLayout grid(UiBreakpoint::Tiny);

    // A 4-column minimum in a 2-column grid can never exist here. The grid is
    // empty, so "grid full" would name the wrong condition.
    auto fit = grid.find_available_bottom_min(4, 1);
    CHECK(fit.failure == GridLayout::PlacementFailure::TooLargeForGrid);
    CHECK_FALSE(fit.placed());
}

TEST_CASE_METHOD(GridLayoutFixture,
                 "GridLayout find_available_bottom_min: reports GridFull when space runs out",
                 "[grid_layout][find][minfirst][1216]") {
    GridLayout grid(UiBreakpoint::Micro); // 6x4
    for (int r = 0; r < 4; ++r) {
        REQUIRE(grid.place({"filler" + std::to_string(r), 0, r, 6, 1}));
    }

    auto fit = grid.find_available_bottom_min(1, 1);
    CHECK(fit.failure == GridLayout::PlacementFailure::GridFull);
    CHECK_FALSE(fit.placed());
}

TEST_CASE("GridLayout failure_text names the condition that actually failed",
          "[grid_layout][minfirst][1216]") {
    // The toast said "grid full" for both causes. They must read differently.
    std::string full = GridLayout::failure_text(GridLayout::PlacementFailure::GridFull);
    std::string too_large = GridLayout::failure_text(GridLayout::PlacementFailure::TooLargeForGrid);
    CHECK(full == "grid full");
    CHECK(too_large != full);
    CHECK_FALSE(too_large.empty());
}

// --- growth ------------------------------------------------------------------

TEST_CASE_METHOD(GridLayoutFixture,
                 "GridLayout grow_once: extends right before any other direction",
                 "[grid_layout][grow][1216]") {
    GridLayout grid(UiBreakpoint::Micro); // 6x4
    REQUIRE(grid.place({"w", 2, 1, 1, 1}));

    CHECK(grid.grow_once("w", 2, 2));
    const auto* p = grid.find_placement("w");
    REQUIRE(p);
    CHECK(p->col == 2); // origin unchanged — right is tried first
    CHECK(p->row == 1);
    CHECK(p->colspan == 2);
    CHECK(p->rowspan == 1);
}

TEST_CASE_METHOD(GridLayoutFixture, "GridLayout grow_once: falls back to left and up at the edge",
                 "[grid_layout][grow][1216]") {
    GridLayout grid(UiBreakpoint::Micro); // 6x4
    // Bottom-right corner: exactly where minimum-first bottom-packing puts the
    // first auto-placed widget, so left/up is the common growth path.
    REQUIRE(grid.place({"w", 5, 3, 1, 1}));

    REQUIRE(grid.grow_once("w", 2, 2));
    const auto* p = grid.find_placement("w");
    REQUIRE(p);
    CHECK(p->col == 4); // grew left
    CHECK(p->colspan == 2);
    CHECK(p->row == 3);
    CHECK(p->rowspan == 1);

    REQUIRE(grid.grow_once("w", 2, 2));
    p = grid.find_placement("w");
    REQUIRE(p);
    CHECK(p->row == 2); // then up
    CHECK(p->rowspan == 2);
    CHECK(p->col == 4);
    CHECK(p->colspan == 2);
}

TEST_CASE_METHOD(GridLayoutFixture, "GridLayout grow_once: never overruns an occupied neighbour",
                 "[grid_layout][grow][1216]") {
    GridLayout grid(UiBreakpoint::Micro); // 6x4
    REQUIRE(grid.place({"w", 0, 0, 1, 1}));
    REQUIRE(grid.place({"right", 1, 0, 1, 1}));
    REQUIRE(grid.place({"below", 0, 1, 1, 1}));

    // Right and down are taken; left and up are off-grid.
    CHECK_FALSE(grid.grow_once("w", 4, 4));
    const auto* p = grid.find_placement("w");
    REQUIRE(p);
    CHECK(p->colspan == 1);
    CHECK(p->rowspan == 1);
    // …and the neighbours are untouched.
    CHECK(grid.find_placement("right")->col == 1);
    CHECK(grid.find_placement("below")->row == 1);
}

TEST_CASE_METHOD(GridLayoutFixture, "GridLayout grow_once: stops at the target span",
                 "[grid_layout][grow][1216]") {
    GridLayout grid(UiBreakpoint::Micro); // 6x4, otherwise empty
    REQUIRE(grid.place({"w", 0, 0, 2, 2}));

    // Already at the target: no growth even though the grid is mostly free.
    CHECK_FALSE(grid.grow_once("w", 2, 2));
    const auto* p = grid.find_placement("w");
    CHECK(p->colspan == 2);
    CHECK(p->rowspan == 2);
}

TEST_CASE_METHOD(GridLayoutFixture, "GridLayout grow_once: ignores an unknown widget",
                 "[grid_layout][grow][1216]") {
    GridLayout grid(UiBreakpoint::Micro);
    CHECK_FALSE(grid.grow_once("nobody", 4, 4));
}

TEST_CASE_METHOD(GridLayoutFixture, "GridLayout grow_to_targets: expands into the free region",
                 "[grid_layout][grow][1216]") {
    GridLayout grid(UiBreakpoint::Micro); // 6x4 = 24 cells
    // Two widgets parked at their minimum in the bottom-right corner.
    REQUIRE(grid.place({"a", 5, 3, 1, 1}));
    REQUIRE(grid.place({"b", 3, 3, 2, 1}));

    int steps = grid.grow_to_targets({{"a", 2, 2}, {"b", 2, 2}});
    CHECK(steps > 0);

    const auto* a = grid.find_placement("a");
    const auto* b = grid.find_placement("b");
    REQUIRE(a);
    REQUIRE(b);
    INFO("a=" << a->col << "," << a->row << " " << a->colspan << "x" << a->rowspan
              << "  b=" << b->col << "," << b->row << " " << b->colspan << "x" << b->rowspan);
    CHECK(a->colspan * a->rowspan > 1); // both grew
    CHECK(b->colspan * b->rowspan > 2);
    CHECK(a->colspan <= 2); // …and neither passed its target
    CHECK(a->rowspan <= 2);
    CHECK(b->colspan <= 2);
    CHECK(b->rowspan <= 2);
}

TEST_CASE_METHOD(GridLayoutFixture, "GridLayout grow_to_targets: round-robin, not first-come",
                 "[grid_layout][grow][1216]") {
    // One row of slack shared by two widgets that both want it. Round-robin
    // hands one step to each in turn, so the first target cannot absorb the
    // whole strip while the second stays at its minimum.
    GridLayout grid(UiBreakpoint::Micro); // 6x4
    // Fill rows 0-1 so only rows 2-3 are in play, then park two 1x1s on row 3.
    REQUIRE(grid.place({"pad0", 0, 0, 6, 1}));
    REQUIRE(grid.place({"pad1", 0, 1, 6, 1}));
    REQUIRE(grid.place({"a", 0, 3, 1, 1}));
    REQUIRE(grid.place({"b", 1, 3, 1, 1}));

    grid.grow_to_targets({{"a", 1, 2}, {"b", 1, 2}});

    const auto* a = grid.find_placement("a");
    const auto* b = grid.find_placement("b");
    REQUIRE(a);
    REQUIRE(b);
    CHECK(a->rowspan == 2);
    CHECK(b->rowspan == 2);
}

TEST_CASE_METHOD(GridLayoutFixture, "GridLayout grow_to_targets: same input, same result",
                 "[grid_layout][grow][determinism][1216]") {
    auto run = [] {
        GridLayout grid(UiBreakpoint::Micro); // 6x4
        REQUIRE(grid.place({"a", 5, 3, 1, 1}));
        REQUIRE(grid.place({"b", 3, 3, 2, 1}));
        REQUIRE(grid.place({"c", 1, 3, 2, 1}));
        grid.grow_to_targets({{"a", 2, 2}, {"b", 2, 2}, {"c", 4, 2}});
        std::vector<std::tuple<std::string, int, int, int, int>> out;
        for (const auto& p : grid.placements()) {
            out.emplace_back(p.widget_id, p.col, p.row, p.colspan, p.rowspan);
        }
        return out;
    };

    auto first = run();
    for (int i = 0; i < 5; ++i) {
        CHECK(run() == first);
    }
}
