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

// ============================================================================
// portrait_preview_card_max_height / portrait_preview_slack
// ============================================================================

TEST_CASE("portrait_preview_card_max_height: the ceiling is the band's own width scaled",
          "[print-status][portrait][layout-decision]") {
    // band_w is the card's CONTENT width, i.e. the art band itself. The ceiling
    // is that width times the aspect cap, plus the strip and card chrome under it.
    CHECK(portrait_preview_card_max_height(/*band_w=*/300, /*chrome_h=*/0) ==
          300 * kMaxPreviewAspectPct / 100);
    CHECK(portrait_preview_card_max_height(300, 100) == 300 * kMaxPreviewAspectPct / 100 + 100);

    // 320x1480 ultratall — the case this exists for. 302px band, ~1090 available.
    const int32_t band_only = portrait_preview_card_max_height(302, 0);
    CHECK(band_only == 392); // 302 * 130 / 100, truncated
    CHECK(band_only < 1090); // strictly less than what flex_grow would hand it
}

TEST_CASE("portrait_preview_card_max_height: an unmeasurable card disables the cap",
          "[print-status][portrait][layout-decision]") {
    // A hidden or not-yet-laid-out subtree measures 0 wide. Returning a small
    // number there would clamp the preview to nothing on the first pass; 0 is
    // the sentinel that means "leave the layout alone".
    CHECK(portrait_preview_card_max_height(0, 174) == 0);
    CHECK(portrait_preview_card_max_height(-1, 174) == 0);
    // ...and a 0 ceiling must not be mistaken for "no slack allowed".
    CHECK(portrait_preview_slack(portrait_preview_card_max_height(0, 174), 1090, 8) == 0);
}

TEST_CASE("portrait_preview_card_max_height: the strip's second height moves the ceiling",
          "[print-status][portrait][layout-decision]") {
    // metadata_clip gains a text_small line once Klipper publishes an M117, so
    // the strip height is measured, not assumed. The band must keep its full
    // allowance either way — the taller strip raises the card's ceiling rather
    // than eating into the picture.
    const int32_t no_m117 = portrait_preview_card_max_height(302, 140);
    const int32_t with_m117 = portrait_preview_card_max_height(302, 174);
    CHECK(with_m117 - no_m117 == 174 - 140);
    CHECK(no_m117 - 140 == with_m117 - 174); // identical band in both
}

TEST_CASE("portrait_preview_slack: ultratall hands the leftover to the absorber",
          "[print-status][portrait][layout-decision]") {
    // 320x1480: ~1090px shared by the card and the absorber, 8px column gap.
    const int32_t max_h = portrait_preview_card_max_height(302, 174); // 392 + 174 = 566
    const int32_t slack = portrait_preview_slack(max_h, /*avail_h=*/1090, /*gap=*/8);
    CHECK(slack > 0);
    // The absorber's own gap is charged to the absorber, so the card lands on
    // exactly its ceiling — not one gap short of it.
    CHECK(1090 - slack - 8 == max_h);
}

TEST_CASE("portrait_preview_slack: sizes where the cap does not bind get nothing",
          "[print-status][portrait][layout-decision]") {
    // THE REGRESSION GUARD. 480x800 and 272x480 must be byte-identical to the
    // pre-cap layout, and the only way that holds is slack == 0 — a 0 return
    // keeps the absorber HIDDEN, and LVGL's flex pass skips hidden children
    // entirely, size and gap both. Any positive value here shifts every size.
    // 480x800: 446px band, 174px strip, card measured 536 tall.
    CHECK(portrait_preview_slack(portrait_preview_card_max_height(446, 174), 536, 8) == 0);
    // 272x480: 254px band, card measured ~348 tall.
    CHECK(portrait_preview_slack(portrait_preview_card_max_height(254, 174), 348, 8) == 0);
    // Landscape's card is ~380x392 — aspect 1.03, nowhere near the 1.30 cap.
    CHECK(portrait_preview_slack(portrait_preview_card_max_height(380, 0), 392, 8) == 0);
}

TEST_CASE("portrait_preview_slack: exactly-at and just-over the ceiling",
          "[print-status][portrait][layout-decision]") {
    const int32_t max_h = 500;
    // Fits exactly -> no absorber.
    CHECK(portrait_preview_slack(max_h, 500, 8) == 0);
    // Over by less than the absorber's own gap -> still not worth a child.
    CHECK(portrait_preview_slack(max_h, 505, 8) == 0);
    CHECK(portrait_preview_slack(max_h, 508, 8) == 0);
    // Over by more than the gap -> absorber takes the difference.
    CHECK(portrait_preview_slack(max_h, 509, 8) == 1);
    CHECK(portrait_preview_slack(max_h, 600, 8) == 92);
}

