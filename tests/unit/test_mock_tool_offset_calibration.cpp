// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_mock_tool_offset_calibration.cpp
 * @brief The mock's klipper-toolchanger tool-offset calibration.
 *
 * TOOL_CALIBRATE_TOOL_OFFSET takes NO arguments - it measures whatever is on
 * the carriage - and refuses until TOOL_LOCATE_SENSOR has established the probe
 * position. Both of those are load-bearing: a driver that forgets to
 * SELECT_TOOL first writes the measurement onto the wrong tool, and one that
 * skips the locate pass gets a refusal it has to surface rather than silently
 * treat as success.
 *
 * The mock models the refusals so a panel driving this sequence can be tested
 * against them. It also routes the result through the pending-config model
 * (see test_mock_save_config.cpp) - klipper-toolchanger writes the measurement
 * with configfile.set(), so nothing survives a restart until SAVE_CONFIG.
 *
 * These tests drive raw gcode through the mock's synchronous
 * printer.gcode.script handler and assert on the mock's own state, so they
 * describe the SIMULATOR's fidelity to Klipper rather than any panel's use of
 * it.
 */

#include "../helix_test_fixture.h"
#include "moonraker_client_mock.h"
#include "moonraker_error.h"

#include "../catch_amalgamated.hpp"

using nlohmann::json;

namespace {

struct ToolCalibrateFixture : public HelixTestFixture {
    MoonrakerClientMock client{MoonrakerClientMock::PrinterType::MULTI_EXTRUDER, 100.0};

    /// Run one script through the mock's printer.gcode.script handler, which
    /// executes synchronously inside the call. Returns whether the rpc acked -
    /// a refusal comes back as an error, not an ack.
    bool gcode(const std::string& script) {
        bool acked = false;
        client.send_jsonrpc(
            "printer.gcode.script", json{{"script", script}},
            [&acked](const json&) { acked = true; }, [](const MoonrakerError&) {});
        return acked;
    }

    /// Drive the full happy-path sequence for one tool.
    void calibrate(int tool) {
        gcode("TOOL_LOCATE_SENSOR");
        gcode("SELECT_TOOL T=" + std::to_string(tool));
        gcode("TOOL_CALIBRATE_TOOL_OFFSET");
    }
};

} // namespace

// ============================================================================
// Preconditions - the two refusals a driver must handle
// ============================================================================

TEST_CASE_METHOD(ToolCalibrateFixture,
                 "mock: calibrating before locating the sensor is refused",
                 "[mock][toolchanger][tool_offset_calibration]") {
    // The probe position is the zero every tool is measured against. Without
    // it there is nothing to measure, and the real extra errors rather than
    // returning a meaningless number.
    REQUIRE_FALSE(client.tools_calibrate_located());

    gcode("SELECT_TOOL T=1");
    CHECK_FALSE(gcode("TOOL_CALIBRATE_TOOL_OFFSET"));

    // ...and nothing was written.
    CHECK_FALSE(client.save_config_pending());
}

TEST_CASE_METHOD(ToolCalibrateFixture, "mock: calibrating with nothing mounted is refused",
                 "[mock][toolchanger][tool_offset_calibration]") {
    // The mock's own rule, not a transcription of firmware behaviour: the
    // measuring command takes no arguments, so with nothing on the carriage
    // there is no tool the result could belong to. Refusing keeps a caller that
    // skipped SELECT_TOOL visible instead of writing onto whichever tool was
    // mounted last.
    gcode("TOOL_LOCATE_SENSOR");
    gcode("UNSELECT_TOOL");

    CHECK_FALSE(gcode("TOOL_CALIBRATE_TOOL_OFFSET"));
    CHECK_FALSE(client.save_config_pending());
}

TEST_CASE_METHOD(ToolCalibrateFixture, "mock: TOOL_LOCATE_SENSOR sets the located flag",
                 "[mock][toolchanger][tool_offset_calibration]") {
    REQUIRE_FALSE(client.tools_calibrate_located());

    CHECK(gcode("TOOL_LOCATE_SENSOR"));
    CHECK(client.tools_calibrate_located());
}

