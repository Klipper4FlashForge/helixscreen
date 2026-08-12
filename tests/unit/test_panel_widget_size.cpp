// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_panel_widget_size.cpp
 * @brief Pins the four physical size-band constants against real measured
 * cell extents, not against themselves.
 *
 * Every per-widget `[widget_size]` test drives its cases from
 * `W_NORMAL - 1` / `H_TALL` / etc., which proves a widget's own predicate is
 * wired to the constant, but shifts both sides of the comparison together if
 * the constant itself is miscalibrated — it cannot catch an off-by-one in
 * the constant. This file drives the constants from the independently
 * measured tier table
 * (`.superpowers/sdd/2026-08-05-grid-metrics-followups/span-pixel-table.md`)
 * instead: real per-tier cell/span pixel extents, truncated the same way
 * `grid_track_extent()` truncates them at runtime
 * (`static_cast<int>(float)`, i.e. floor for these always-positive values).
 *
 * All four predicates in this codebase compare with `>=`, so a constant must
 * equal the smallest extent that should match — one pixel low silently
 * admits a false positive (Defect 1: XLarge's 134px single-column width
 * exactly equalled the old W_NORMAL, so a plain 1x1 widget there read as
 * "wide"), never a false negative.
 */

#include "panel_widget_size.h"

#include "catch_amalgamated.hpp"

using namespace helix::widget_size;

TEST_CASE("W_NORMAL admits the smallest genuine 2-column width and excludes every "
          "single-column width",
          "[widget_size][panel_widget_size]") {
    // Smallest measured 2-column (colspan>=2) extent across all eight tiers:
    // Small (480x400), span2 = 3*65.6667 rounds out to 135.33f truncated to
    // 135. This is the tightest genuine "wide" case there is — it must fire.
    CHECK(135 >= W_NORMAL);

    // Every tier's single-column (1x1) width, truncated exactly as
    // grid_track_extent() truncates it at runtime. None of these are a
    // colspan>=2 case and must NOT read as W_NORMAL-or-above. XLarge's 134 is
    // Defect 1 itself: it exactly equalled the old (134) constant.
    int span1_by_tier[] = {
        70,  // Micro
        68,  // Tiny
        65,  // Small
        114, // Medium
        107, // Large
        134, // XLarge -- the off-by-one this defect is about
        131, // Micro portrait
             // Portrait's 152 is intentionally excluded: the header comment
             // documents that physical size legitimately overtakes span
             // there (a single portrait column is wider than some smaller
             // tiers' two), so 152 >= W_NORMAL is correct, not a bug.
    };
    for (int w : span1_by_tier) {
        CHECK(w < W_NORMAL);
    }
}

TEST_CASE("W_WIDE admits the smallest genuine 3-column width and excludes narrower spans",
          "[widget_size][panel_widget_size]") {
    // Smallest measured 3-column (colspan>=3) extent: Small, span3 = 205
    // (exact — no truncation slop at this tier's span3).
    CHECK(205 >= W_WIDE);

    // A representative 2-column width that must stay below W_WIDE on tiers
    // where colspan>=3 is meaningfully distinct from colspan>=2.
    CHECK(142 < W_WIDE); // Micro span2
}

TEST_CASE("H_TALL admits the smallest genuine 2-row height and excludes every single-row "
          "height except the two tiers where physical size legitimately overtakes span",
          "[widget_size][panel_widget_size]") {
    // Smallest measured 2-row (rowspan>=2) extent: Micro (480x272), row2 =
    // 131 (exact). This is the case the header comment calls out by name —
    // recalibrating H_TALL upward to dodge Defect 2 would break it.
    CHECK(131 >= H_TALL);

    // Every tier's single-row (1x1) height, truncated exactly as
    // grid_track_extent() truncates it at runtime. Large and XLarge are
    // Defect 2 itself: a single grid row is already taller, in absolute
    // pixels, than Micro's genuinely-2-row case.
    int row1_by_tier[] = {
        64,  // Micro
        76,  // Tiny
        94,  // Small
        112, // Medium
    };
    for (int h : row1_by_tier) {
        CHECK(h < H_TALL);
    }
    // Large (141) and XLarge (169) single-row heights legitimately exceed
    // H_TALL — recalibrating the constant to exclude them would also
    // exclude Micro's genuine row2 (131), which is exactly what the header
    // comment is for: a widget whose taller layout also needs width must
    // gate on its own width predicate, not push H_TALL any higher.
    CHECK(141 >= H_TALL);
    CHECK(169 >= H_TALL);
}

TEST_CASE("H_TALLER already reflects truncation, not the raw float midpoint",
          "[widget_size][panel_widget_size]") {
    // Smallest measured 3-row (rowspan>=3) extent: Micro, row3 = 3*64.5 +
    // 2*2 = 197.5f exactly in float arithmetic. grid_track_extent()'s only
    // conversion to the int a widget receives is static_cast<int>(197.5f),
    // which truncates (not rounds) to 197 -- so 197, not 198 or 197.5, is
    // the real smallest value a widget ever sees at runtime, and H_TALLER
    // must equal it under the >= rule.
    constexpr int micro_row3_raw_truncated = static_cast<int>(3.0f * 64.5f + 2.0f * 2.0f);
    CHECK(micro_row3_raw_truncated == 197);
    CHECK(micro_row3_raw_truncated >= H_TALLER);
    CHECK(H_TALLER == 197);
}
