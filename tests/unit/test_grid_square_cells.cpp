// SPDX-License-Identifier: GPL-3.0-or-later

// The properties the square-cell sizing model exists to buy: cells that are
// square on every shipping panel, and a grid that transposes exactly when the
// panel rotates. Rotation-transpose, the design table and the track clamps
// are checked against the nominal grid — panel extent divided by track count
// — which is the whole of what get_dimensions() controls.
//
// Squareness itself is checked against the MEASURED cell instead (Owner
// ruling 7, docs/superpowers/plans/2026-08-08-square-cell-grid.md). Nominal
// aspect ignores container padding and the inter-track gutter, and once
// ruling 6 floors odd track counts to even, that omission is large enough to
// fail 4 of 8 shipping tiers while every one of them renders a near-perfect
// cell in practice — nominal was the wrong oracle, not the sizing model.

#include "ui_breakpoint.h"

#include "grid_layout.h"
#include "layout_manager.h"

#include <vector>

#include "../catch_amalgamated.hpp"

using helix::CellMetrics;
using helix::grid_cell_metrics;
using helix::GridDimensions;
using helix::GridLayout;
using helix::LayoutManager;

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

namespace {

struct Panel {
    const char* name;
    int w;
    int h;
};

// Every geometry HelixScreen ships or is known to run on.
const std::vector<Panel> kShippingPanels = {
    {"micro 480x272", 480, 272},       {"tiny 480x320", 480, 320},
    {"small 480x400", 480, 400},       {"medium 800x480", 800, 480},
    {"large 1024x600", 1024, 600},     {"xlarge 1280x720", 1280, 720},
    {"ultrawide 1920x440", 1920, 440}, {"ultratall 320x1480", 320, 1480},
};

GridDimensions dims_for(int w, int h) {
    auto& lm = LayoutManager::instance();
    LayoutManagerTestAccess::reset(lm);
    lm.init(w, h);
    return GridLayout::get_dimensions(breakpoint_for(std::min(w, h)));
}

} // namespace

TEST_CASE("square cells: measured cell aspect is within 25% of square", "[grid_layout][square]") {
    // content_w/content_h/gutter are real measurements from a live instance per
    // tier (ctl geom against carousel_host's actual content box), not derived
    // from get_dimensions() — see
    // .superpowers/sdd/2026-08-05-grid-metrics-followups/span-pixel-table.md.
    // Only cols/rows come from the code under test; grid_cell_metrics() is the
    // same production helper PanelWidgetManager::rebuild_page() calls, so this
    // asserts what the container actually renders, not a reimplementation of it.
    struct Tier {
        const char* name;
        int w, h;                         // panel resolution, fed to dims_for()
        int content_w, content_h, gutter; // measured content box for that panel
    };
    const std::vector<Tier> tiers = {
        {"micro 480x272", 480, 272, 430, 264, 2},
        {"micro portrait 272x480", 272, 480, 264, 394, 2},
        {"tiny 480x320", 480, 320, 418, 312, 2},
        {"small 480x400", 480, 400, 414, 388, 4},
        {"medium 800x480", 800, 480, 710, 466, 5},
        {"portrait 480x800", 480, 800, 466, 664, 5},
        {"large 1024x600", 1024, 600, 904, 584, 6},
        {"xlarge 1280x720", 1280, 720, 1128, 700, 8},
    };
    for (const auto& t : tiers) {
        auto d = dims_for(t.w, t.h);
        REQUIRE(d.cols > 0);
        REQUIRE(d.rows > 0);
        CellMetrics m = grid_cell_metrics(t.content_w, t.content_h, d.cols, d.rows, t.gutter);
        REQUIRE(m.cell_w > 0.0f);
        REQUIRE(m.cell_h > 0.0f);
        const double aspect = static_cast<double>(m.cell_w) / static_cast<double>(m.cell_h);
        INFO(t.name << " -> " << d.cols << "x" << d.rows << " cell " << m.cell_w << "x" << m.cell_h
                    << " aspect " << aspect);
        CHECK(aspect >= 0.80);
        CHECK(aspect <= 1.25);
    }
    LayoutManagerTestAccess::reset(LayoutManager::instance());
}

TEST_CASE("square cells: rotation transposes the grid exactly", "[grid_layout][square]") {
    for (const auto& p : kShippingPanels) {
        auto landscape = dims_for(p.w, p.h);
        auto portrait = dims_for(p.h, p.w);
        INFO(p.name << ": " << landscape.cols << "x" << landscape.rows << " vs " << portrait.cols
                    << "x" << portrait.rows);
        CHECK(landscape.cols == portrait.rows);
        CHECK(landscape.rows == portrait.cols);
    }
    LayoutManagerTestAccess::reset(LayoutManager::instance());
}

TEST_CASE("square cells: the shipped grids match the design table", "[grid_layout][square]") {
    struct Expected {
        int w, h, cols, rows;
    };
    const std::vector<Expected> table = {
        {480, 272, 14, 8},   {272, 480, 8, 14},   {480, 320, 12, 8},  {320, 480, 8, 12},
        {480, 400, 12, 10},  {800, 480, 12, 8},   {480, 800, 8, 12},  {1024, 600, 16, 10},
        {1280, 720, 16, 10}, {1920, 440, 48, 10}, {320, 1480, 8, 36},
    };
    for (const auto& e : table) {
        auto d = dims_for(e.w, e.h);
        INFO(e.w << "x" << e.h);
        CHECK(d.cols == e.cols);
        CHECK(d.rows == e.rows);
    }
    LayoutManagerTestAccess::reset(LayoutManager::instance());
}

TEST_CASE("square cells: no shipping panel reaches either track clamp", "[grid_layout][square]") {
    // MIN_TRACKS is a degenerate-display guard and MAX_TRACKS is a memory
    // ceiling. A shipping panel landing on either means the cell size no longer
    // controls the grid and the aspect invariant above is being met by accident.
    for (const auto& p : kShippingPanels) {
        for (bool rotated : {false, true}) {
            auto d = dims_for(rotated ? p.h : p.w, rotated ? p.w : p.h);
            INFO(p.name << (rotated ? " rotated" : ""));
            CHECK(d.cols > GridLayout::MIN_TRACKS);
            CHECK(d.rows > GridLayout::MIN_TRACKS);
            CHECK(d.cols < GridLayout::MAX_TRACKS);
            CHECK(d.rows < GridLayout::MAX_TRACKS);
        }
    }
    LayoutManagerTestAccess::reset(LayoutManager::instance());
}

TEST_CASE("square cells: an unsized LayoutManager falls to the track floor",
          "[grid_layout][square]") {
    // LayoutManager::width_ is 0 until Application initialises it. Any consumer
    // asking before then gets the floor, deliberately and identically on both
    // axes, rather than a plausible-looking grid derived from nothing.
    LayoutManagerTestAccess::reset(LayoutManager::instance());
    auto d = GridLayout::get_dimensions(UiBreakpoint::Medium);
    CHECK(d.cols == GridLayout::MIN_TRACKS);
    CHECK(d.rows == GridLayout::MIN_TRACKS);
}
