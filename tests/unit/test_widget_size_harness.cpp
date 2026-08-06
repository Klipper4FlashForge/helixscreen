// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_widget_size_harness.cpp
 * @brief The harness itself: does it build the right component and attach?
 */

#include "../lvgl_ui_test_fixture.h"
#include "../test_helpers/panel_widget_size_harness.h"
#include "src/ui/panel_widgets/tips_widget.h"

#include "../catch_amalgamated.hpp"

using namespace helix;

TEST_CASE_METHOD(LVGLUITestFixture, "harness builds the widget's own component",
                 "[widget_size][harness]") {
    PanelWidgetHarness<TipsWidget> h(test_screen());
    REQUIRE(h.root() != nullptr);
    REQUIRE(h.child("status_text_label") != nullptr);
}

TEST_CASE_METHOD(LVGLUITestFixture, "font tokens resolve distinctly under this fixture",
                 "[widget_size][harness]") {
    // If this fails, every font assertion in this plan is vacuous.
    require_font_tokens_distinct();
}
