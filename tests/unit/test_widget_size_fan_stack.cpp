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
 * restyles the icon's font directly. The `icon` XML widget
 * (`ui_icon_xml_create`, `ui_icon.cpp`) is itself an `lv_label` — there is
 * no child glyph object beneath it. This branch used to go through
 * `lv_obj_get_child(icon, 0)`, which is always null for a childless label,
 * so the restyle silently never applied; fixed to style the icon object
 * itself. That makes fan_stack icons scale with the widget for the first
 * time (16px xs / 24px sm, same tiers as the text), a real visible change
 * covered by the font assertions below.
 *
 * `bigger = (width_px >= W_NORMAL || height_px >= H_TALL)`. Three cases
 * isolate the two independent terms of that OR: neither term true (large
 * span, sub-threshold pixels — proves pixels win over span), width alone,
 * and height alone. A predicate that dropped either term would still pass
 * the "neither" case but fail exactly one of the other two.
 *
 * Row geometry (`fan_stack_{part,hotend,aux}_row` width/flex alignment) is
 * also driven by this predicate: compact rows are pinned to `LV_PCT(100)`
 * of the parent's content box; bigger rows are measured at
 * `LV_SIZE_CONTENT` and then widened to the widest row's measured content
 * width so all rows share a left edge. Empirically (`style_min_width="80"`
 * on the widget's own view, no explicit width, under the 800x480 test
 * display) the *parent* itself is content-sized and floors out at that
 * 80px minimum while the row is still compact (single-letter names, xs
 * icon/font) — so `LV_PCT(100)` measures smaller than what the same row's
 * content needs once it carries the full display name and the larger
 * sm icon/font. Net effect: the bigger row is measured *wider* than the
 * compact one, not narrower — content growth outpaces the fixed-vs-fluid
 * width mechanism. That is the actual, verified behavior; do not assume
 * the opposite from the "shrink to content" description above without
 * re-measuring against the real display size in use.
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

    lv_obj_t* part_icon = h.child("fan_stack_part_icon");
    lv_obj_t* hotend_icon = h.child("fan_stack_hotend_icon");
    lv_obj_t* aux_icon = h.child("fan_stack_aux_icon");
    REQUIRE(part_icon != nullptr);
    REQUIRE(hotend_icon != nullptr);
    REQUIRE(aux_icon != nullptr);

    lv_obj_t* part_row = h.child("fan_stack_part_row");
    REQUIRE(part_row != nullptr);

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

    CHECK(lv_obj_get_style_text_font(part_icon, LV_PART_MAIN) == &mdi_icons_16);
    CHECK(lv_obj_get_style_text_font(hotend_icon, LV_PART_MAIN) == &mdi_icons_16);
    CHECK(lv_obj_get_style_text_font(aux_icon, LV_PART_MAIN) == &mdi_icons_16);

    // Compact: row fills the full column width (LV_PCT(100)), content centered.
    int compact_row_w = lv_obj_get_width(part_row);
    CHECK(compact_row_w == lv_obj_get_content_width(h.root()));

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

    CHECK(lv_obj_get_style_text_font(part_icon, LV_PART_MAIN) == &mdi_icons_24);
    CHECK(lv_obj_get_style_text_font(hotend_icon, LV_PART_MAIN) == &mdi_icons_24);
    CHECK(lv_obj_get_style_text_font(aux_icon, LV_PART_MAIN) == &mdi_icons_24);

    // Bigger: row is measured at its own content width (icon 24px, full
    // display-name text, sm font) rather than pinned to the parent's
    // LV_PCT(100). That content is wider than the compact form measured
    // above (see file header) — this is width GROWING with "bigger", not
    // shrinking; a row that stayed pinned to the compact width would fail
    // this the same way a row that failed to grow at all would.
    CHECK(lv_obj_get_width(part_row) > compact_row_w);

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

    CHECK(lv_obj_get_style_text_font(part_icon, LV_PART_MAIN) == &mdi_icons_24);
    CHECK(lv_obj_get_style_text_font(hotend_icon, LV_PART_MAIN) == &mdi_icons_24);
    CHECK(lv_obj_get_style_text_font(aux_icon, LV_PART_MAIN) == &mdi_icons_24);

    CHECK(lv_obj_get_width(part_row) > compact_row_w);
}

