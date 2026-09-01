// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_tool_z_offset.cpp
 * @brief Tests for the per-tool Z offset abstraction.
 *
 * A tool changer has two independent Z corrections: Klipper's global baby step
 * and, on some firmwares, a per-tool one. helix::zoffset owns which firmwares
 * have the second, what object publishes it, and what commands move and persist
 * it. Generic code asks the capability questions and never names a firmware, so
 * these tests are also the guard on that boundary: they reach a vendor only
 * through a printer's object list.
 */

#include "printer_discovery.h"
#include "tool_z_offset.h"

#include <algorithm>

#include "../catch_amalgamated.hpp"
#include "hv/json.hpp"

using helix::PrinterDiscovery;
using nlohmann::json;

namespace {

/// A printer whose objects list carries the given extra Klipper objects.
PrinterDiscovery printer_with_objects(std::initializer_list<const char*> extra) {
    json objects = json::array();
    objects.push_back("gcode_move");
    objects.push_back("toolhead");
    for (const char* o : extra) {
        objects.push_back(o);
    }
    PrinterDiscovery hw;
    hw.parse_objects(objects);
    return hw;
}

/// A tool changer running the Reforge firmware: toolchanger + per-tool objects.
PrinterDiscovery reforge_printer() {
    return printer_with_objects({"toolchanger", "tool T0", "tool T1", "tool T2", "tool T3",
                                 "ff_toolchange", "ff_tool_offset", "ff_tool 0", "ff_tool 1",
                                 "ff_tool 2", "ff_tool 3"});
}

bool contains(const std::vector<std::string>& v, const std::string& s) {
    return std::find(v.begin(), v.end(), s) != v.end();
}

} // namespace

// ============================================================================
// Detection
// ============================================================================

TEST_CASE("per-tool z-offset: a single-nozzle printer has none", "[zoffset][pertool]") {
    // The common case, and the one that must keep behaving exactly as before:
    // no second axis, so no selector and nothing extra to subscribe.
    PrinterDiscovery hw = printer_with_objects({"extruder", "heater_bed"});

    CHECK_FALSE(helix::zoffset::supports_per_tool_offset(hw));
    CHECK(helix::zoffset::per_tool_provider_name(hw).empty());
    CHECK(helix::zoffset::per_tool_status_objects(hw, 4).empty());
    CHECK(helix::zoffset::build_tool_adjust_gcode(hw, 0, -50).empty());
    CHECK(helix::zoffset::build_tool_save_gcode(hw, 0, -50).empty());
    CHECK_FALSE(helix::zoffset::per_tool_save_restarts_klipper(hw));
}

TEST_CASE("per-tool z-offset: a tool changer without the firmware has none", "[zoffset][pertool]") {
    // Being a tool changer is NOT the capability. klipper-toolchanger publishes
    // per-tool gcode offsets, but those are the calibrated tool geometry, not an
    // operator-editable correction — offering the selector there would promise
    // something no command backs.
    PrinterDiscovery hw =
        printer_with_objects({"toolchanger", "tool T0", "tool T1", "gcode_macro T0"});

    CHECK_FALSE(helix::zoffset::supports_per_tool_offset(hw));
    CHECK(helix::zoffset::per_tool_status_objects(hw, 2).empty());
}

TEST_CASE("per-tool z-offset: Reforge is detected by its per-tool objects", "[zoffset][pertool]") {
    PrinterDiscovery hw = reforge_printer();

    CHECK(helix::zoffset::supports_per_tool_offset(hw));
    CHECK(helix::zoffset::per_tool_provider_name(hw) == "Reforge");
}

// ============================================================================
// Subscription
// ============================================================================

TEST_CASE("per-tool z-offset: one status object per tool", "[zoffset][pertool]") {
    PrinterDiscovery hw = reforge_printer();

    auto objects = helix::zoffset::per_tool_status_objects(hw, 4);
    REQUIRE(objects.size() == 4);
    CHECK(contains(objects, "ff_tool 0"));
    CHECK(contains(objects, "ff_tool 3"));
}