TEST_CASE("portrait_preview_slack: applying it is a fixed point",
          "[print-status][portrait][layout-decision]") {
    // The caller recomputes on every breakpoint/size event, so a second pass
    // over an already-capped layout must not drift. avail_h is invariant by
    // construction (card + absorber + the absorber's gap), so feeding the
    // post-cap geometry back in has to return the same slack.
    const int32_t max_h = portrait_preview_card_max_height(302, 174);
    const int32_t gap = 8;
    const int32_t avail = 1090;

    const int32_t slack1 = portrait_preview_slack(max_h, avail, gap);
    const int32_t card_h = avail - slack1 - gap;
    const int32_t avail2 = card_h + slack1 + gap; // what the caller re-measures
    CHECK(avail2 == avail);
    CHECK(portrait_preview_slack(max_h, avail2, gap) == slack1);
}

// ---------------------------------------------------------------------------
// portrait_graph_fits
// ---------------------------------------------------------------------------

TEST_CASE("portrait_graph_fits: only the ultratall slack is worth a graph",
          "[print-status][portrait][layout-decision][temp-graph]") {
    // Same geometry the absorber tests use, fed straight through: whatever
    // portrait_preview_slack() hands the absorber is the graph's entire budget.
    const int32_t ultratall = portrait_preview_slack(portrait_preview_card_max_height(302, 174),
                                                     /*avail_h=*/1090, /*gap=*/8);
    CHECK(portrait_graph_fits(ultratall, /*shown=*/false));

    // Every size where the cap does not bind gets zero slack, and zero slack can
    // never hold a graph — in either direction of the hysteresis.
    CHECK_FALSE(portrait_graph_fits(0, /*shown=*/false));
    CHECK_FALSE(portrait_graph_fits(0, /*shown=*/true));

    // Landscape reports 0 explicitly rather than leaving a portrait reading
    // latched, so rotating away must drop the graph even from the shown state.
    const int32_t landscape = portrait_preview_slack(portrait_preview_card_max_height(380, 0),
                                                     /*avail_h=*/392, /*gap=*/8);
    CHECK(landscape == 0);
    CHECK_FALSE(portrait_graph_fits(landscape, /*shown=*/true));
}

TEST_CASE("portrait_graph_fits: the dead band is asymmetric and cannot oscillate",
          "[print-status][portrait][layout-decision][temp-graph]") {
    const int32_t on = kMinTempGraphHeightPx + kTempGraphFitHysteresisPx;

    // Cheaper to keep showing than to start showing — that asymmetry IS the
    // anti-oscillation guarantee.
    CHECK(kTempGraphFitHysteresisPx > 0);
    CHECK_FALSE(portrait_graph_fits(kMinTempGraphHeightPx, /*shown=*/false));
    CHECK(portrait_graph_fits(kMinTempGraphHeightPx, /*shown=*/true));

    // Just under the floor drops it no matter what state we were in.
    CHECK_FALSE(portrait_graph_fits(kMinTempGraphHeightPx - 1, /*shown=*/true));
    CHECK_FALSE(portrait_graph_fits(kMinTempGraphHeightPx - 1, /*shown=*/false));

    // Just over the turn-on threshold shows it, and staying there keeps it.
    CHECK(portrait_graph_fits(on, /*shown=*/false));
    CHECK(portrait_graph_fits(on, /*shown=*/true));

    // The real test: a slack parked anywhere inside the dead band must be a
    // FIXED POINT for both states. If either flipped, feeding the result back in
    // would flip it again forever — that is the oscillation this guards.
    for (int32_t h = kMinTempGraphHeightPx; h < on; ++h) {
        INFO("slack=" << h);
        CHECK(portrait_graph_fits(h, /*shown=*/true) == true);
        CHECK(portrait_graph_fits(h, /*shown=*/false) == false);
    }
}

TEST_CASE("portrait_graph_fits: the floor is one constant and it is a real floor",
          "[print-status][portrait][layout-decision][temp-graph]") {
    // Retuning kMinTempGraphHeightPx must move the whole gate; nothing else may
    // encode the threshold. Derive expectations from the constants.
    CHECK(kMinTempGraphHeightPx > 0);
    // A graph shorter than the fan row it shares a column with is a smear.
    CHECK(kMinTempGraphHeightPx >= 48);
    // ...and the floor must not be so tall that the one size this exists for
    // fails it. 320x1480 is the whole point of the feature.
    const int32_t ultratall =
        portrait_preview_slack(portrait_preview_card_max_height(302, 174), 1090, 8);
    CHECK(ultratall >= kMinTempGraphHeightPx + kTempGraphFitHysteresisPx);
}

// ---------------------------------------------------------------------------
// portrait_graph_height
// ---------------------------------------------------------------------------

TEST_CASE("portrait_graph_height: the ultratall slack caps the graph square",
          "[print-status][portrait][layout-decision][temp-graph]") {
    // THE CASE THIS EXISTS FOR. 320x1480 parks ~709px in the absorber against a
    // 302px-wide graph. Filling it gives a 302x709 plot: a reheat is a vertical
    // wall and steady state is three flat lines in half a screen of void.
    const int32_t slack =
        portrait_preview_slack(portrait_preview_card_max_height(302, 174), /*avail_h=*/1090,
                               /*gap=*/8);
    CHECK(slack > 302); // the cap has something to bite on
    CHECK(portrait_graph_height(/*graph_w=*/302, slack) == 302);
    // ...and the remainder is real background, not a rounding artifact.
    CHECK(slack - portrait_graph_height(302, slack) > 100);
}

