// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui_material_picker_menu.h"
#include "ui_panel_filament.h"
#include "ui_update_queue.h"

#include "../lvgl_test_fixture.h"
#include "../test_helpers/material_picker_test_access.h"

#include "../catch_amalgamated.hpp"

using namespace helix::ui;

TEST_CASE_METHOD(LVGLTestFixture, "MaterialPickerMenu dispatch_select fires callback with material",
                 "[material_picker]") {
    MaterialPickerMenu menu;
    std::string got;
    menu.set_select_callback([&](const std::string& m) { got = m; });

    MaterialPickerTestAccess::dispatch_select(menu, "PC");
    UpdateQueue::instance().drain();

    CHECK(got == "PC");
}

TEST_CASE_METHOD(LVGLTestFixture, "MaterialPickerMenu dispatch_reset fires reset callback",
                 "[material_picker]") {
    MaterialPickerMenu menu;
    bool did_reset = false;
    menu.set_reset_callback([&]() { did_reset = true; });

    MaterialPickerTestAccess::dispatch_reset(menu);
    UpdateQueue::instance().drain();

    CHECK(did_reset);
}

TEST_CASE("validate_reassignment accepts valid slot+material", "[material_picker][validate]") {
    using helix::filament_presets::validate_reassignment;
    CHECK(validate_reassignment(0, "PLA"));
    CHECK(validate_reassignment(3, "PC"));
}

TEST_CASE("validate_reassignment rejects bad slot, empty, or unknown material",
          "[material_picker][validate]") {
    using helix::filament_presets::validate_reassignment;
    CHECK_FALSE(validate_reassignment(-1, "PLA"));
    CHECK_FALSE(validate_reassignment(4, "PLA"));
    CHECK_FALSE(validate_reassignment(0, ""));
    CHECK_FALSE(validate_reassignment(0, "DEFINITELY_NOT_A_MATERIAL"));
}
