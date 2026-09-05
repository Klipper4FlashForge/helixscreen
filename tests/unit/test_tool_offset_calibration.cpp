// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_tool_offset_calibration.cpp
 * @brief Tests for the tool-offset measuring abstraction.
 *
 * helix::tool_offsets owns what a tool's offset IS; this module owns how a
 * machine measures one for itself. The firmwares disagree on the commands, on
 * whether a reference fixture exists, and on what the resulting numbers mean -
 * so these tests are also the guard on that boundary. They exercise the
 * capability API and reach a firmware only through a printer's object list.
 */

#include "printer_discovery.h"
#include "tool_offset_calibration.h"

#include "../catch_amalgamated.hpp"
#include "hv/json.hpp"

using helix::PrinterDiscovery;
using nlohmann::json;
namespace toc = helix::tool_offset_calibration;

namespace {

/// Objects for a klipper-toolchanger machine with @p tool_count toolheads.
/// @param with_calibrate include [tools_calibrate], i.e. probing hardware.
json toolchanger_objects(int tool_count, bool with_calibrate) {
    json objects = json::array({"gcode_move", "toolhead", "extruder", "toolchanger"});
    if (with_calibrate) {
        // The extra AND the wrapper macro. The app gates on the macro, because
        // the macro is what it sends.
        objects.push_back("tools_calibrate");
        objects.push_back("gcode_macro CALIBRATE_TOOL_OFFSETS");
    }
    for (int i = 0; i < tool_count; ++i) {
        objects.push_back("tool T" + std::to_string(i));
    }
    return objects;
}

/// A 4-tool klipper-toolchanger with a nozzle-touch probe.
PrinterDiscovery calibrating_printer() {
    PrinterDiscovery hw;
    hw.parse_objects(toolchanger_objects(4, true));
    return hw;
}

/// The same machine with no probing hardware - offsets typed in by hand.
PrinterDiscovery manual_printer() {
    PrinterDiscovery hw;
    hw.parse_objects(toolchanger_objects(4, false));
    return hw;
}

/// A plain single-toolhead printer.
PrinterDiscovery plain_printer() {
    PrinterDiscovery hw;
    hw.parse_objects(json::array({"gcode_move", "toolhead", "extruder", "tools_calibrate"}));
    return hw;
}

/// A status frame carrying one tool's three offsets.
json tool_frame(const std::string& name, double x, double y, double z) {
    json status;
    status["tool " + name] = {{"gcode_x_offset", x}, {"gcode_y_offset", y}, {"gcode_z_offset", z}};
    return status;
}

} // namespace

// ============================================================================
// Capability gating
// ============================================================================

TEST_CASE("tool offset calibration: a probe-equipped tool changer supports it",
          "[tool_offset_calibration]") {
    PrinterDiscovery hw = calibrating_printer();

    CHECK(toc::supported(hw));
    CHECK(toc::provider_name(hw) == "klipper-toolchanger");
}

TEST_CASE("tool offset calibration: a tool changer without the macro does not",
          "[tool_offset_calibration]") {
    // We gate on what we SEND. Without CALIBRATE_TOOL_OFFSETS the Calibrate
    // button would issue a command Klipper rejects as unknown.
    CHECK_FALSE(toc::supported(manual_printer()));
    CHECK(toc::provider_name(manual_printer()).empty());
}

TEST_CASE("tool offset calibration: the extra without its wrapper macro is unsupported",
          "[tool_offset_calibration]") {
    // A deliberate, and lossy, tradeoff. CALIBRATE_TOOL_OFFSETS is shipped as a
    // printer.cfg EXAMPLE rather than as part of the extra, so a machine can run
    // tools_calibrate and still not have it. Gating on the macro means such a
    // printer reads as unsupported - which is the honest answer, because the
    // alternative is driving the extra's primitives ourselves and guessing at
    // every precondition the wrapper exists to own.
    PrinterDiscovery hw;
    hw.parse_objects(json::array({"gcode_move", "toolhead", "extruder", "toolchanger",
                                  "tools_calibrate", "tool T0", "tool T1"}));

    CHECK_FALSE(toc::supported(hw));
}

TEST_CASE("tool offset calibration: a single-toolhead printer does not",
          "[tool_offset_calibration]") {
    // [tools_calibrate] with no toolchanger is meaningless - there is no second
    // tool to measure against the first.
    CHECK_FALSE(toc::supported(plain_printer()));
}

