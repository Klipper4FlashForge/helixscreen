// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_context_menu_row_width.cpp
 * @brief Context menu rows are tappable across the whole card, not just their text.
 *
 * A context menu card is sized `width="content"`, so it ends up as wide as its
 * longest row. Every shorter row kept its own natural width, leaving dead space
 * to its right that looked clickable and was not — a tap on the empty half of
 * "Load" fell through to the backdrop and dismissed the menu.
 *
 * The declarative fix does not exist: LVGL flex has no cross-axis stretch, and
 * `width="100%"` drops a child out of the parent's content-width calculation
 * (`w_ignore_size`, lv_obj_pos.c), collapsing the card to its widest remaining
 * non-percentage child. ContextMenu therefore measures the card once the rows
 * are built and widens each tappable row to that measurement.
 */

#include "ui_context_menu.h"

#include "ams_state.h"

#include "../catch_amalgamated.hpp"
#include "../test_fixtures.h"

using namespace helix;

namespace {

// Drives the base-class show path against a real menu layout. The AMS menu is
// used because it is the widest mix of row lengths ("Load" vs "Spool Info").
class BareContextMenu : public helix::ui::ContextMenu {
  protected:
    const char* xml_component_name() const override {
        return "ams_context_menu";
    }
    const char* menu_card_name() const override {
        return "context_menu";
    }
};

// Widths of the rows a user can actually tap, in child order.
std::vector<int32_t> tappable_row_widths(lv_obj_t* card) {
    std::vector<int32_t> widths;
    for (uint32_t i = 0; i < lv_obj_get_child_count(card); i++) {
        lv_obj_t* row = lv_obj_get_child(card, i);
        if (!row || lv_obj_has_flag(row, LV_OBJ_FLAG_HIDDEN))
            continue;
        if (!lv_obj_has_flag(row, LV_OBJ_FLAG_CLICKABLE))
            continue;
        widths.push_back(lv_obj_get_width(row));
    }
    return widths;
}

} // namespace

// Establishes the premise the fix exists for: left alone, the card's rows really
// do come out at different widths. Without this the width assertions below would
// pass just as happily on a menu whose rows were already uniform.
TEST_CASE_METHOD(XMLTestFixture, "context menu: raw card leaves short rows narrower than the card",
                 "[ui][context_menu]") {
    REQUIRE(register_component("ams_context_menu"));
    AmsState::instance().init_subjects(true);

    lv_obj_t* menu = create_component("ams_context_menu");
    REQUIRE(menu != nullptr);

    lv_obj_t* card = lv_obj_find_by_name(menu, "context_menu");
    REQUIRE(card != nullptr);
    lv_obj_update_layout(card);

    const int32_t content_w = lv_obj_get_content_width(card);
    REQUIRE(content_w > 0);

    const std::vector<int32_t> widths = tappable_row_widths(card);
    REQUIRE(widths.size() >= 2);

    const int32_t narrowest = *std::min_element(widths.begin(), widths.end());
    CHECK(narrowest < content_w);
}

TEST_CASE_METHOD(XMLTestFixture, "context menu: every tappable row spans the card's content width",
                 "[ui][context_menu]") {
    REQUIRE(register_component("ams_context_menu"));
    AmsState::instance().init_subjects(true);

    BareContextMenu menu;
    menu.set_click_point({100, 100});
    REQUIRE(menu.show_near_widget(test_screen(), 0, test_screen()));

    lv_obj_t* card = lv_obj_find_by_name(test_screen(), "context_menu");
    REQUIRE(card != nullptr);
    lv_obj_update_layout(card);

    const int32_t content_w = lv_obj_get_content_width(card);
    REQUIRE(content_w > 0);

    const std::vector<int32_t> widths = tappable_row_widths(card);
    REQUIRE(widths.size() >= 2);

    for (size_t i = 0; i < widths.size(); i++) {
        INFO("tappable row " << i << " of " << widths.size());
        CHECK(widths[i] == content_w);
    }

    menu.hide();
    process_lvgl(50); // flush the deferred delete before the fixture tears down
}

// Stretching rows must not feed back into the card: each row is set to the
// content width the card already had, so a second layout pass finds the same
// number rather than growing by the padding every time the menu opens.
TEST_CASE_METHOD(XMLTestFixture, "context menu: stretching rows leaves the card width alone",
                 "[ui][context_menu]") {
    REQUIRE(register_component("ams_context_menu"));
    AmsState::instance().init_subjects(true);

    lv_obj_t* raw = create_component("ams_context_menu");
    REQUIRE(raw != nullptr);
    lv_obj_t* raw_card = lv_obj_find_by_name(raw, "context_menu");
    REQUIRE(raw_card != nullptr);
    lv_obj_update_layout(raw_card);
    const int32_t natural_w = lv_obj_get_width(raw_card);
    REQUIRE(natural_w > 0);
    lv_obj_delete(raw);

    BareContextMenu menu;
    menu.set_click_point({100, 100});
    REQUIRE(menu.show_near_widget(test_screen(), 0, test_screen()));

    lv_obj_t* card = lv_obj_find_by_name(test_screen(), "context_menu");
    REQUIRE(card != nullptr);
    lv_obj_update_layout(card);
    CHECK(lv_obj_get_width(card) == natural_w);

    menu.hide();
    process_lvgl(50);
}
