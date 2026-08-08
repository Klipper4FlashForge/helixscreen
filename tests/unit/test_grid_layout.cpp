// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_grid_layout.cpp
 * @brief Unit tests for GridLayout — grid dimensions, descriptor generation,
 *        widget placement, collision detection, and breakpoint adaptation.
 */

#include "grid_layout.h"
#include "layout_manager.h"
#include "panel_widget_registry.h"

#include "../catch_amalgamated.hpp"

using namespace helix;

// Access LayoutManager internals for test setup.
// Note: LayoutManagerTestAccess is also defined in test_layout_manager.cpp and
// test_grid_square_cells.cpp, but Catch2 amalgamated builds compile each test
// file separately, so no ODR conflict.
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

// get_dimensions() now divides the live LayoutManager panel size by a
// per-breakpoint track edge (GridLayout::GRID_CELL) instead of reading a
// fixed table, so every placement/collision/growth test below needs a real
// panel size behind it, not just a breakpoint enum.
//
// 250x165 is engineered, not arbitrary: at the MICRO track (34px) it floors to
// 6 cols x 4 rows, and at the TINY track (40px) it *also* floors to 6x4. Both
// breakpoints are used across this file's placement tests, and this is the
// one geometry that reproduces the pre-square-cell grid's 6x4 for either of
// them, so none of those tests' hardcoded coordinates had to change. Tests
// that need the LARGE breakpoint's larger grid override it explicitly with
// their own LayoutManager::init() call.
constexpr int kDefaultGridW = 250;
constexpr int kDefaultGridH = 165;

struct GridLayoutFixture {
    GridLayoutFixture() {
        LayoutManagerTestAccess::reset(helix::LayoutManager::instance());
        helix::LayoutManager::instance().init(kDefaultGridW, kDefaultGridH);
    }
    ~GridLayoutFixture() {
        LayoutManagerTestAccess::reset(helix::LayoutManager::instance());
    }
};

// Grid dimensions per breakpoint are no longer a fixed table — get_dimensions()
// divides the live LayoutManager panel size by a per-breakpoint track edge (see
// GridLayout::GRID_CELL). The properties that model exists to buy — square
// cells, exact rotation transpose, breakpoint-index clamping against real
// panel sizes — are covered in test_grid_square_cells.cpp, which drives
// LayoutManager::init() for every assertion. This file covers placement,
// collision, growth and descriptor generation, which are unaffected by how
// the dimensions themselves are computed.

// =============================================================================
// Descriptor array generation
// =============================================================================

TEST_CASE_METHOD(GridLayoutFixture, "GridLayout make_col_dsc: correct length and values",
                 "[grid_layout][descriptor]") {
    SECTION("MICRO (6 cols)") {
        auto dsc = GridLayout::make_col_dsc(UiBreakpoint::Micro);
        REQUIRE(dsc.size() == 7); // 6 FR values + terminator
        for (int i = 0; i < 6; ++i) {
            CHECK(dsc[static_cast<size_t>(i)] == LV_GRID_FR(1));
        }
        CHECK(dsc[6] == LV_GRID_TEMPLATE_LAST);
    }

    SECTION("LARGE (8 cols)") {
        auto& lm = helix::LayoutManager::instance();
        lm.init(480, 360); // LARGE track (60px): 480/60 = 8 cols
        auto dsc = GridLayout::make_col_dsc(UiBreakpoint::Large);
        REQUIRE(dsc.size() == 9); // 8 FR values + terminator
        for (int i = 0; i < 8; ++i) {
            CHECK(dsc[static_cast<size_t>(i)] == LV_GRID_FR(1));
        }
        CHECK(dsc[8] == LV_GRID_TEMPLATE_LAST);
    }
}