// ============================================================================
// Presentation - the seam that lets a second firmware show different numbers
// ============================================================================

TEST_CASE("tool offset calibration: klipper-toolchanger shows deltas, no reference row",
          "[tool_offset_calibration]") {
    // Its result is folded into each tool's own gcode offset, so the probe
    // position is the zero the numbers are already expressed against, not a
    // second set of numbers to display.
    toc::Presentation p = toc::presentation(calibrating_printer());

    CHECK(std::string(p.columns[0]) == "X");
    CHECK(std::string(p.columns[1]) == "Y");
    CHECK(std::string(p.columns[2]) == "Z");
    CHECK(std::string(p.caption) == "Offsets are measured against the base tool.");
    CHECK_FALSE(p.has_reference_row);
    CHECK(std::string(p.reference_caption).empty());
}

TEST_CASE("tool offset calibration: an unsupported printer yields empty captions",
          "[tool_offset_calibration]") {
    // A caller that renders the presentation anyway must get empty strings
    // rather than a dangling pointer.
    toc::Presentation p = toc::presentation(plain_printer());

    CHECK(std::string(p.caption).empty());
    CHECK_FALSE(p.has_reference_row);
}

TEST_CASE("tool offset calibration: no reference row means no reference reading",
          "[tool_offset_calibration]") {
    json status = tool_frame("T1", 0.4, -0.08, -0.13);

    CHECK_FALSE(toc::read_reference(status).has_value());
}

// ============================================================================
// Subscription
// ============================================================================

TEST_CASE("tool offset calibration: the panel queries every tool object",
          "[tool_offset_calibration]") {
    // The panel does a one-shot query rather than riding the tool-changer
    // subscription, so the objects have to be named here or the rows never
    // populate.
    std::vector<std::string> objects = toc::required_status_objects(calibrating_printer());
    std::sort(objects.begin(), objects.end());

    CHECK(objects == std::vector<std::string>{"tool T0", "tool T1", "tool T2", "tool T3"});
}

TEST_CASE("tool offset calibration: an unsupported printer asks for nothing",
          "[tool_offset_calibration]") {
    CHECK(toc::required_status_objects(manual_printer()).empty());
}

// ============================================================================
// Reading results
// ============================================================================

TEST_CASE("tool offset calibration: a tool's three offsets read off its own object",
          "[tool_offset_calibration]") {
    json status = tool_frame("T1", 0.412, -0.087, -0.135);

    auto r = toc::read_tool(status, 1, "T1");
    REQUIRE(r.has_value());
    CHECK(r->x == Catch::Approx(0.412));
    CHECK(r->y == Catch::Approx(-0.087));
    CHECK(r->z == Catch::Approx(-0.135));
}

TEST_CASE("tool offset calibration: zero is a value, not an absence", "[tool_offset_calibration]") {
    // A reported 0.000 is a number the firmware reported, and gets shown as
    // one. Only the ABSENCE of the field is "no news". Conflating the two would
    // blank a row whose offset genuinely is zero - and on the firmwares that
    // express tools against a base tool, that is a row the operator sees every
    // session.
    json status = tool_frame("T0", 0.0, 0.0, 0.0);

    auto r = toc::read_tool(status, 0, "T0");
    REQUIRE(r.has_value());
    CHECK(r->x == Catch::Approx(0.0));
}

TEST_CASE("tool offset calibration: a frame with no news about the tool is nullopt",
          "[tool_offset_calibration]") {
    // Moonraker republishes only what CHANGED, so a frame about T1 carrying
    // nothing for T2 is routine. Answering zero would silently wipe T2's row.
    json status = tool_frame("T1", 0.4, -0.08, -0.13);

    CHECK_FALSE(toc::read_tool(status, 2, "T2").has_value());
}

TEST_CASE("tool offset calibration: a partial tool object is not a reading",
          "[tool_offset_calibration]") {
    // Two real numbers beside a fabricated zero would read as a measured axis
    // that was never measured.
    json status;
    status["tool T1"] = {{"gcode_x_offset", 0.4}, {"gcode_y_offset", -0.08}};

    CHECK_FALSE(toc::read_tool(status, 1, "T1").has_value());
}