/**
 * Carousel mode takes the early return at the top of on_size_changed
 * (`!widget_obj_ || is_carousel_mode()`), below which Task 4's pixel
 * predicate lives. This proves two things: the carousel component is
 * genuinely what gets built (not the stack component silently substituted
 * by a harness that forgot set_config()), and driving resize() across the
 * same W_NORMAL/H_TALL threshold used above never reaches that predicate.
 *
 * `fan_stack_part_name`/`fan_stack_part_speed`/`fan_stack_part_row` (the
 * objects the stack-mode test above asserts on) do not exist anywhere in
 * `panel_widget_fan_carousel.xml` or the `fan_arc_core` component it embeds
 * — confirmed below by asserting the lookups miss. So the strong per-object
 * "this label's font moved" check from the stack test is not available
 * here; there is nothing named that to check. What carousel mode DOES have,
 * from bind_carousel_fans() (fan_stack_widget.cpp), is a `speed_label`
 * (text_heading) and `fan_icon` per carousel page, explicitly given
 * "font_xs" at construction time — those are the closest carousel
 * equivalent of the stack test's font assertions, so this test pins them
 * instead.
 *
 * Mutation testing this against `is_carousel_mode()` removed from the guard
 * (leaving only `!widget_obj_`) does NOT turn this test red: every branch in
 * on_size_changed's body is gated behind either a `fan_stack_*` name lookup
 * (all miss, per the CHECKs above) or a `part_label_`/`hotend_label_`/
 * `aux_label_`/`part_icon_`/`hotend_icon_`/`aux_icon_` member pointer, none
 * of which attach_carousel() ever populates — those stay their default
 * nullptr for a carousel-mode widget. So the whole function body is
 * unreachable from a carousel-built widget regardless of the guard, and
 * `is_carousel_mode()` is defensive rather than load-bearing *as far as this
 * test's own observables go* — it does not prove no other path could ever
 * depend on it. Recorded here rather than papering over it with an
 * assertion that happens to fail.
 */
TEST_CASE_METHOD(LVGLUITestFixture, "fan_stack carousel mode ignores the pixel size predicate",
                 "[widget_size][fan_stack]") {
    require_font_tokens_distinct();

    PanelWidgetHarness<FanStackWidget> h(
        test_screen(), HarnessConfig{{{"display_mode", "carousel"}}}, "fan_stack", state());

    // Proves the carousel component was actually built, not the stack one
    // (see harness's own test for what a missing HarnessConfig would do).
    REQUIRE(h.child("fan_carousel") != nullptr);

    // The stack-only names on_size_changed's pixel predicate would move
    // simply do not exist under the carousel component.
    CHECK(h.child("fan_stack_part_name") == nullptr);
    CHECK(h.child("fan_stack_part_speed") == nullptr);
    CHECK(h.child("fan_stack_part_row") == nullptr);

    // Placeholder fan entries (no fans discovered under this fixture) still
    // produce a carousel page with a real speed_label/fan_icon pair.
    lv_obj_t* speed_label = h.child("speed_label");
    lv_obj_t* fan_icon = h.child("fan_icon");
    REQUIRE(speed_label != nullptr);
    REQUIRE(fan_icon != nullptr);

    const lv_font_t* xs_font = theme_manager_get_font("font_xs");
    const lv_font_t* speed_font_before = lv_obj_get_style_text_font(speed_label, LV_PART_MAIN);
    std::string speed_text_before = lv_label_get_text(speed_label);
    CHECK(speed_font_before == xs_font);

    // Drive resize() past the threshold in both directions — the same
    // stimuli that flip the stack test's labels to font_small/full names.
    h.resize(4, 4, W_NORMAL - 1, H_TALL - 1);
    process_lvgl(30);
    CHECK(lv_obj_get_style_text_font(speed_label, LV_PART_MAIN) == speed_font_before);
    CHECK(std::string(lv_label_get_text(speed_label)) == speed_text_before);

    h.resize(1, 1, W_NORMAL, H_TALL - 1);
    process_lvgl(30);
    CHECK(lv_obj_get_style_text_font(speed_label, LV_PART_MAIN) == speed_font_before);
    CHECK(std::string(lv_label_get_text(speed_label)) == speed_text_before);

    h.resize(1, 1, W_NORMAL - 1, H_TALL);
    process_lvgl(30);
    CHECK(lv_obj_get_style_text_font(speed_label, LV_PART_MAIN) == speed_font_before);
    CHECK(std::string(lv_label_get_text(speed_label)) == speed_text_before);
}
