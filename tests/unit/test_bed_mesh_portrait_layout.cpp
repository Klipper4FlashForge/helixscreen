// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_bed_mesh_portrait_layout.cpp
 * @brief Canvas sizing for the stacked portrait bed mesh layout
 *
 * Portrait stacks canvas / mesh info / profiles in one column. The canvas and
 * the profiles list would both flex_grow, so the split is decided here instead
 * of left to flex. Square is the target; wider-than-tall is acceptable when
 * vertical room is short, because a mesh plot squashed horizontally still
 * reads and one squashed vertically does not.
 */

#include "bed_mesh_portrait_layout.h"

#include "../catch_amalgamated.hpp"

using namespace helix;

TEST_CASE("Canvas is square when there is room", "[bed-mesh][portrait][layout]") {
    // 480px wide, 700px of column to spend: square (480) fits with room left
    // over for the info card and profiles.
    CHECK(bed_mesh_portrait_canvas_height(480, 700) == 480);
}

TEST_CASE("Canvas goes wider than tall when vertical room is short",
          "[bed-mesh][portrait][layout]") {
    // 480px wide but only 400px of column. A square canvas would consume the
    // whole thing and leave nothing for info or profiles, so it flattens.
    const int32_t h = bed_mesh_portrait_canvas_height(480, 400);
    CHECK(h < 480); // wider than tall
    CHECK(h > 0);
}

TEST_CASE("Canvas never falls below its floor share", "[bed-mesh][portrait][layout]") {
    // Pathological: a very short column. The canvas still claims its floor
    // rather than collapsing to nothing — an unreadable plot is a bug, an
    // unreachable profile row is a scroll.
    const int32_t avail = 300;
    const int32_t h = bed_mesh_portrait_canvas_height(480, avail);
    CHECK(h >= avail * kBedMeshPortraitCanvasMinPct / 100);
}

TEST_CASE("Canvas never exceeds its own width", "[bed-mesh][portrait][layout]") {
    // Taller-than-square wastes vertical room the profiles list needs and makes
    // the plot no more readable. Square is the ceiling.
    CHECK(bed_mesh_portrait_canvas_height(480, 2000) == 480);
    CHECK(bed_mesh_portrait_canvas_height(320, 2000) == 320);
}

TEST_CASE("Degenerate inputs return 0 rather than a garbage size", "[bed-mesh][portrait][layout]") {
    // Called from on_size_changed() before the first layout pass, when the
    // measured width is still 0. 0 means "cannot decide" — the caller leaves
    // the XML default in place, matching print_status_layout_decision.h.
    CHECK(bed_mesh_portrait_canvas_height(0, 700) == 0);
    CHECK(bed_mesh_portrait_canvas_height(480, 0) == 0);
    CHECK(bed_mesh_portrait_canvas_height(-1, -1) == 0);
}
