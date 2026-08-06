// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_widget_size_fan_stack.cpp
 * @brief fan_stack picks label fonts and name-label text from physical
 * pixels, not colspan/rowspan.
 *
 * `on_size_changed` (fan_stack_widget.cpp, stack mode only) restyles:
 *   - Speed labels `fan_stack_{part,hotend,aux}_speed`: text font
 *     `font_small` when bigger, `font_xs` otherwise.
 *   - Name labels `fan_stack_{part,hotend,aux}_name`: same font switch as
 *     the speed labels, **and** text — `lv_tr("P"/"H"/"C")` (single letter)
 *     when compact, the resolved fan display name (or a hardcoded English
 *     fallback when no fan of that role was discovered) when bigger. The
 *     text assertion is the stronger one: a font can leak in from a stray
 *     style, but the text string cannot.
 *
 * `on_size_changed` also loops over `fan_stack_{part,hotend,aux}_icon` and
 * restyles `lv_obj_get_child(icon, 0)` — but the `icon` XML widget (see
 * `ui_icon.cpp`) creates a single `lv_label`, never a child. That child
 * lookup is always null, so this branch is dead: the icon glyph font never
 * actually changes today, under either the old or the new predicate. Not
 * asserted here since there is no observable effect to assert — a
 * pre-existing bug, unrelated to and unchanged by the span-to-pixel
 * migration this test covers.
 *
 * `bigger = (width_px >= W_NORMAL || height_px >= H_TALL)`. Three cases
 * isolate the two independent terms of that OR: neither term true (large
 * span, sub-threshold pixels — proves pixels win over span), width alone,
 * and height alone. A predicate that dropped either term would still pass
 * the "neither" case but fail exactly one of the other two.
 */

#include "ui_fonts.h"

#include "../lvgl_ui_test_fixture.h"
#include "../test_helpers/panel_widget_size_harness.h"
#include "panel_widget_size.h"
#include "src/ui/panel_widgets/fan_stack_widget.h"

#include "../catch_amalgamated.hpp"

using namespace helix;
using namespace helix::widget_size;

TEST_CASE_METHOD(LVGLUITestFixture, "fan_stack labels/names follow pixels, not spans",
                 "[widget_size][fan_stack]") {
    require_font_tokens_distinct();

    PanelWidgetHarness<FanStackWidget> h(test_screen(), "fan_stack", state());

    lv_obj_t* part_speed = h.child("fan_stack_part_speed");
    lv_obj_t* hotend_speed = h.child("fan_stack_hotend_speed");
    lv_obj_t* aux_speed = h.child("fan_stack_aux_speed");
    REQUIRE(part_speed != nullptr);
    REQUIRE(hotend_speed != nullptr);
    REQUIRE(aux_speed != nullptr);

    lv_obj_t* part_name = h.child("fan_stack_part_name");
    lv_obj_t* hotend_name = h.child("fan_stack_hotend_name");
    lv_obj_t* aux_name = h.child("fan_stack_aux_name");
    REQUIRE(part_name != nullptr);
    REQUIRE(hotend_name != nullptr);
    REQUIRE(aux_name != nullptr);

    // No fans are discovered under this fixture (PrinterState starts with
    // an empty fan list), so bind_fans() never populates the display-name
    // strings and on_size_changed falls back to the hardcoded English
    // names. That fallback still differs from the compact single letter,
    // so it is a valid target for the "bigger" text assertion.
    const char* part_bigger_text = lv_tr("Part");
    const char* hotend_bigger_text = lv_tr("Hotend");
    const char* aux_bigger_text = lv_tr("Chamber");

    // --- Neither flag: large span, sub-threshold pixels on both axes. ---
    // A span-reading implementation would go "bigger" here; pixels must win.
    h.resize(4, 4, W_NORMAL - 1, H_TALL - 1);
    process_lvgl(30);

    CHECK(lv_obj_get_style_text_font(part_speed, LV_PART_MAIN) ==
          theme_manager_get_font("font_xs"));
    CHECK(lv_obj_get_style_text_font(hotend_speed, LV_PART_MAIN) ==
          theme_manager_get_font("font_xs"));
    CHECK(lv_obj_get_style_text_font(aux_speed, LV_PART_MAIN) == theme_manager_get_font("font_xs"));

    CHECK(lv_obj_get_style_text_font(part_name, LV_PART_MAIN) == theme_manager_get_font("font_xs"));
    CHECK(std::string(lv_label_get_text(part_name)) == lv_tr("P"));
    CHECK(std::string(lv_label_get_text(hotend_name)) == lv_tr("H"));
    CHECK(std::string(lv_label_get_text(aux_name)) == lv_tr("C"));

    // --- Width alone: at the width threshold, height still sub-threshold. ---
    h.resize(1, 1, W_NORMAL, H_TALL - 1);
    process_lvgl(30);

    CHECK(lv_obj_get_style_text_font(part_speed, LV_PART_MAIN) ==
          theme_manager_get_font("font_small"));
    CHECK(lv_obj_get_style_text_font(hotend_speed, LV_PART_MAIN) ==
          theme_manager_get_font("font_small"));
    CHECK(lv_obj_get_style_text_font(aux_speed, LV_PART_MAIN) ==
          theme_manager_get_font("font_small"));

    CHECK(lv_obj_get_style_text_font(part_name, LV_PART_MAIN) ==
          theme_manager_get_font("font_small"));
    CHECK(std::string(lv_label_get_text(part_name)) == part_bigger_text);
    CHECK(std::string(lv_label_get_text(hotend_name)) == hotend_bigger_text);
    CHECK(std::string(lv_label_get_text(aux_name)) == aux_bigger_text);

    // --- Height alone: at the height threshold, width still sub-threshold. ---
    h.resize(1, 1, W_NORMAL - 1, H_TALL);
    process_lvgl(30);

    CHECK(lv_obj_get_style_text_font(part_speed, LV_PART_MAIN) ==
          theme_manager_get_font("font_small"));
    CHECK(lv_obj_get_style_text_font(hotend_speed, LV_PART_MAIN) ==
          theme_manager_get_font("font_small"));
    CHECK(lv_obj_get_style_text_font(aux_speed, LV_PART_MAIN) ==
          theme_manager_get_font("font_small"));

    CHECK(lv_obj_get_style_text_font(part_name, LV_PART_MAIN) ==
          theme_manager_get_font("font_small"));
    CHECK(std::string(lv_label_get_text(part_name)) == part_bigger_text);
    CHECK(std::string(lv_label_get_text(hotend_name)) == hotend_bigger_text);
    CHECK(std::string(lv_label_get_text(aux_name)) == aux_bigger_text);
}
