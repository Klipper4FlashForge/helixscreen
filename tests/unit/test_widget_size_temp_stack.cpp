// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_widget_size_temp_stack.cpp
 * @brief temp_stack picks its text and icon fonts from physical height, not
 * rowspan.
 *
 * `on_size_changed` (temp_stack_widget.cpp, near :239) computes
 * `size = (height_px >= h_tall()) ? "sm" : "xs"` and reuses the same predicate
 * for the icon font (`mdi_icons_24` vs `mdi_icons_16`). It restyles three
 * kinds of object, all asserted here as real widget state (applied fonts),
 * since the "xs"/"sm" token itself is only a local variable — there is no
 * member or attribute that stores it:
 *   - the nozzle/bed/chamber heater icon glyphs (`nozzle_icon_glyph`,
 *     `bed_icon_glyph`, `chamber_icon_glyph`) — all three are set, the first
 *     two by direct name lookup and the chamber one through the per-row
 *     loop's "icon component" branch (temp_stack_widget.cpp:282-286)
 *   - every label inside each row's `temp_display` (current + unit; the rows
 *     here use `show_target="false"`, so there is no separator/target label)
 *
 * Each resize pairs pixels with a rowspan that would pick the OPPOSITE size
 * under the old `rowspan >= 2` predicate, so an implementation that still
 * reads rowspan fails here instead of passing by coincidence — same
 * technique as test_widget_size_active_spool.cpp.
 */

#include "ui_fonts.h"

#include "../lvgl_ui_test_fixture.h"
#include "../test_helpers/panel_widget_size_harness.h"
#include "panel_widget_size.h"
#include "src/ui/panel_widgets/temp_stack_widget.h"

#include "../catch_amalgamated.hpp"

using namespace helix;
using namespace helix::widget_size;

TEST_CASE_METHOD(LVGLUITestFixture, "temp_stack text and icon fonts follow height, not rowspan",
                 "[widget_size][temp_stack][panel_widget]") {
    // Sanity: the two size tokens this widget switches between must actually
    // resolve to different fonts, or every assertion below would trivially
    // agree regardless of which branch ran.
    REQUIRE(theme_manager_get_font("font_xs") != theme_manager_get_font("font_small"));

    PanelWidgetHarness<TempStackWidget> h(test_screen(), state(), nullptr);

    lv_obj_t* nozzle_row = h.child("temp_stack_nozzle_row");
    lv_obj_t* bed_row = h.child("temp_stack_bed_row");
    REQUIRE(nozzle_row != nullptr);
    REQUIRE(bed_row != nullptr);

    lv_obj_t* nozzle_glyph = h.child("nozzle_icon_glyph");
    lv_obj_t* bed_glyph = h.child("bed_icon_glyph");
    lv_obj_t* chamber_glyph = h.child("chamber_icon_glyph");
    REQUIRE(nozzle_glyph != nullptr);
    REQUIRE(bed_glyph != nullptr);
    REQUIRE(chamber_glyph != nullptr);

    // Row layout is [icon component, temp_display] (panel_widget_temp_stack.xml)
    // — the temp_display instances carry no `name` attribute, so they are
    // reached by position rather than lv_obj_find_by_name().
    lv_obj_t* nozzle_temp_display = lv_obj_get_child(nozzle_row, 1);
    lv_obj_t* bed_temp_display = lv_obj_get_child(bed_row, 1);
    REQUIRE(nozzle_temp_display != nullptr);
    REQUIRE(bed_temp_display != nullptr);

    // show_target="false" -> temp_display's only children are current_label
    // (index 0) and unit_label (index 1); both get restyled.
    lv_obj_t* nozzle_current_label = lv_obj_get_child(nozzle_temp_display, 0);
    lv_obj_t* nozzle_unit_label = lv_obj_get_child(nozzle_temp_display, 1);
    lv_obj_t* bed_current_label = lv_obj_get_child(bed_temp_display, 0);
    lv_obj_t* bed_unit_label = lv_obj_get_child(bed_temp_display, 1);
    REQUIRE(nozzle_current_label != nullptr);
    REQUIRE(nozzle_unit_label != nullptr);
    REQUIRE(bed_current_label != nullptr);
    REQUIRE(bed_unit_label != nullptr);

    auto check_size = [&](const char* font_token, const lv_font_t* icon_font) {
        const lv_font_t* text_font = theme_manager_get_font(font_token);
        CHECK(lv_obj_get_style_text_font(nozzle_glyph, LV_PART_MAIN) == icon_font);
        CHECK(lv_obj_get_style_text_font(bed_glyph, LV_PART_MAIN) == icon_font);
        CHECK(lv_obj_get_style_text_font(chamber_glyph, LV_PART_MAIN) == icon_font);
        CHECK(lv_obj_get_style_text_font(nozzle_current_label, LV_PART_MAIN) == text_font);
        CHECK(lv_obj_get_style_text_font(nozzle_unit_label, LV_PART_MAIN) == text_font);
        CHECK(lv_obj_get_style_text_font(bed_current_label, LV_PART_MAIN) == text_font);
        CHECK(lv_obj_get_style_text_font(bed_unit_label, LV_PART_MAIN) == text_font);
    };

    // Below threshold, rowspan agrees (1): "xs" / mdi_icons_16.
    h.resize(1, 1, 150, h_tall() - 1);
    check_size("font_xs", &mdi_icons_16);

    // At threshold, rowspan agrees (2): "sm" / mdi_icons_24.
    h.resize(1, 2, 150, h_tall());
    check_size("font_small", &mdi_icons_24);

    // Contradicting rowspan: rowspan=1 (old predicate -> "xs") but tall
    // pixels -- the pixel verdict ("sm") must win.
    h.resize(1, 1, 150, 200);
    check_size("font_small", &mdi_icons_24);

    // Contradicting rowspan: rowspan=4 (old predicate -> "sm") but short
    // pixels -- the pixel verdict ("xs") must win.
    h.resize(1, 4, 150, 80);
    check_size("font_xs", &mdi_icons_16);
}