TEST_CASE_METHOD(GridLayoutFixture, "GridLayout make_row_dsc: correct length and values",
                 "[grid_layout][descriptor]") {
    SECTION("MICRO (4 rows)") {
        auto dsc = GridLayout::make_row_dsc(UiBreakpoint::Micro);
        REQUIRE(dsc.size() == 5); // 4 FR values + terminator
        for (int i = 0; i < 4; ++i) {
            CHECK(dsc[static_cast<size_t>(i)] == LV_GRID_FR(1));
        }
        CHECK(dsc[4] == LV_GRID_TEMPLATE_LAST);
    }

    SECTION("LARGE (6 rows)") {
        // Row counts are always even now — a track is half a cell (see
        // GridLayout::TRACKS_PER_CELL) — so the old table's odd 5-row LARGE
        // entry has no equivalent. 6 is the nearest even count at this track.
        auto& lm = helix::LayoutManager::instance();
        lm.init(480, 360); // LARGE track (60px): 360/60 = 6 rows
        auto dsc = GridLayout::make_row_dsc(UiBreakpoint::Large);
        REQUIRE(dsc.size() == 7); // 6 FR values + terminator
        for (int i = 0; i < 6; ++i) {
            CHECK(dsc[static_cast<size_t>(i)] == LV_GRID_FR(1));
        }
        CHECK(dsc[6] == LV_GRID_TEMPLATE_LAST);
    }
}

// =============================================================================
// Widget placement — successful
// =============================================================================

TEST_CASE_METHOD(GridLayoutFixture, "GridLayout place: single widget at origin",
                 "[grid_layout][placement]") {
    GridLayout grid(UiBreakpoint::Micro); // 6x4
    REQUIRE(grid.place({"widget_a", 0, 0, 2, 1}));
    REQUIRE(grid.placements().size() == 1);
    CHECK(grid.placements()[0].widget_id == "widget_a");
}

TEST_CASE_METHOD(GridLayoutFixture, "GridLayout place: multiple non-overlapping widgets",
                 "[grid_layout][placement]") {
    GridLayout grid(UiBreakpoint::Tiny); // 6x4
    REQUIRE(grid.place({"w1", 0, 0, 2, 2}));
    REQUIRE(grid.place({"w2", 2, 0, 2, 2}));
    REQUIRE(grid.place({"w3", 4, 0, 2, 2}));
    REQUIRE(grid.place({"w4", 0, 2, 3, 2}));
    CHECK(grid.placements().size() == 4);
}

TEST_CASE_METHOD(GridLayoutFixture, "GridLayout place: widget filling entire grid",
                 "[grid_layout][placement]") {
    GridLayout grid(UiBreakpoint::Micro); // 6x4
    REQUIRE(grid.place({"full", 0, 0, 6, 4}));
    CHECK(grid.placements().size() == 1);
}

// =============================================================================
// Collision detection
// =============================================================================

TEST_CASE_METHOD(GridLayoutFixture, "GridLayout place: rejects overlapping placements",
                 "[grid_layout][collision]") {
    GridLayout grid(UiBreakpoint::Tiny);     // 6x4
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

TEST_CASE_METHOD(GridLayoutFixture, "GridLayout can_place: returns false for occupied cells",
                 "[grid_layout][collision]") {
    GridLayout grid(UiBreakpoint::Micro); // 6x4
    grid.place({"w1", 0, 0, 2, 2});

    CHECK_FALSE(grid.can_place(0, 0, 1, 1));
    CHECK_FALSE(grid.can_place(1, 1, 1, 1));
    CHECK(grid.can_place(2, 0, 1, 1));
    CHECK(grid.can_place(0, 2, 1, 1));
}

// =============================================================================
// Out-of-bounds rejection
// =============================================================================

TEST_CASE_METHOD(GridLayoutFixture, "GridLayout place: rejects out-of-bounds placements",
                 "[grid_layout][bounds]") {
    GridLayout grid(UiBreakpoint::Micro); // 6x4

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

TEST_CASE_METHOD(GridLayoutFixture, "GridLayout find_available: finds first open position",
                 "[grid_layout][find]") {
    GridLayout grid(UiBreakpoint::Micro); // 6x4
    grid.place({"w1", 0, 0, 2, 1});

    auto pos = grid.find_available(2, 1);
    REQUIRE(pos.has_value());
    // First available 2x1 slot: (2,0) — same row, after w1
    CHECK(pos->first == 2);
    CHECK(pos->second == 0);
}

TEST_CASE_METHOD(GridLayoutFixture, "GridLayout find_available: scans top-to-bottom, left-to-right",
                 "[grid_layout][find]") {
    GridLayout grid(UiBreakpoint::Tiny); // 6x4

    // Fill top row completely
    grid.place({"r0a", 0, 0, 3, 1});
    grid.place({"r0b", 3, 0, 3, 1});

    // Next available 1x1 should be at row 1
    auto pos = grid.find_available(1, 1);
    REQUIRE(pos.has_value());
    CHECK(pos->first == 0);
    CHECK(pos->second == 1);
}

TEST_CASE_METHOD(GridLayoutFixture, "GridLayout find_available: returns nullopt when no space",
                 "[grid_layout][find]") {
    GridLayout grid(UiBreakpoint::Micro); // 6x4

    // Fill the entire grid with 1x1 widgets
    int id = 0;
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 6; ++c) {
            REQUIRE(grid.place({"fill_" + std::to_string(id++), c, r, 1, 1}));
        }
    }

    CHECK_FALSE(grid.find_available(1, 1).has_value());
}

