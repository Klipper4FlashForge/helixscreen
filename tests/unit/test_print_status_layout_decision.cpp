// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_print_status_layout_decision.cpp
 * @brief Portrait print-status geometry rules (prestonbrown/helixscreen#1203)
 *
 * The fan-row fit test used to read controls_section's own height and subtract
 * its children. That is a fixed-budget-with-slack question, which only the
 * landscape row poses. A stacked portrait layout sizes controls_section to its
 * content, so the subtraction is ~0 regardless of how much screen is free and
 * the fan row latches off forever. Same shape of error in the side list, whose
 * 44% width encoded "the controls column is 4/9 of a row".
 */

#include "print_status_layout_decision.h"

#include "../catch_amalgamated.hpp"

using namespace helix::ui;

// ============================================================================
// fan_row_budget
// ============================================================================

TEST_CASE("fan_row_budget: landscape measures slack in the fixed column",
          "[print-status][portrait][layout-decision]") {
    // controls_section is height="100%" in a row; its children compete inside it.
    CHECK(fan_row_budget(/*portrait=*/false, /*controls_h=*/400, /*content_h=*/0, /*used=*/300) ==
          100);
    CHECK(fan_row_budget(false, 400, 0, 400) == 0);
    CHECK(fan_row_budget(false, 400, 0, 460) == -60); // over-subscribed
    // content_h is not consulted in landscape.
    CHECK(fan_row_budget(false, 400, 9999, 300) == 100);
}

TEST_CASE("fan_row_budget: portrait does not measure the content-sized column",
          "[print-status][portrait][layout-decision]") {
    // THE REGRESSION. In portrait controls_section is height="content", so its
    // height IS the sum of its children — controls_h == used. The landscape
    // formula yields 0 (or negative once the prospective fan row's gap counts),
    // which latches print_status_fans_fit to 0 permanently. Portrait must not
    // consult controls_h at all.
    const int32_t used = 300;
    const int32_t controls_h = used; // content-sized: no slack, by construction

    CHECK(fan_row_budget(false, controls_h, 1400, used) == 0); // what it used to compute
    CHECK(fan_row_budget(true, controls_h, 1400, used) > 0);   // what it must compute

    // 1400px stack, 40% reserved for the preview -> 840 usable, minus 300 used.
    CHECK(fan_row_budget(true, controls_h, 1400, used) == 540);
}

TEST_CASE("fan_row_budget: portrait still refuses when the stack is genuinely full",
          "[print-status][portrait][layout-decision]") {
    // The fix must not degenerate into "always fits". On a short portrait panel
    // the controls really can crowd the preview out, and the row must drop.
    // 430px stack (272x480 class), 40% reserved -> 258 usable.
    CHECK(fan_row_budget(true, 250, 430, 250) == 8);
    CHECK(fan_row_budget(true, 300, 430, 300) < 0);
}

TEST_CASE("fan_row_budget: the preview floor is what portrait actually protects",
          "[print-status][portrait][layout-decision]") {
    // With nothing else in the column, the budget is exactly the complement of
    // the reserve — the preview keeps its share no matter how empty the controls.
    CHECK(fan_row_budget(true, 0, 1000, 0) == 1000 - (1000 * kPortraitPreviewReservePct / 100));
    CHECK(fan_row_budget(true, 0, 1000, 0) == 600);
}

// ============================================================================
// exclude_side_list_geometry
// ============================================================================

TEST_CASE("exclude_side_list_geometry: landscape covers the right-hand column",
          "[print-status][portrait][layout-decision]") {
    const auto g = exclude_side_list_geometry(/*portrait=*/false);
    CHECK(g.width_pct == 44); // ~= 4/9, the controls column's share of the row
    CHECK(g.height_pct == 100);
    CHECK_FALSE(g.anchor_bottom);
}

TEST_CASE("exclude_side_list_geometry: portrait covers the bottom of the stack",
          "[print-status][portrait][layout-decision]") {
    // A 44%-wide list anchored right covers 44% of *everything* in a stack and
    // none of the controls fully — the bug. Full width, bottom-anchored, taking
    // the complement of the preview reserve so the object map stays visible.
    const auto g = exclude_side_list_geometry(/*portrait=*/true);
    CHECK(g.width_pct == 100);
    CHECK(g.anchor_bottom);
    CHECK(g.height_pct == 100 - kPortraitPreviewReservePct);
}

TEST_CASE("exclude_side_list_geometry: the list never hides the whole preview",
          "[print-status][portrait][layout-decision]") {
    // Whatever the reserve is tuned to, the map has to remain visible — the list
    // exists to accompany it, not replace it.
    for (bool portrait : {false, true}) {
        INFO("portrait=" << portrait);
        const auto g = exclude_side_list_geometry(portrait);
        CHECK(g.height_pct > 0);
        CHECK(g.height_pct <= 100);
        CHECK(g.width_pct > 0);
        CHECK(g.width_pct <= 100);
        if (portrait) {
            CHECK(g.height_pct < 100); // leaves room for the map above it
        }
    }
}

TEST_CASE("both rules derive from one reserve constant",
          "[print-status][portrait][layout-decision]") {
    // "How much of a portrait print-status screen is preview" is one decision.
    // If someone retunes it, both consumers must move together.
    const auto g = exclude_side_list_geometry(true);
    const int32_t stack = 1000;
    const int32_t preview_floor = stack * kPortraitPreviewReservePct / 100;

    CHECK(g.height_pct * stack / 100 == stack - preview_floor);
    CHECK(fan_row_budget(true, 0, stack, 0) == stack - preview_floor);
}
