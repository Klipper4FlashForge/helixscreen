// SPDX-License-Identifier: GPL-3.0-or-later
// Hit-testing for print_file_card: the whole card face must resolve to card_root,
// which owns the CLICKED/LONG_PRESSED handlers (ui_print_select_card_view.cpp).
// Decorative children (metadata overlay and its rows) must not absorb the press.
// Regression: prestonbrown/helixscreen#1101

#include "../test_fixtures.h"

#include "../catch_amalgamated.hpp"

#include <string>

namespace {

// Resolve the object LVGL's input layer would deliver a press at (x, y) to.
lv_obj_t* hit(lv_obj_t* screen, int32_t x, int32_t y) {
    lv_point_t p{x, y};
    return lv_indev_search_obj(screen, &p);
}

// Centre point of a widget in absolute screen coords.
lv_point_t centre_of(lv_obj_t* obj) {
    lv_area_t a;
    lv_obj_get_coords(obj, &a);
    return lv_point_t{(a.x1 + a.x2) / 2, (a.y1 + a.y2) / 2};
}

std::string name_of(lv_obj_t* obj) {
    if (obj == nullptr) return "<null>";
    const char* n = lv_obj_get_name(obj);
    return n != nullptr ? n : "<unnamed>";
}

} // namespace

TEST_CASE_METHOD(XMLTestFixture, "print_file_card: metadata area routes clicks to card_root",
                 "[xml][print_file_card][hittest]") {
    REQUIRE(register_component("print_file_card"));

    const char* attrs[] = {"filename",         "some_long_model_name.gcode",
                           "print_time",       "1h 20m",
                           "filament_weight",  "24g",
                           nullptr};
    lv_obj_t* card = create_component("print_file_card", attrs);
    REQUIRE(card != nullptr);

    // Layout must settle before coordinates mean anything.
    lv_obj_update_layout(card);

    lv_obj_t* overlay = lv_obj_find_by_name(card, "metadata_overlay");
    lv_obj_t* filename = lv_obj_find_by_name(card, "filename_label");
    lv_obj_t* row = lv_obj_find_by_name(card, "metadata_row");
    REQUIRE(overlay != nullptr);
    REQUIRE(filename != nullptr);
    REQUIRE(row != nullptr);

    SECTION("thumbnail area works today (control)") {
        lv_area_t c;
        lv_obj_get_coords(card, &c);
        // A point near the top of the card, well above the metadata overlay.
        lv_obj_t* got = hit(test_screen(), (c.x1 + c.x2) / 2, c.y1 + 10);
        INFO("hit near card top resolved to: " << name_of(got));
        CHECK(got == card);
    }

    SECTION("tap on the filename text reaches card_root") {
        lv_point_t p = centre_of(filename);
        lv_obj_t* got = hit(test_screen(), p.x, p.y);
        INFO("hit on filename_label resolved to: " << name_of(got));
        CHECK(got == card);
    }

    SECTION("tap on the metadata row (time/filament) reaches card_root") {
        lv_point_t p = centre_of(row);
        lv_obj_t* got = hit(test_screen(), p.x, p.y);
        INFO("hit on metadata_row resolved to: " << name_of(got));
        CHECK(got == card);
    }

    SECTION("tap anywhere on the overlay reaches card_root") {
        lv_point_t p = centre_of(overlay);
        lv_obj_t* got = hit(test_screen(), p.x, p.y);
        INFO("hit on metadata_overlay resolved to: " << name_of(got));
        CHECK(got == card);
    }
}