TEST_CASE_METHOD(GridLayoutFixture, "GridLayout find_available: large widget in fragmented grid",
                 "[grid_layout][find]") {
    GridLayout grid(UiBreakpoint::Tiny); // 6x4

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

TEST_CASE_METHOD(GridLayoutFixture, "GridLayout remove: removes existing widget",
                 "[grid_layout][remove]") {
    GridLayout grid(UiBreakpoint::Micro); // 6x4
    grid.place({"w1", 0, 0, 2, 2});
    grid.place({"w2", 2, 0, 2, 2});

    REQUIRE(grid.remove("w1"));
    CHECK(grid.placements().size() == 1);
    CHECK(grid.placements()[0].widget_id == "w2");

    // Space freed: can place at (0,0) again
    CHECK(grid.can_place(0, 0, 2, 2));
}

TEST_CASE_METHOD(GridLayoutFixture, "GridLayout remove: returns false for nonexistent widget",
                 "[grid_layout][remove]") {
    GridLayout grid(UiBreakpoint::Micro);
    CHECK_FALSE(grid.remove("nonexistent"));
}

// =============================================================================
// clear()
// =============================================================================

TEST_CASE_METHOD(GridLayoutFixture, "GridLayout clear: removes all placements",
                 "[grid_layout][clear]") {
    GridLayout grid(UiBreakpoint::Micro); // 6x4
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

TEST_CASE_METHOD(GridLayoutFixture,
                 "GridLayout filter_for_breakpoint: separates fitting vs non-fitting",
                 "[grid_layout][filter]") {
    std::vector<GridPlacement> all = {
        {"fits_1", 0, 0, 2, 2},   // fits in 6x4
        {"fits_2", 2, 0, 2, 1},   // fits in 6x4
        {"too_wide", 0, 0, 7, 1}, // needs 7 cols, grid has 6
        {"too_tall", 0, 0, 1, 5}, // needs 5 rows, grid has 4
    };

    auto [fits, no_fit] = GridLayout::filter_for_breakpoint(UiBreakpoint::Micro, all); // 6x4

    REQUIRE(fits.size() == 2);
    REQUIRE(no_fit.size() == 2);

    CHECK(fits[0].widget_id == "fits_1");
    CHECK(fits[1].widget_id == "fits_2");
    CHECK(no_fit[0].widget_id == "too_wide");
    CHECK(no_fit[1].widget_id == "too_tall");
}

TEST_CASE_METHOD(GridLayoutFixture, "GridLayout filter_for_breakpoint: all fit in LARGE",
                 "[grid_layout][filter]") {
    auto& lm = helix::LayoutManager::instance();
    lm.init(480, 360); // LARGE: 8x6

    std::vector<GridPlacement> all = {
        {"w1", 0, 0, 4, 3},
        {"w2", 4, 0, 4, 2},
    };

    auto [fits, no_fit] = GridLayout::filter_for_breakpoint(UiBreakpoint::Large, all);
    CHECK(fits.size() == 2);
    CHECK(no_fit.empty());
}

// =============================================================================
// Breakpoint transition scenarios
// =============================================================================

TEST_CASE_METHOD(GridLayoutFixture,
                 "GridLayout breakpoint transition: LARGE placement does not fit MICRO",
                 "[grid_layout][transition]") {
    // A widget placed at col 7 in an 8-col (LARGE) grid should not fit in a
    // 6-col (MICRO) grid.
    std::vector<GridPlacement> placements = {
        {"corner", 7, 4, 1, 1}, // col 7 + span 1 = 8; row 4 + span 1 = 5
    };

    // MICRO (default fixture geometry): 6x4 — corner overruns both axes.
    auto [fits, no_fit] = GridLayout::filter_for_breakpoint(UiBreakpoint::Micro, placements);
    CHECK(fits.empty());
    CHECK(no_fit.size() == 1);

    // Re-init to a real LARGE-class panel: 8x6 — same placement now fits.
    auto& lm = helix::LayoutManager::instance();
    lm.init(480, 360);
    auto [fits2, no_fit2] = GridLayout::filter_for_breakpoint(UiBreakpoint::Large, placements);
    CHECK(fits2.size() == 1);
    CHECK(no_fit2.empty());
}

TEST_CASE_METHOD(GridLayoutFixture,
                 "GridLayout breakpoint transition: LARGE placement partially fits in SMALL",
                 "[grid_layout][transition]") {
    std::vector<GridPlacement> placements = {
        {"top_left", 0, 0, 2, 2},   // fits everywhere
        {"wide_right", 6, 0, 2, 1}, // needs col 6+2=8, only fits an 8-col-or-wider grid
        {"bottom_row", 0, 4, 3, 1}, // needs row 4+1=5, only fits a 5-row-or-taller grid
    };

    // SMALL (default fixture geometry, 6x4): only top_left fits.
    auto [small_fits, small_no] = GridLayout::filter_for_breakpoint(UiBreakpoint::Tiny, placements);
    CHECK(small_fits.size() == 1);
    CHECK(small_fits[0].widget_id == "top_left");
    CHECK(small_no.size() == 2);

    // LARGE (re-init to 8x6): all fit.
    auto& lm = helix::LayoutManager::instance();
    lm.init(480, 360);
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

TEST_CASE("GridLayout dimensions: out-of-range breakpoints clamp to the array bounds",
          "[grid_layout][dimensions]") {
    // clamp_bp() is file-local in grid_layout.cpp; exercised here through
    // get_dimensions() with indices outside [Micro, XLarge].
    auto below = GridLayout::get_dimensions(static_cast<UiBreakpoint>(-1));
    auto micro = GridLayout::get_dimensions(UiBreakpoint::Micro);
    CHECK(below.cols == micro.cols);
    CHECK(below.rows == micro.rows);

    auto above =
        GridLayout::get_dimensions(static_cast<UiBreakpoint>(GridLayout::NUM_BREAKPOINTS + 3));
    auto xlarge = GridLayout::get_dimensions(UiBreakpoint::XLarge);
    CHECK(above.cols == xlarge.cols);
    CHECK(above.rows == xlarge.rows);
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
// Descriptor generation with dynamic sizing
// =============================================================================

TEST_CASE_METHOD(GridLayoutFixture,
                 "GridLayout make_col_dsc: descriptor length matches the computed column count",
                 "[grid_layout][descriptor][dynamic]") {
    auto& lm = helix::LayoutManager::instance();
    lm.init(1920, 440); // wide, short panel

    auto dsc = GridLayout::make_col_dsc(UiBreakpoint::Tiny); // SMALL breakpoint track
    const auto expected_cols = static_cast<size_t>(GridLayout::get_cols(UiBreakpoint::Tiny));
    REQUIRE(dsc.size() == expected_cols + 1);
    for (size_t i = 0; i < expected_cols; ++i) {
        CHECK(dsc[i] == LV_GRID_FR(1));
    }
    CHECK(dsc[expected_cols] == LV_GRID_TEMPLATE_LAST);
}

TEST_CASE_METHOD(GridLayoutFixture,
                 "GridLayout make_row_dsc: descriptor length matches the computed row count",
                 "[grid_layout][descriptor][dynamic]") {
    auto& lm = helix::LayoutManager::instance();
    lm.init(480, 1600); // narrow, tall panel

    auto dsc = GridLayout::make_row_dsc(UiBreakpoint::Large); // XLARGE breakpoint track
    const auto expected_rows = static_cast<size_t>(GridLayout::get_rows(UiBreakpoint::Large));
    REQUIRE(dsc.size() == expected_rows + 1);
    for (size_t i = 0; i < expected_rows; ++i) {
        CHECK(dsc[i] == LV_GRID_FR(1));
    }
    CHECK(dsc[expected_rows] == LV_GRID_TEMPLATE_LAST);
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
    GridLayout grid(UiBreakpoint::Micro); // 6x4
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
    GridLayout grid(UiBreakpoint::Micro); // 6x4, entirely empty
    REQUIRE(grid.cols() == 6);

    // A 7-column minimum can never exist in a 6-column grid, however it is
    // packed. The grid is empty, so "grid full" would name the wrong condition.
    auto fit = grid.find_available_bottom_min(7, 1);
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