TEST_CASE("portrait_graph_height: a slack under the cap is taken whole",
          "[print-status][portrait][layout-decision][temp-graph]") {
    // A band shorter than it is wide is a perfectly good strip chart, so the cap
    // must not pad it out to square — it is a ceiling, never a floor.
    CHECK(portrait_graph_height(/*graph_w=*/302, /*slack_h=*/200) == 200);
    CHECK(portrait_graph_height(302, kMinTempGraphHeightPx) == kMinTempGraphHeightPx);
    // Narrow screens cap sooner because the ceiling follows the width.
    CHECK(portrait_graph_height(/*graph_w=*/254, /*slack_h=*/600) == 254);
}

TEST_CASE("portrait_graph_height: exactly at the ceiling, and either side of it",
          "[print-status][portrait][layout-decision][temp-graph]") {
    const int32_t w = 302;
    const int32_t cap = w * kMaxGraphAspectPct / 100;
    CHECK(portrait_graph_height(w, cap - 1) == cap - 1); // just under: take it all
    CHECK(portrait_graph_height(w, cap) == cap);         // exactly on: unchanged
    CHECK(portrait_graph_height(w, cap + 1) == cap);     // just over: clamped
}

TEST_CASE("portrait_graph_height: nothing to size means nothing to decide",
          "[print-status][portrait][layout-decision][temp-graph]") {
    // 0 is the "leave the height alone" sentinel, same contract as
    // portrait_preview_card_max_height(). Every size where the aspect cap does
    // not bind reports zero slack, and landscape reports zero explicitly.
    CHECK(portrait_graph_height(302, 0) == 0);
    CHECK(portrait_graph_height(302, -1) == 0);
    // A hidden or not-yet-laid-out container measures 0 wide. Scaling that would
    // collapse the graph to nothing on the first pass.
    CHECK(portrait_graph_height(0, 709) == 0);
    CHECK(portrait_graph_height(-1, 709) == 0);
}

TEST_CASE("portrait_graph_height: capped is not the same question as fits",
          "[print-status][portrait][layout-decision][temp-graph]") {
    // The two gates are independent and must stay that way: fits() is a FLOOR on
    // the slack, height() is a CEILING on the graph. A graph that fits but is
    // capped is the normal ultratall case; a slack under the floor stays hidden
    // however tall the ceiling would allow.
    const int32_t slack = 709;
    CHECK(portrait_graph_fits(slack, /*shown=*/false));
    CHECK(portrait_graph_height(302, slack) < slack); // fits AND capped

    const int32_t too_short = kMinTempGraphHeightPx - 1;
    CHECK_FALSE(portrait_graph_fits(too_short, /*shown=*/false));
    CHECK_FALSE(portrait_graph_fits(too_short, /*shown=*/true));

    // Whenever the graph is shown it is at least the floor tall — the cap can
    // never shrink it below what the gate promised, because the cap only binds
    // above the width and the width is far over the floor at every portrait size.
    for (int32_t w : {254, 302, 446}) {
        INFO("graph_w=" << w);
        CHECK(portrait_graph_height(w, kMinTempGraphHeightPx) >= kMinTempGraphHeightPx);
    }
}

TEST_CASE("the graph aspect cap is one constant, and it actually caps",
          "[print-status][portrait][layout-decision][temp-graph]") {
    // Retuning kMaxGraphAspectPct must move the whole behaviour — nothing else
    // may encode the number. Derive the expectation from the constant.
    CHECK(kMaxGraphAspectPct > 0);
    // A time-series plot taller than square distorts the value axis against an
    // already-compressed time axis; that is the whole rationale for the cap.
    CHECK(kMaxGraphAspectPct <= 100);
    for (int32_t w : {254, 302, 446}) {
        INFO("graph_w=" << w);
        CHECK(portrait_graph_height(w, /*slack_h=*/9999) == w * kMaxGraphAspectPct / 100);
    }
}

TEST_CASE("the aspect cap is one constant, and it actually caps",
          "[print-status][portrait][layout-decision]") {
    // Retuning kMaxPreviewAspectPct must move the whole behaviour with it —
    // nothing else may encode the number. Derive the expectation from the
    // constant rather than restating 130.
    CHECK(kMaxPreviewAspectPct > 100); // a band taller than wide is still allowed
    CHECK(kMaxPreviewAspectPct < 400); // ...but not the ultratall free-for-all
    for (int32_t w : {254, 302, 380, 446, 592}) {
        INFO("card_w=" << w);
        CHECK(portrait_preview_card_max_height(w, 0) == w * kMaxPreviewAspectPct / 100);
    }
}