TEST_CASE("tool offset calibration: a malformed or empty frame is ignored",
          "[tool_offset_calibration]") {
    CHECK_FALSE(toc::read_tool(json::object(), 0, "T0").has_value());
    CHECK_FALSE(toc::read_tool(json::array(), 0, "T0").has_value());
    CHECK_FALSE(toc::read_tool(json(nullptr), 0, "T0").has_value());

    json wrong_type;
    wrong_type["tool T0"] = "not an object";
    CHECK_FALSE(toc::read_tool(wrong_type, 0, "T0").has_value());
}

TEST_CASE("tool offset calibration: a negative tool index reads nothing",
          "[tool_offset_calibration]") {
    // -1 is ToolState's "no active tool".
    json status = tool_frame("T0", 0.0, 0.0, 0.0);

    CHECK_FALSE(toc::read_tool(status, -1, "T0").has_value());
}

TEST_CASE("tool offset calibration: an unnamed tool reads nothing", "[tool_offset_calibration]") {
    // klipper-toolchanger keys the status object off the tool's NAME, so an
    // empty name would query the object literally called "tool ".
    json status = tool_frame("T0", 0.0, 0.0, 0.0);

    CHECK_FALSE(toc::read_tool(status, 0, "").has_value());
}

// ============================================================================
// Commands
// ============================================================================

TEST_CASE("tool offset calibration: the whole run is one command", "[tool_offset_calibration]") {
    // The wrapper owns the reference pass, the machine state it needs, the
    // temperature, and which tools exist - so there is nothing for us to
    // sequence, and no per-tool form to offer.
    CHECK(toc::calibrate_gcode(calibrating_printer()) ==
          std::vector<std::string>{"CALIBRATE_TOOL_OFFSETS"});
}

TEST_CASE("tool offset calibration: the macro is also the instruction text",
          "[tool_offset_calibration]") {
    // Its `description:` is the firmware's own words for its own hardware,
    // which beats anything we could write generically.
    CHECK(toc::hint_command(calibrating_printer()) == "CALIBRATE_TOOL_OFFSETS");
    CHECK(toc::hint_command(manual_printer()).empty());
}

TEST_CASE("tool offset calibration: persisting stages every axis, then commits",
          "[tool_offset_calibration]") {
    // SAVE_TOOL_PARAMETER takes no value - it persists whatever the tool
    // currently HOLDS. Staging explicitly is what makes this correct without
    // knowing where the calibration pass put its result; a bare SAVE_CONFIG
    // would be a bet that the pass had already staged a pending config change,
    // and would persist nothing if it had not.
    PrinterDiscovery hw = calibrating_printer();

    CHECK(toc::persist_requires_save_config(hw));
    CHECK(toc::save_gcode(hw, {1}) ==
          std::vector<std::string>{"SAVE_TOOL_PARAMETER T=1 PARAMETER=gcode_x_offset",
                                   "SAVE_TOOL_PARAMETER T=1 PARAMETER=gcode_y_offset",
                                   "SAVE_TOOL_PARAMETER T=1 PARAMETER=gcode_z_offset",
                                   "SAVE_CONFIG"});
}

TEST_CASE("tool offset calibration: the commit comes once, after every tool",
          "[tool_offset_calibration]") {
    // SAVE_CONFIG restarts Klipper. One per tool would restart it three times
    // and lose the tools staged after the first restart.
    const auto lines = toc::save_gcode(calibrating_printer(), {0, 2});

    CHECK(std::count(lines.begin(), lines.end(), std::string("SAVE_CONFIG")) == 1);
    CHECK(lines.back() == "SAVE_CONFIG");
    CHECK(lines.size() == 7); // 3 axes x 2 tools + the commit
}

TEST_CASE("tool offset calibration: nothing to persist emits nothing",
          "[tool_offset_calibration]") {
    // A bare SAVE_CONFIG with no staged tool would restart Klipper for nothing.
    CHECK(toc::save_gcode(calibrating_printer(), {}).empty());
    CHECK(toc::save_gcode(calibrating_printer(), {-1}).empty());
}

TEST_CASE("tool offset calibration: an unsupported printer emits no commands",
          "[tool_offset_calibration]") {
    // Empty is the "this printer needs no such call" answer. A caller that
    // sent it blindly would inject a bare newline.
    PrinterDiscovery hw = manual_printer();

    CHECK(toc::calibrate_gcode(hw).empty());
    CHECK(toc::save_gcode(hw, {0}).empty());
    CHECK_FALSE(toc::persist_requires_save_config(hw));
}
