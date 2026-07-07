// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui_filament_catalog_picker.h"
#include "ui_update_queue.h"

#include "../test_fixtures.h"
#include "../test_helpers/filament_picker_test_access.h"

#include "../catch_amalgamated.hpp"

using helix::printer::EffectiveFilament;
using helix::ui::FilamentCatalogPickerModal;
using helix::ui::FilamentPickerTestAccess;

TEST_CASE_METHOD(XMLTestFixture, "picker emits selected EffectiveFilament", "[filament_picker]") {
    FilamentCatalogPickerModal modal;
    std::optional<EffectiveFilament> got;
    modal.show(lv_screen_active(), std::string("PLA"),
               [&](const EffectiveFilament& ef) { got = ef; });

    // Drive: select the first Generic PLA product via the test accessor, then confirm.
    FilamentPickerTestAccess::select_first_product(modal);
    FilamentPickerTestAccess::press_select(modal);
    helix::ui::UpdateQueue::instance().drain();

    REQUIRE(got.has_value());
    REQUIRE(got->type == "PLA");
    REQUIRE(!got->brand.empty());
}

TEST_CASE_METHOD(XMLTestFixture, "picker clears highlight on vendor change", "[filament_picker]") {
    FilamentCatalogPickerModal modal;
    std::optional<EffectiveFilament> got;
    modal.show(lv_screen_active(), std::string("PLA"),
               [&](const EffectiveFilament& ef) { got = ef; });

    // Highlight a product under the initial (Generic) vendor.
    FilamentPickerTestAccess::select_first_product(modal);
    REQUIRE(!FilamentPickerTestAccess::highlighted_id(modal).empty());

    // Switch the vendor dropdown selection — this rebuilds the list and must drop the
    // stale highlight, since that product row is no longer visible.
    FilamentPickerTestAccess::change_vendor(modal, 1);
    REQUIRE(FilamentPickerTestAccess::highlighted_id(modal).empty());

    // Pressing Select now must be a no-op: no callback fire for a product the user can
    // no longer see.
    FilamentPickerTestAccess::press_select(modal);
    helix::ui::UpdateQueue::instance().drain();

    REQUIRE_FALSE(got.has_value());
}

TEST_CASE_METHOD(XMLTestFixture, "picker Select with no highlight is a graceful no-op",
                 "[filament_picker]") {
    FilamentCatalogPickerModal modal;
    std::optional<EffectiveFilament> got;
    modal.show(lv_screen_active(), std::string("PLA"),
               [&](const EffectiveFilament& ef) { got = ef; });

    REQUIRE(FilamentPickerTestAccess::highlighted_id(modal).empty());

    // No row ever selected — Select must not crash and must not invoke the callback.
    FilamentPickerTestAccess::press_select(modal);
    helix::ui::UpdateQueue::instance().drain();

    REQUIRE_FALSE(got.has_value());
}

TEST_CASE_METHOD(XMLTestFixture, "picker reset row hidden when no reset callback set",
                 "[filament_picker]") {
    FilamentCatalogPickerModal modal;
    modal.show(lv_screen_active(), std::string("PLA"), [](const EffectiveFilament&) {});
    helix::ui::UpdateQueue::instance().drain();

    REQUIRE(FilamentPickerTestAccess::reset_button_hidden(modal));
}

TEST_CASE_METHOD(XMLTestFixture, "picker reset row shown when reset callback set before show",
                 "[filament_picker]") {
    FilamentCatalogPickerModal modal;
    modal.set_reset_callback([]() {});
    modal.show(lv_screen_active(), std::string("PLA"), [](const EffectiveFilament&) {});
    helix::ui::UpdateQueue::instance().drain();

    REQUIRE_FALSE(FilamentPickerTestAccess::reset_button_hidden(modal));
}

namespace {
// Split a newline-separated dropdown options string into individual entries.
std::vector<std::string> split_lines(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == '\n') {
            out.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    if (!cur.empty())
        out.push_back(cur);
    return out;
}
} // namespace

TEST_CASE_METHOD(XMLTestFixture, "picker allowed_types full match keeps every vendor type",
                 "[filament_picker]") {
    // Capture the UNFILTERED Type dropdown for the default (Generic) vendor.
    FilamentCatalogPickerModal ref;
    ref.show(lv_screen_active(), std::nullopt, [](const EffectiveFilament&) {});
    std::string unfiltered = FilamentPickerTestAccess::type_options(ref);
    ref.hide();
    helix::ui::UpdateQueue::instance().drain();

    REQUIRE_FALSE(unfiltered.empty());

    // allowed_types = the exact full type set -> a superset that must not drop anything.
    std::vector<std::string> allowed = split_lines(unfiltered);
    FilamentCatalogPickerModal modal;
    modal.show(lv_screen_active(), std::nullopt, allowed, [](const EffectiveFilament&) {});
    helix::ui::UpdateQueue::instance().drain();

    REQUIRE(FilamentPickerTestAccess::type_options(modal) == unfiltered);
}

TEST_CASE_METHOD(XMLTestFixture, "picker allowed_types partial match keeps only the subset",
                 "[filament_picker]") {
    // PLA and PETG both exist for Generic; the dropdown is alphabetically sorted, so the
    // filtered order is PETG then PLA.
    std::vector<std::string> allowed = {"PLA", "PETG"};
    FilamentCatalogPickerModal modal;
    modal.show(lv_screen_active(), std::nullopt, allowed, [](const EffectiveFilament&) {});
    helix::ui::UpdateQueue::instance().drain();

    REQUIRE(FilamentPickerTestAccess::type_options(modal) == "PETG\nPLA");
}

TEST_CASE_METHOD(XMLTestFixture,
                 "picker allowed_types with no matching product yields empty type list",
                 "[filament_picker]") {
    // A type Generic has no product for -> filter drops everything, dropdown empties.
    std::vector<std::string> allowed = {"NONEXISTENT_TYPE"};
    FilamentCatalogPickerModal modal;
    modal.show(lv_screen_active(), std::nullopt, allowed, [](const EffectiveFilament&) {});
    helix::ui::UpdateQueue::instance().drain();

    // Must not crash; the Type dropdown is empty.
    REQUIRE(FilamentPickerTestAccess::type_options(modal).empty());
}

TEST_CASE_METHOD(XMLTestFixture, "picker allowed_types filter is case-insensitive",
                 "[filament_picker]") {
    // The AMS material dropdown is case-insensitive; a backend whitelist entry spelled
    // "pla" must still surface the catalog's "PLA" type. A case-sensitive filter would
    // wrongly hide it (a real UX gap), so populate_type_dropdown() lowercases both sides.
    std::vector<std::string> allowed = {"pla"};
    FilamentCatalogPickerModal modal;
    modal.show(lv_screen_active(), std::nullopt, allowed, [](const EffectiveFilament&) {});
    helix::ui::UpdateQueue::instance().drain();

    REQUIRE(FilamentPickerTestAccess::type_options(modal) == "PLA");
}

TEST_CASE_METHOD(XMLTestFixture, "picker reset invokes stored reset callback",
                 "[filament_picker]") {
    FilamentCatalogPickerModal modal;
    bool reset_invoked = false;
    modal.set_reset_callback([&]() { reset_invoked = true; });
    modal.show(lv_screen_active(), std::string("PLA"), [](const EffectiveFilament&) {});
    helix::ui::UpdateQueue::instance().drain();

    REQUIRE_FALSE(FilamentPickerTestAccess::reset_button_hidden(modal));

    FilamentPickerTestAccess::press_reset(modal);
    helix::ui::UpdateQueue::instance().drain();

    REQUIRE(reset_invoked);
}
