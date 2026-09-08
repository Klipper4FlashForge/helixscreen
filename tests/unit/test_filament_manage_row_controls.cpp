// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_filament_manage_row_controls.cpp
 * @brief What the tool row offers, and when it appears at all.
 *
 * This is prestonbrown/helixscreen#1350's question - which control the row
 * carries, decided by tool count - asked of the row that replaced it. The
 * panel rebuild dropped the two mutually exclusive controls (a dropdown for
 * multi-tool, a Manage button for single-tool) for one chip per tool plus a
 * Manage button that is always there. So the question is no longer WHICH
 * control shows but WHETHER the row does: filament_panel.xml hides it only
 * when there is nothing to choose between, `tool_count le 1 and ams_type eq
 * 0`, because that printer's spool is shown in the external-spool card
 * instead.
 */

#include "ui_panel_filament.h"

#include "../lvgl_test_fixture.h"
#include "../lvgl_ui_test_fixture.h"
#include "../test_helpers/filament_panel_test_access.h"
#include "ams_state.h"
#include "ams_types.h"
#include "printer_discovery.h"
#include "tool_state.h"

#include <lvgl.h>
#include <memory>
#include <string>

#include "../catch_amalgamated.hpp"

using namespace helix;
using TA = helix::ui::FilamentPanelTestAccess;

namespace {

/// Builds the real FilamentPanel over the real filament_panel.xml so the
/// visibility assertions read the same widgets production shows.
struct ManageRowHarness {
    LVGLUITestFixture& fx;
    std::unique_ptr<FilamentPanel> panel;
    lv_obj_t* root = nullptr;

    /// @param extruder_heaters Klipper extruder objects the printer reports.
    ///        Two or more with no [tool N] object is a plain multi-extruder
    ///        machine, which ToolState turns into one tool per heater.
    /// @param ams_type Value of the ams_type subject; 0 means no backend.
    ManageRowHarness(LVGLUITestFixture& f, const std::vector<std::string>& extruder_heaters,
                     AmsType ams_type)
        : fx(f) {
        ToolState::instance().init_subjects(true);
        AmsState::instance().init_subjects(true);
        AmsState::instance().clear_backends();

        nlohmann::json objects = nlohmann::json::array();
        for (const auto& h : extruder_heaters) {
            objects.push_back(h);
        }
        objects.push_back("heater_bed");
        objects.push_back("fan");
        objects.push_back("gcode_move");

        helix::PrinterDiscovery hw;
        hw.parse_objects(objects);
        ToolState::instance().init_tools(hw);

        lv_subject_set_int(AmsState::instance().get_ams_type_subject(),
                           static_cast<int>(ams_type));

        panel = std::make_unique<FilamentPanel>(fx.state(), fx.api());
        panel->init_subjects();

        root = static_cast<lv_obj_t*>(lv_xml_create(fx.test_screen(), "filament_panel", nullptr));
        REQUIRE(root != nullptr);
        panel->setup(root, fx.test_screen());
        fx.process_lvgl(30);

        TA::seed_selected_tool(*panel);
    }

    ~ManageRowHarness() {
        panel.reset();
        AmsState::instance().clear_backends();
    }

    [[nodiscard]] bool hidden(const char* name) const {
        lv_obj_t* obj = lv_obj_find_by_name(root, name);
        REQUIRE(obj != nullptr);
        return lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }

    /// The chips are built by a subject-bound <repeat> over tool_count, so
    /// their presence - not a hidden flag - is what says how many tools the
    /// row is offering.
    [[nodiscard]] bool has_chip(int index) const {
        return lv_obj_find_by_name(root, ("tool_chip_" + std::to_string(index)).c_str()) != nullptr;
    }
};

} // namespace

TEST_CASE_METHOD(LVGLUITestFixture, "Tool row: multi-tool without AMS still gets a chip per tool",
                 "[filament][ui][tool]") {
    ManageRowHarness h(*this, {"extruder", "extruder1"}, AmsType::NONE);

    REQUIRE(ToolState::instance().is_multi_tool());

    // Two tools is something to choose between, so the row shows even with no
    // backend - this is the half of #1350 that said the selector belongs to
    // every multi-tool printer, not only to one with an AMS.
    CHECK_FALSE(h.hidden("tool_row"));
    CHECK(h.has_chip(0));
    CHECK(h.has_chip(1));
    CHECK_FALSE(h.has_chip(2));

    // Manage is no longer the single-tool alternative to a selector; it rides
    // the row for every printer that has one.
    CHECK_FALSE(h.hidden("btn_manage_slots"));
}

TEST_CASE_METHOD(LVGLUITestFixture, "Tool row: single-tool with AMS keeps the row for Manage",
                 "[filament][ui][tool]") {
    ManageRowHarness h(*this, {"extruder"}, AmsType::AFC);

    REQUIRE_FALSE(ToolState::instance().is_multi_tool());

    // One tool, but a backend whose slots the user needs a route to.
    CHECK_FALSE(h.hidden("tool_row"));
    CHECK_FALSE(h.hidden("btn_manage_slots"));
}

TEST_CASE_METHOD(LVGLUITestFixture, "Tool row: single-tool without AMS hides the whole row",
                 "[filament][ui][tool]") {
    ManageRowHarness h(*this, {"extruder"}, AmsType::NONE);

    REQUIRE_FALSE(ToolState::instance().is_multi_tool());

    // Nothing to choose between and no slots to manage. The spool for this
    // printer is on the external-spool card, so a row here would be a chip
    // that selects the only tool there is.
    CHECK(h.hidden("tool_row"));
}