TEST_CASE("per-tool z-offset: a zero or negative tool count asks for nothing",
          "[zoffset][pertool]") {
    // Discovery can run before the tool list is known; asking for -1 objects
    // must be an empty subscription, not a crash or a bogus "ff_tool -1".
    PrinterDiscovery hw = reforge_printer();

    CHECK(helix::zoffset::per_tool_status_objects(hw, 0).empty());
    CHECK(helix::zoffset::per_tool_status_objects(hw, -1).empty());
}

// ============================================================================
// Reading a status frame
// ============================================================================

TEST_CASE("per-tool z-offset: the value is read per tool", "[zoffset][pertool]") {
    json status = {{"ff_tool 0", {{"z_adjust", -0.075}}}, {"ff_tool 1", {{"z_adjust", 0.120}}}};

    CHECK(helix::zoffset::read_tool_offset_microns(status, 0) == -75);
    CHECK(helix::zoffset::read_tool_offset_microns(status, 1) == 120);
}

TEST_CASE("per-tool z-offset: a frame without the tool is no news", "[zoffset][pertool]") {
    // Moonraker sends deltas: a frame that carries T0 says nothing about T1,
    // and reading that silence as 0 would yank the display to zero mid-tune.
    json status = {{"ff_tool 0", {{"z_adjust", -0.075}}}};

    CHECK(helix::zoffset::read_tool_offset_microns(status, 1) == std::nullopt);
    CHECK(helix::zoffset::read_tool_offset_microns(json::object(), 0) == std::nullopt);
    CHECK(helix::zoffset::read_tool_offset_microns(status, -1) == std::nullopt);
}

TEST_CASE("per-tool z-offset: accumulated floats round to the nearest micron",
          "[zoffset][pertool]") {
    // The stored value is a sum of relative adjustments, so a nominal -0.150
    // arrives as -0.1499999; truncating would show -0.149 and drift further on
    // every step.
    json status = {{"ff_tool 2", {{"z_adjust", -0.14999999999}}}};

    CHECK(helix::zoffset::read_tool_offset_microns(status, 2) == -150);
}

TEST_CASE("per-tool z-offset: a non-numeric value is not a reading", "[zoffset][pertool]") {
    json status = {{"ff_tool 0", {{"z_adjust", nullptr}}}};

    CHECK(helix::zoffset::read_tool_offset_microns(status, 0) == std::nullopt);
}

// ============================================================================
// Commands
// ============================================================================

TEST_CASE("per-tool z-offset: a baby step is relative and names its tool", "[zoffset][pertool]") {
    // Relative, so a display that is one status frame stale still lands on
    // "what the printer had, plus what the user asked for".
    PrinterDiscovery hw = reforge_printer();

    CHECK(helix::zoffset::build_tool_adjust_gcode(hw, 1, -25) ==
          "TOOL_Z_ADJUST TOOL=1 ADJUST=-0.025");
    CHECK(helix::zoffset::build_tool_adjust_gcode(hw, 0, 50) == "TOOL_Z_ADJUST TOOL=0 ADJUST=0.050");
}

TEST_CASE("per-tool z-offset: saving is absolute, then SAVE_CONFIG", "[zoffset][pertool]") {
    // Absolute on the save so a persisted value cannot inherit the accumulated
    // rounding of the steps that produced it, and SAVE_CONFIG last because the
    // firmware only stages the value until then.
    PrinterDiscovery hw = reforge_printer();

    auto commands = helix::zoffset::build_tool_save_gcode(hw, 2, -125);
    REQUIRE(commands.size() == 2);
    CHECK(commands[0] == "TOOL_Z_ADJUST TOOL=2 VALUE=-0.125 SAVE=1");
    CHECK(commands[1] == "SAVE_CONFIG");
    CHECK(helix::zoffset::per_tool_save_restarts_klipper(hw));
}

TEST_CASE("per-tool z-offset: a negative tool index yields no command", "[zoffset][pertool]") {
    // active_tool_index() is -1 before any tool is known; that must not send
    // TOOL=-1 at a printer that would happily refuse it mid-print.
    PrinterDiscovery hw = reforge_printer();

    CHECK(helix::zoffset::build_tool_adjust_gcode(hw, -1, -25).empty());
    CHECK(helix::zoffset::build_tool_save_gcode(hw, -1, -25).empty());
}
