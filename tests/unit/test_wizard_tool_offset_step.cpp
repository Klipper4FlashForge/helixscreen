// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_wizard_tool_offset_step.cpp
 * @brief Unit tests for WizardToolOffsetStep skip logic
 *
 * The step is capability-gated: it appears only when the printer exposes a
 * klipper-toolchanger `toolchanger` object AND the CALIBRATE_TOOL_OFFSETS
 * macro. Neither a preset nor the first-run flags influence it.
 */

#include "ui_wizard_tool_offset.h"

#include "../../src/ui/wizard_step_registry.h"
#include "../lvgl_test_fixture.h"
#include "../test_helpers/printer_state_test_access.h"
#include "../test_helpers/update_queue_test_access.h"
#include "app_globals.h"
#include "printer_discovery.h"
#include "printer_state.h"
#include "runtime_config.h"
#include "tool_state.h"

#include "../catch_amalgamated.hpp"

using namespace helix;
using namespace helix::ui;
using helix::wizard::StepId;

class WizardToolOffsetStepTestFixture : public LVGLTestFixture {
  public:
    WizardToolOffsetStepTestFixture() {
        get_runtime_config()->test_mode = true;
        PrinterStateTestAccess::reset(state());
        state().init_subjects(true);
    }

    ~WizardToolOffsetStepTestFixture() override {
        PrinterStateTestAccess::reset(state());
    }

  protected:
    PrinterState& state() {
        return get_printer_state();
    }

    void set_printer_objects(const nlohmann::json& objects) {
        PrinterDiscovery hardware;
        hardware.parse_objects(objects);
        state().set_hardware(hardware);
        UpdateQueueTestAccess::drain(helix::ui::UpdateQueue::instance());
    }
};

TEST_CASE_METHOD(WizardToolOffsetStepTestFixture, "WizardToolOffsetStep - should_skip",
                 "[wizard][tool-offset][skip]") {
    WizardToolOffsetStep step;
    helix::wizard::StepContext ctx;

    SECTION("Skipped on a single-tool printer") {
        set_printer_objects({"heater_bed", "extruder", "fan", "gcode_macro CALIBRATE_TOOL_OFFSETS"});
        REQUIRE(step.should_skip(ctx) == true);
    }

    SECTION("Skipped on a tool changer without the calibration macro") {
        set_printer_objects({"heater_bed", "extruder", "toolchanger", "tool T0", "tool T1"});
        REQUIRE(step.should_skip(ctx) == true);
    }

    SECTION("Shown when toolchanger + CALIBRATE_TOOL_OFFSETS exist") {
        set_printer_objects({"heater_bed", "extruder", "toolchanger", "tool T0", "tool T1",
                             "gcode_macro CALIBRATE_TOOL_OFFSETS"});
        REQUIRE(step.should_skip(ctx) == false);
        REQUIRE(WizardToolOffsetStep::printer_supports_calibration());
    }

    SECTION("A preset does not hide it") {
        set_printer_objects({"toolchanger", "tool T0", "gcode_macro CALIBRATE_TOOL_OFFSETS"});
        ctx.preset.skip_hardware = true;
        ctx.preset.first_run = true;
        REQUIRE(step.should_skip(ctx) == false);
    }

    SECTION("Skipped only once EVERY tool carries an offset") {
        PrinterDiscovery hardware;
        hardware.parse_objects({"extruder", "extruder1", "toolchanger", "tool T0", "tool T1",
                                "gcode_macro CALIBRATE_TOOL_OFFSETS"});
        state().set_hardware(hardware);
        helix::ToolState::instance().init_tools(hardware);
        UpdateQueueTestAccess::drain(helix::ui::UpdateQueue::instance());
        REQUIRE(WizardToolOffsetStep::tools_already_calibrated() == false);

        // One tool measured, the other still zero: not calibrated
        helix::ToolState::instance().update_from_status({{"tool T1", {{"gcode_y_offset", -0.12}}}});
        REQUIRE(WizardToolOffsetStep::tools_already_calibrated() == false);
        REQUIRE(step.should_skip(ctx) == false);

        // Absolute positions: T0 gets a value of its own too
        helix::ToolState::instance().update_from_status({{"tool T0", {{"gcode_x_offset", 0.25}}}});
        REQUIRE(WizardToolOffsetStep::tools_already_calibrated() == true);
        REQUIRE(step.should_skip(ctx) == true);
    }
}

TEST_CASE_METHOD(WizardToolOffsetStepTestFixture, "WizardToolOffsetStep - registry placement",
                 "[wizard][tool-offset]") {
    auto steps = helix::wizard::steps();
    REQUIRE(steps.size() == static_cast<size_t>(helix::wizard::STEP_COUNT));
    auto* s = helix::wizard::step_by_id(StepId::ToolOffset);
    REQUIRE(s != nullptr);
    REQUIRE(std::string(s->component_name()) == "wizard_tool_offset");
    // Offsets come before resonance: the shaper run needs a mounted tool and
    // the macro leaves one on the carriage.
    REQUIRE(static_cast<int>(StepId::ToolOffset) + 1 == static_cast<int>(StepId::InputShaper));
    REQUIRE(std::string(helix::wizard::to_string(StepId::ToolOffset)) == "ToolOffset");
}

TEST_CASE_METHOD(WizardToolOffsetStepTestFixture, "WizardToolOffsetStep - not validated until run",
                 "[wizard][tool-offset]") {
    WizardToolOffsetStep step;
    step.init_subjects();
    REQUIRE(step.is_validated() == false);
    REQUIRE(step.is_calibration_active() == false);
    REQUIRE(lv_subject_get_int(step.get_started_subject()) == 0);
    REQUIRE(lv_xml_get_subject(nullptr, "wizard_tool_offset_status") != nullptr);
}
