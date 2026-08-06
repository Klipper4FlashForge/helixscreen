// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_widget_size_indicators.cpp
 * @brief humidity and width_sensor pick their font from physical width.
 *
 * Each case passes a colspan that contradicts the width, so a widget still
 * reading the span fails rather than passing by coincidence.
 */

#include "../lvgl_ui_test_fixture.h"
#include "../test_helpers/panel_widget_size_harness.h"
#include "panel_widget_size.h"
#include "src/ui/panel_widgets/humidity_widget.h"
#include "src/ui/panel_widgets/width_sensor_widget.h"

#include "../catch_amalgamated.hpp"

using namespace helix;
using namespace helix::widget_size;

TEST_CASE_METHOD(LVGLUITestFixture, "humidity value font follows width, not colspan",
                 "[widget_size][humidity]") {
    require_font_tokens_distinct();

    PanelWidgetHarness<HumidityWidget> h(test_screen());
    lv_obj_t* value = h.child("humidity_value");
    REQUIRE(value != nullptr);

    // Narrow pixels with a large colspan: must stay compact.
    h.resize(4, 4, W_NORMAL - 1, H_TALL - 1);
    REQUIRE(lv_obj_get_style_text_font(value, LV_PART_MAIN) == theme_manager_get_font("font_xs"));

    // Wide pixels with colspan 1: must go wide.
    h.resize(1, 1, W_NORMAL, H_TALL);
    REQUIRE(lv_obj_get_style_text_font(value, LV_PART_MAIN) == theme_manager_get_font("font_body"));
}

TEST_CASE_METHOD(LVGLUITestFixture, "width_sensor value font follows width, not colspan",
                 "[widget_size][width_sensor]") {
    require_font_tokens_distinct();

    PanelWidgetHarness<WidthSensorWidget> h(test_screen());
    lv_obj_t* value = h.child("width_value");
    REQUIRE(value != nullptr);

    h.resize(4, 4, W_NORMAL - 1, H_TALL - 1);
    REQUIRE(lv_obj_get_style_text_font(value, LV_PART_MAIN) == theme_manager_get_font("font_xs"));

    h.resize(1, 1, W_NORMAL, H_TALL);
    REQUIRE(lv_obj_get_style_text_font(value, LV_PART_MAIN) == theme_manager_get_font("font_body"));
}