// ============================================================================
// Selection - the trap that writes onto the wrong tool
// ============================================================================

TEST_CASE_METHOD(ToolCalibrateFixture, "mock: SELECT_TOOL is what the calibration measures",
                 "[mock][toolchanger][tool_offset_calibration]") {
    // TOOL_CALIBRATE_TOOL_OFFSET carries no tool argument, so the selection is
    // the ONLY thing that decides which tool gets written. Modelling that
    // literally is the point: a driver that forgets the SELECT_TOOL must be
    // able to fail here rather than in the field.
    calibrate(2);

    CHECK(client.toolchanger_current_tool() == 2);
    // T2 was measured...
    CHECK(client.tool_offset(2).x == Catch::Approx(0.412 * 2));
    // ...and T1, never selected, was not touched.
    CHECK(client.tool_offset(1).x == Catch::Approx(0.400 * 1));
}

TEST_CASE_METHOD(ToolCalibrateFixture, "mock: UNSELECT_TOOL empties the carriage",
                 "[mock][toolchanger][tool_offset_calibration]") {
    gcode("SELECT_TOOL T=3");
    REQUIRE(client.toolchanger_current_tool() == 3);

    gcode("UNSELECT_TOOL");
    CHECK(client.toolchanger_current_tool() == -1);
}

// ============================================================================
// The measurement itself
// ============================================================================

TEST_CASE_METHOD(ToolCalibrateFixture, "mock: a calibration writes all three axes",
                 "[mock][toolchanger][tool_offset_calibration]") {
    calibrate(1);

    const auto offset = client.tool_offset(1);
    CHECK(offset.x == Catch::Approx(0.412));
    CHECK(offset.y == Catch::Approx(-0.085));
    CHECK(offset.z == Catch::Approx(-0.028));
    // The z-only accessor sees the same record - not a second store.
    CHECK(client.tool_z_offset(1) == Catch::Approx(-0.028));
}

TEST_CASE_METHOD(ToolCalibrateFixture, "mock: the base tool is zero on every axis",
                 "[mock][toolchanger][tool_offset_calibration]") {
    // Every other tool's offset is a delta FROM tool 0, so a non-zero base tool
    // is not a different number - it is a contradiction. True both before and
    // after measuring it.
    const auto seeded = client.tool_offset(0);
    CHECK(seeded.x == Catch::Approx(0.0));
    CHECK(seeded.y == Catch::Approx(0.0));
    CHECK(seeded.z == Catch::Approx(0.0));

    calibrate(0);

    const auto measured = client.tool_offset(0);
    CHECK(measured.x == Catch::Approx(0.0));
    CHECK(measured.y == Catch::Approx(0.0));
    CHECK(measured.z == Catch::Approx(0.0));
}

TEST_CASE_METHOD(ToolCalibrateFixture, "mock: tools seed distinct, not all-zero",
                 "[mock][toolchanger][tool_offset_calibration]") {
    // An all-zero seed makes "every tool shows the same number" - the
    // characteristic per-tool display bug - look correct.
    CHECK(client.tool_offset(1).x != Catch::Approx(client.tool_offset(2).x));
    CHECK(client.tool_offset(1).y != Catch::Approx(client.tool_offset(2).y));
}

// ============================================================================
// Persistence - it rides the pending-config model
// ============================================================================

TEST_CASE_METHOD(ToolCalibrateFixture, "mock: a calibration is staged, not durable",
                 "[mock][toolchanger][tool_offset_calibration]") {
    // klipper-toolchanger writes the measurement through configfile.set(). A
    // mock that persisted it here would hide a panel that never sends the
    // SAVE_CONFIG at all.
    REQUIRE_FALSE(client.save_config_pending());

    calibrate(1);

    CHECK(client.save_config_pending());
    const json pending = client.save_config_pending_items();
    REQUIRE(pending.contains("tool T1"));
    CHECK(pending["tool T1"].contains("gcode_x_offset"));
    CHECK(pending["tool T1"].contains("gcode_y_offset"));
    CHECK(pending["tool T1"].contains("gcode_z_offset"));
}

