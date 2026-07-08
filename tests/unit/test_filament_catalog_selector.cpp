// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui_filament_catalog_selector.h"
#include "ui_update_queue.h"

#include "../test_fixtures.h"

#include "../catch_amalgamated.hpp"

using helix::printer::EffectiveFilament;
using helix::ui::FilamentCatalogSelector;

namespace {
lv_obj_t* make_fragment() {
    FilamentCatalogSelector::register_callbacks();
    lv_xml_register_component_from_file("A:ui_xml/components/filament_catalog_selector.xml");
    return static_cast<lv_obj_t*>(
        lv_xml_create(lv_screen_active(), "filament_catalog_selector", nullptr));
}
} // namespace

TEST_CASE_METHOD(XMLTestFixture, "selector populates and reports a highlighted product",
                 "[filament_picker][catalog_selector]") {
    lv_obj_t* root = make_fragment();
    REQUIRE(root != nullptr);

    FilamentCatalogSelector sel;
    sel.attach(root);
    sel.configure(std::string("PLA"), std::nullopt);
    sel.populate();

    REQUIRE(sel.current_vendor() == "Generic");
    REQUIRE(sel.current_type() == "PLA");
    REQUIRE(sel.highlighted() == nullptr);

    const EffectiveFilament* got = nullptr;
    sel.set_selection_changed([&](const EffectiveFilament* ef) { got = ef; });
    sel.select_first_product_for_test();
    REQUIRE(sel.highlighted() != nullptr);
    REQUIRE(got != nullptr);
    CHECK(got->type == "PLA");

    sel.detach();
    helix::ui::UpdateQueue::instance().drain();
}

TEST_CASE_METHOD(XMLTestFixture, "selector allowed_types filter is case-insensitive",
                 "[filament_picker][catalog_selector]") {
    lv_obj_t* root = make_fragment();
    REQUIRE(root != nullptr);

    FilamentCatalogSelector sel;
    sel.attach(root);
    sel.configure(std::nullopt, std::vector<std::string>{"pla"});
    sel.populate();

    CHECK(sel.type_options() == "PLA");

    sel.detach();
}

TEST_CASE_METHOD(XMLTestFixture, "selector clears highlight when vendor changes",
                 "[filament_picker][catalog_selector]") {
    lv_obj_t* root = make_fragment();
    REQUIRE(root != nullptr);

    FilamentCatalogSelector sel;
    sel.attach(root);
    sel.configure(std::string("PLA"), std::nullopt);
    sel.populate();
    sel.select_first_product_for_test();
    REQUIRE(sel.highlighted() != nullptr);

    sel.change_vendor_for_test(1);
    CHECK(sel.highlighted() == nullptr);

    sel.detach();
}
