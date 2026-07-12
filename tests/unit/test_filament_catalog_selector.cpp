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

TEST_CASE_METHOD(XMLTestFixture,
                 "selector allowed_types appends whitelist entries missing from catalog",
                 "[filament_picker][catalog_selector][whitelist]") {
    lv_obj_t* root = make_fragment();
    REQUIRE(root != nullptr);

    FilamentCatalogSelector sel;
    sel.attach(root);
    // AD5X-shaped whitelist: SILK has no Generic-vendor catalog product at all, so the old
    // subtract-only filter silently dropped it, locking users out of a firmware-supported
    // material. PLA and PETG both exist for Generic and are intersected as before.
    sel.configure(std::nullopt, std::vector<std::string>{"PLA", "SILK", "PETG"});
    sel.populate();

    // Sorted catalog intersection (PETG, PLA) first, then whitelist-only entries appended
    // in whitelist order, preserving whitelist spelling.
    CHECK(sel.type_options() == "PETG\nPLA\nSILK");

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

TEST_CASE_METHOD(XMLTestFixture,
                 "preselect_on_change keeps a checked product across a type change",
                 "[filament_picker][catalog_selector][preselect]") {
    lv_obj_t* root = make_fragment();
    REQUIRE(root != nullptr);

    FilamentCatalogSelector sel;
    sel.attach(root);
    sel.set_preselect_on_change(true);
    // Constrain the type dropdown to a known, sorted set so indices are stable:
    // catalog intersection is alphabetical -> "PETG\nPLA" (index 0 = PETG, 1 = PLA).
    sel.configure(std::string("PLA"), std::vector<std::string>{"PLA", "PETG"});
    sel.populate();
    sel.preselect_first();
    REQUIRE(sel.type_options() == "PETG\nPLA");
    REQUIRE(sel.current_type() == "PLA");
    const EffectiveFilament* pla = sel.highlighted();
    REQUIRE(pla != nullptr);
    CHECK(pla->type == "PLA");
    std::string pla_id = pla->id;

    // Change type to PETG: the rebuilt list must auto-highlight a PETG product
    // (invariant: never an all-unchecked list under preselect_on_change).
    sel.change_type_for_test(0);
    CHECK(sel.current_type() == "PETG");
    const EffectiveFilament* petg = sel.highlighted();
    REQUIRE(petg != nullptr);
    CHECK(petg->type == "PETG");

    // Navigate back to PLA: the original (anchor) product is restored, not just
    // whatever happens to be first.
    sel.change_type_for_test(1);
    CHECK(sel.current_type() == "PLA");
    REQUIRE(sel.highlighted() != nullptr);
    CHECK(sel.highlighted()->id == pla_id);

    sel.detach();
    helix::ui::UpdateQueue::instance().drain();
}

TEST_CASE_METHOD(XMLTestFixture,
                 "preselect_on_change leaves an empty product list unchecked",
                 "[filament_picker][catalog_selector][preselect]") {
    lv_obj_t* root = make_fragment();
    REQUIRE(root != nullptr);

    FilamentCatalogSelector sel;
    sel.attach(root);
    sel.set_preselect_on_change(true);
    // SILK is whitelisted but has no Generic catalog product -> appended type,
    // empty product list. Nothing to check; the host decides Save semantics.
    sel.configure(std::nullopt, std::vector<std::string>{"SILK"});
    sel.populate();
    CHECK(sel.current_type() == "SILK");
    CHECK(sel.highlighted() == nullptr);

    sel.detach();
}

TEST_CASE_METHOD(XMLTestFixture,
                 "product list ranks the plain material first and Support materials last",
                 "[filament_picker][catalog_selector][ordering]") {
    lv_obj_t* root = make_fragment();
    REQUIRE(root != nullptr);

    FilamentCatalogSelector sel;
    sel.attach(root);
    sel.configure(std::string("PLA"), std::nullopt);
    sel.populate();
    REQUIRE(sel.current_vendor() == "Generic");
    REQUIRE(sel.current_type() == "PLA");

    // assets/filaments.json lists Generic/PLA in file order as: Support for
    // PLA, PLA, PLA High Speed, PLA Matte, PLA Silk. Display order must sink
    // "Support for PLA" to the end and put the plain "PLA" first, with the
    // remaining variants alphabetical in between.
    auto names = sel.product_names_for_test();
    REQUIRE(names == std::vector<std::string>{"PLA", "PLA High Speed", "PLA Matte", "PLA Silk",
                                              "Support for PLA"});

    sel.detach();
}

TEST_CASE_METHOD(XMLTestFixture,
                 "type-change auto-preselect lands on the plain material, not Support",
                 "[filament_picker][catalog_selector][preselect][ordering]") {
    lv_obj_t* root = make_fragment();
    REQUIRE(root != nullptr);

    FilamentCatalogSelector sel;
    sel.attach(root);
    sel.set_preselect_on_change(true);
    // Constrain to a known, sorted type set so indices are stable:
    // catalog intersection is alphabetical -> "ABS\nPLA" (index 0 = ABS, 1 = PLA).
    sel.configure(std::string("ABS"), std::vector<std::string>{"PLA", "ABS"});
    sel.populate();
    REQUIRE(sel.type_options() == "ABS\nPLA");
    REQUIRE(sel.current_type() == "ABS");

    // Switching to PLA (no anchor carried over from ABS) must auto-highlight
    // the plain "PLA" product per preselect_after_change()'s front() fallback
    // now walking the ranked list, not raw catalog/file order.
    sel.change_type_for_test(1);
    CHECK(sel.current_type() == "PLA");
    const EffectiveFilament* picked = sel.highlighted();
    REQUIRE(picked != nullptr);
    CHECK(picked->name == "PLA");

    sel.detach();
    helix::ui::UpdateQueue::instance().drain();
}

TEST_CASE_METHOD(XMLTestFixture, "preselect_first checks the first product but keeps a prior pick",
                 "[filament_picker][catalog_selector]") {
    lv_obj_t* root = make_fragment();
    REQUIRE(root != nullptr);

    FilamentCatalogSelector sel;
    sel.attach(root);
    sel.configure(std::string("ABS"), std::nullopt);
    sel.populate();
    REQUIRE(sel.highlighted() == nullptr);

    sel.preselect_first();
    const EffectiveFilament* first = sel.highlighted();
    REQUIRE(first != nullptr);
    CHECK(first->type == "ABS");

    // Idempotent: a second call must not clobber the existing selection.
    std::string kept_id = first->id;
    sel.preselect_first();
    REQUIRE(sel.highlighted() != nullptr);
    CHECK(sel.highlighted()->id == kept_id);

    sel.detach();
}