TEST_CASE_METHOD(ToolCalibrateFixture, "mock: an uncommitted calibration is lost on restart",
                 "[mock][toolchanger][tool_offset_calibration]") {
    calibrate(1);
    REQUIRE(client.tool_offset(1).x == Catch::Approx(0.412));

    client.trigger_restart(/*is_firmware=*/false);

    // Back to the seed - the measurement never reached printer.cfg.
    CHECK(client.tool_offset(1).x == Catch::Approx(0.400));
}

TEST_CASE_METHOD(ToolCalibrateFixture, "mock: SAVE_CONFIG commits all three axes",
                 "[mock][toolchanger][tool_offset_calibration]") {
    calibrate(1);
    REQUIRE(client.save_config_pending());

    gcode("SAVE_CONFIG");

    // SAVE_CONFIG restarts Klipper; the durable copy is what comes back.
    const auto offset = client.tool_offset(1);
    CHECK(offset.x == Catch::Approx(0.412));
    CHECK(offset.y == Catch::Approx(-0.085));
    CHECK(offset.z == Catch::Approx(-0.028));
    CHECK_FALSE(client.save_config_pending());
}

// ============================================================================
// SET_TOOL_PARAMETER now covers all three axes
// ============================================================================

TEST_CASE_METHOD(ToolCalibrateFixture, "mock: SET_TOOL_PARAMETER writes one axis and only that one",
                 "[mock][toolchanger][tool_offset_calibration]") {
    // The tune overlay writes Z alone. Writing it must not disturb X and Y,
    // which a whole-record overwrite would silently zero.
    const auto before = client.tool_offset(1);

    gcode("SET_TOOL_PARAMETER T=1 PARAMETER=gcode_x_offset VALUE=1.5");

    const auto after = client.tool_offset(1);
    CHECK(after.x == Catch::Approx(1.5));
    CHECK(after.y == Catch::Approx(before.y));
    CHECK(after.z == Catch::Approx(before.z));
}

// ============================================================================
// The persist path the panel actually drives
// ============================================================================

TEST_CASE_METHOD(ToolCalibrateFixture,
                 "mock: the explicit save persists a calibration across a restart",
                 "[mock][toolchanger][tool_offset_calibration]") {
    // The whole sequence helix::tool_offset_calibration::save_gcode() emits,
    // end to end. This is the test that would fail if SAVE_TOOL_PARAMETER did
    // not persist what the CALIBRATION wrote - which is the assumption the
    // explicit staging exists to avoid making.
    calibrate(2);
    const auto measured = client.tool_offset(2);
    REQUIRE(measured.x != Catch::Approx(0.0)); // a real measurement landed

    for (const char* axis : {"gcode_x_offset", "gcode_y_offset", "gcode_z_offset"}) {
        gcode(std::string("SAVE_TOOL_PARAMETER T=2 PARAMETER=") + axis);
    }
    REQUIRE(client.save_config_pending());

    gcode("SAVE_CONFIG");
    client.trigger_restart(/*is_firmware=*/false);

    // Survived, on all three axes.
    const auto after = client.tool_offset(2);
    CHECK(after.x == Catch::Approx(measured.x));
    CHECK(after.y == Catch::Approx(measured.y));
    CHECK(after.z == Catch::Approx(measured.z));
}

TEST_CASE_METHOD(ToolCalibrateFixture,
                 "mock: a calibration nobody saved does not survive a restart",
                 "[mock][toolchanger][tool_offset_calibration]") {
    // The other half, and the reason the mock keeps a separate durable map: if
    // an unsaved calibration survived, no test could tell a working save from a
    // forgotten one.
    calibrate(2);
    const auto measured = client.tool_offset(2);
    REQUIRE(measured.z != Catch::Approx(0.0));

    client.trigger_restart(/*is_firmware=*/false);

    CHECK(client.tool_offset(2).z != Catch::Approx(measured.z));
}
