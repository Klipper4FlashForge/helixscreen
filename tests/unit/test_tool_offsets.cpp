// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_tool_offsets.cpp
 * @brief Tests for the per-tool z-offset abstraction.
 *
 * A tool changer carries a z-offset per toolhead, independent of the live
 * gcode_move offset the rest of the UI tunes, and the firmwares disagree on
 * where it lives, what writes it, and whether persisting it restarts Klipper.
 * helix::tool_offsets owns all of that. Generic code asks these questions and
 * never names a firmware, so these tests are also the guard on that boundary:
 * they exercise the capability API and reach a vendor only through a printer's
 * object list.
 */

#include "printer_discovery.h"
#include "tool_offsets.h"

#include <algorithm>

#include "../catch_amalgamated.hpp"
#include "hv/json.hpp"

using helix::PrinterDiscovery;
using nlohmann::json;
namespace to_ = helix::tool_offsets;

namespace {

/// A plain single-toolhead printer.
PrinterDiscovery plain_printer() {
    PrinterDiscovery hw;
    hw.parse_objects(json::array({"gcode_move", "toolhead", "extruder"}));
    return hw;
}

/// Objects for a klipper-toolchanger machine with @p tool_count toolheads.
json toolchanger_objects(int tool_count) {
    json objects = json::array({"gcode_move", "toolhead", "extruder", "toolchanger"});
    for (int i = 0; i < tool_count; ++i) {
        objects.push_back("tool T" + std::to_string(i));
    }
    return objects;
}

/// A stock klipper-toolchanger printer.
PrinterDiscovery toolchanger_printer(int tool_count = 4) {
    PrinterDiscovery hw;
    hw.parse_objects(toolchanger_objects(tool_count));
    return hw;
}

/// A single-toolhead printer that happens to define a TOOL_OFFSET macro.
PrinterDiscovery macro_but_one_tool() {
    json objects = json::array({"gcode_move", "toolhead", "extruder"});
    objects.push_back("gcode_macro TOOL_OFFSET");
    PrinterDiscovery hw;
    hw.parse_objects(objects);
    return hw;
}

/// A MedusaHC-style machine: klipper-toolchanger PLUS the TOOL_OFFSET macro.
/// This is the real shape - MedusaHC ships [toolchanger] and [tool T0..T3].
PrinterDiscovery tool_offset_macro_printer(int tool_count = 4) {
    json objects = toolchanger_objects(tool_count);
    objects.push_back("gcode_macro TOOL_OFFSET");
    PrinterDiscovery hw;
    hw.parse_objects(objects);
    return hw;
}

/// The same machine, but printer.cfg spells the section `[gcode_macro
/// Tool_Offset]`. Klipper accepts any casing and uppercases only the COMMAND
/// alias, so this is a legal config that behaves identically on the printer.
PrinterDiscovery mixed_case_macro_printer(int tool_count = 4) {
    json objects = toolchanger_objects(tool_count);
    objects.push_back("gcode_macro Tool_Offset");
    PrinterDiscovery hw;
    hw.parse_objects(objects);
    return hw;
}

bool vec_has(const std::vector<std::string>& v, const std::string& s) {
    return std::find(v.begin(), v.end(), s) != v.end();
}

} // namespace

// ============================================================================
// Detection
// ============================================================================

TEST_CASE("tool offsets: a single-toolhead printer has none", "[tool_offsets]") {
    // This is what gates the tune panel's tool selector. A printer with one
    // nozzle must answer no, or the selector appears with nothing to select.
    PrinterDiscovery hw = plain_printer();

    CHECK_FALSE(to_::supports_per_tool_z(hw));
    CHECK(to_::provider_name(hw).empty());
    CHECK(to_::required_status_objects(hw).empty());
}

TEST_CASE("tool offsets: a tool changer has one per tool", "[tool_offsets]") {
    PrinterDiscovery hw = toolchanger_printer();

    CHECK(to_::supports_per_tool_z(hw));
    CHECK(to_::provider_name(hw) == "klipper-toolchanger");
}

TEST_CASE("tool offsets: klipper-toolchanger needs no extra subscription", "[tool_offsets]") {
    // The offsets ride on the `tool T*` objects the tool-changer subscription
    // already requests, so asking for more would be dead bandwidth.
    CHECK(to_::required_status_objects(toolchanger_printer()).empty());
}

// ============================================================================
// Precedence: the trap that makes row order load-bearing
// ============================================================================

TEST_CASE("tool offsets: the TOOL_OFFSET macro outranks klipper-toolchanger", "[tool_offsets]") {
    // A MedusaHC ships [toolchanger] and [tool T0..T3], so it matches BOTH
    // rows. Its macros print off the TOOL_OFFSET variables and never read
    // klipper-toolchanger's own offset, so resolving to the second row would
    // write a store nothing consumes - the adjustment would silently do
    // nothing on the machine.
    PrinterDiscovery hw = tool_offset_macro_printer();

    CHECK(to_::provider_name(hw) == "TOOL_OFFSET macro");
    CHECK(to_::set_tool_z_gcode(hw, 1, -50).rfind("SET_GCODE_VARIABLE", 0) == 0);
    CHECK(to_::required_status_objects(hw).size() == 1);
    CHECK(vec_has(to_::required_status_objects(hw), "gcode_macro TOOL_OFFSET"));
}

TEST_CASE("tool offsets: a frame carrying both schemas reads the authoritative one",
          "[tool_offsets]") {
    // Same trap on the read path, which has no PrinterDiscovery and resolves by
    // schema. A MedusaHC publishes both, and they can legitimately disagree -
    // klipper-toolchanger's copy is not what the machine prints with.
    json status = json{
        {"gcode_macro TOOL_OFFSET", json{{"t1_off_z", -0.20}}},
        {"tool T1", json{{"gcode_z_offset", -0.05}}},
    };

    auto microns = to_::read_tool_z_microns(status, 1, "T1");
    REQUIRE(microns.has_value());
    CHECK(*microns == -200);
}

TEST_CASE("tool offsets: a TOOL_OFFSET macro alone is not a tool changer", "[tool_offsets]") {
    // supports_per_tool_z() gates the tune panel's selector, and its contract is
    // "false on every single-toolhead printer". TOOL_OFFSET is a plausible name
    // for a hand-written macro, and matching on it alone put the selector on a
    // single-extruder machine and aimed its buttons at `t0_off_z` in a macro
    // with no such variable.
    PrinterDiscovery hw = macro_but_one_tool();

    CHECK_FALSE(to_::supports_per_tool_z(hw));
    CHECK(to_::set_tool_z_gcode(hw, 0, -50).empty());
}

TEST_CASE("tool offsets: a delta frame does not fall through to the other store",
          "[tool_offsets]") {
    // The by-schema read resolves in table order, which is right when both
    // schemas are in ONE frame. Moonraker's notify_status_update carries only
    // what changed, so a MedusaHC frame with just `tool T0` used to fall past
    // the authoritative TOOL_OFFSET row and answer from klipper-toolchanger's
    // copy — the store this module says is NOT the authority. That value then
    // overwrote the real one and became the base for the next adjustment.
    json delta = json{{"tool T0", json{{"gcode_z_offset", -0.05}}}};

    // No TOOL_OFFSET object in this frame at all -> the macro provider is not
    // the owner here, so answering from the tool object is correct.
    CHECK(*to_::read_tool_z_microns(delta, 0, "T0") == -50);

    // But once the macro IS present and simply has nothing for this tool, the
    // frame belongs to it and the tool object must not be consulted.
    json macro_frame = json{
        {"gcode_macro TOOL_OFFSET", json{{"t1_off_z", -0.20}}},
        {"tool T0", json{{"gcode_z_offset", -0.05}}},
    };
    CHECK_FALSE(to_::read_tool_z_microns(macro_frame, 0, "T0").has_value());
    CHECK(*to_::read_tool_z_microns(macro_frame, 1, "T1") == -200);
}

// ============================================================================
// Config casing: detection is case-insensitive, Klipper's keys are not
// ============================================================================

TEST_CASE("tool offsets: a mixed-case macro is subscribed as printer.cfg spells it",
          "[tool_offsets]") {
    // has_macro() is case-insensitive because the callable command is the
    // UPPERCASED alias. The status object key is not: Klipper publishes the
    // config section verbatim, and an object it cannot look up is silently
    // absent from every frame rather than an error. So subscribing to the
    // uppercased name on this machine reads empty forever - the selector shows
    // nothing, and the reader falls through to klipper-toolchanger's copy, the
    // store this module's own table says is not the authority.
    PrinterDiscovery hw = mixed_case_macro_printer();

    REQUIRE(to_::supports_per_tool_z(hw));
    CHECK(to_::provider_name(hw) == "TOOL_OFFSET macro");
    CHECK(vec_has(to_::required_status_objects(hw), "gcode_macro Tool_Offset"));
    CHECK_FALSE(vec_has(to_::required_status_objects(hw), "gcode_macro TOOL_OFFSET"));
}

TEST_CASE("tool offsets: a mixed-case macro's writes use the config-case mux key",
          "[tool_offsets]") {
    // SET_GCODE_VARIABLE's MACRO= is a mux key registered on the config-case
    // name (klippy/extras/gcode_macro.py registers `name`, not `self.alias`),
    // so a capitalised MACRO= is REJECTED - the adjustment errors outright
    // rather than quietly missing.
    PrinterDiscovery hw = mixed_case_macro_printer();

    const std::string set = to_::set_tool_z_gcode(hw, 1, -50);
    CHECK(set.find("MACRO=Tool_Offset ") != std::string::npos);
    CHECK(set.find("MACRO=TOOL_OFFSET") == std::string::npos);

    // The save carries the runtime half, so it is subject to the same trap.
    const std::string save = to_::save_tool_z_gcode(hw, 1, -50);
    CHECK(save.find("MACRO=Tool_Offset ") != std::string::npos);
    CHECK(save.find("SAVE_VARIABLE VARIABLE=t1_gcode_z_offset") != std::string::npos);
}

TEST_CASE("tool offsets: a mixed-case macro object is still read", "[tool_offsets]") {
    // The read path has no PrinterDiscovery to resolve the casing through, so
    // it scans. Two sections differing only in case would register the same
    // command alias and Klipper would refuse to start, so the scan cannot be
    // ambiguous.
    json status = json{{"gcode_macro Tool_Offset", json{{"t2_off_z", -0.15}}}};

    auto microns = to_::read_tool_z_microns(status, 2, "T2");
    REQUIRE(microns.has_value());
    CHECK(*microns == -150);
}

TEST_CASE("tool offsets: a mixed-case macro still owns its frame", "[tool_offsets]") {
    // The fallthrough guard keys off the macro being PRESENT. Matching that
    // case-sensitively would make a Tool_Offset frame look like it had no macro
    // at all, and the tool object underneath would answer for it.
    json frame = json{
        {"gcode_macro Tool_Offset", json{{"t1_off_z", -0.20}}},
        {"tool T0", json{{"gcode_z_offset", -0.05}}},
    };

    CHECK_FALSE(to_::read_tool_z_microns(frame, 0, "T0").has_value());
    CHECK(*to_::read_tool_z_microns(frame, 1, "T1") == -200);
}

// ============================================================================
// Reading
// ============================================================================

TEST_CASE("tool offsets: klipper-toolchanger reads off the tool's own object", "[tool_offsets]") {
    json status = json{{"tool T2", json{{"active", true}, {"gcode_z_offset", -0.15}}}};

    auto microns = to_::read_tool_z_microns(status, 2, "T2");
    REQUIRE(microns.has_value());
    CHECK(*microns == -150);
}

TEST_CASE("tool offsets: the TOOL_OFFSET macro reads off the tool index", "[tool_offsets]") {
    // All four tools live on one macro, keyed by number rather than by object.
    json status = json{{"gcode_macro TOOL_OFFSET",
                        json{{"t0_off_z", 0.10}, {"t1_off_z", -0.20}, {"t2_off_z", 0.0}}}};

    CHECK(*to_::read_tool_z_microns(status, 0, "T0") == 100);
    CHECK(*to_::read_tool_z_microns(status, 1, "T1") == -200);
    CHECK(*to_::read_tool_z_microns(status, 2, "T2") == 0);
    // A tool the macro does not carry is unknown, not zero.
    CHECK_FALSE(to_::read_tool_z_microns(status, 3, "T3").has_value());
}

TEST_CASE("tool offsets: a float that lands just short still rounds", "[tool_offsets]") {
    // The value round-trips through a float, so a nominal -0.150 arrives as
    // -0.1499999. Truncating would report -149 and the UI would drift by a
    // micron every time it echoed the value back.
    json status = json{{"tool T0", json{{"gcode_z_offset", -0.1499999}}}};

    CHECK(*to_::read_tool_z_microns(status, 0, "T0") == -150);
}

TEST_CASE("tool offsets: zero is a value, not an absence", "[tool_offsets]") {
    // A tool genuinely at 0.000 must read as 0, not as "nothing reported" -
    // that distinction is why the UI carries a separate validity subject.
    json status = json{{"tool T0", json{{"gcode_z_offset", 0.0}}}};

    auto microns = to_::read_tool_z_microns(status, 0, "T0");
    REQUIRE(microns.has_value());
    CHECK(*microns == 0);
}

TEST_CASE("tool offsets: a frame without the field is no news", "[tool_offsets]") {
    // Moonraker republishes only what CHANGED, so a frame that carries other
    // tool state and not this one must not be read as a reset to zero.
    json status = json{{"tool T0", json{{"active", true}, {"mounted", true}}}};

    CHECK_FALSE(to_::read_tool_z_microns(status, 0, "T0").has_value());
}

TEST_CASE("tool offsets: a malformed or empty frame is ignored", "[tool_offsets]") {
    CHECK_FALSE(
        to_::read_tool_z_microns(json{{"tool T0", json{{"gcode_z_offset", "oops"}}}}, 0, "T0")
            .has_value());
    CHECK_FALSE(to_::read_tool_z_microns(json::object(), 0, "T0").has_value());
    CHECK_FALSE(to_::read_tool_z_microns(json::array(), 0, "T0").has_value());
    CHECK_FALSE(to_::read_tool_z_microns(json(nullptr), 0, "T0").has_value());
    // No name to key the object off.
    CHECK_FALSE(to_::read_tool_z_microns(json{{"tool T0", json{{"gcode_z_offset", -0.1}}}}, 0, "")
                    .has_value());
}

// ============================================================================
// Writing and persisting
// ============================================================================

TEST_CASE("tool offsets: the klipper-toolchanger write is a bare decimal", "[tool_offsets]") {
    // klipper-toolchanger runs VALUE= through Python's ast.literal_eval(), so
    // the value must be a plain numeric literal. Anything the UI would show a
    // user - "+0.050mm", "-0.05 mm" - raises instead of setting the offset.
    PrinterDiscovery hw = toolchanger_printer();

    CHECK(to_::set_tool_z_gcode(hw, 1, -50) ==
          "SET_TOOL_PARAMETER T=1 PARAMETER=gcode_z_offset VALUE=-0.050");
    CHECK(to_::set_tool_z_gcode(hw, 0, 125) ==
          "SET_TOOL_PARAMETER T=0 PARAMETER=gcode_z_offset VALUE=0.125");
    CHECK(to_::set_tool_z_gcode(hw, 3, 0) ==
          "SET_TOOL_PARAMETER T=3 PARAMETER=gcode_z_offset VALUE=0.000");
}

TEST_CASE("tool offsets: the klipper-toolchanger save sets then persists", "[tool_offsets]") {
    // SAVE_TOOL_PARAMETER persists whatever the tool currently holds and takes
    // no value of its own, so the set has to lead or the save stores the old
    // number.
    PrinterDiscovery hw = toolchanger_printer();

    CHECK(to_::save_tool_z_gcode(hw, 1, -50) ==
          "SET_TOOL_PARAMETER T=1 PARAMETER=gcode_z_offset VALUE=-0.050\n"
          "SAVE_TOOL_PARAMETER T=1 PARAMETER=gcode_z_offset");
}

TEST_CASE("tool offsets: the TOOL_OFFSET macro write targets the macro variable",
          "[tool_offsets]") {
    PrinterDiscovery hw = tool_offset_macro_printer();

    CHECK(to_::set_tool_z_gcode(hw, 2, -75) ==
          "SET_GCODE_VARIABLE MACRO=TOOL_OFFSET VARIABLE=t2_off_z VALUE=-0.075");
}

TEST_CASE("tool offsets: the TOOL_OFFSET macro save writes both stores", "[tool_offsets]") {
    // Durable copy AND the runtime variable the machine actually prints with -
    // writing only save_variables would not change this print.
    PrinterDiscovery hw = tool_offset_macro_printer();

    CHECK(to_::save_tool_z_gcode(hw, 2, -75) ==
          "SAVE_VARIABLE VARIABLE=t2_gcode_z_offset VALUE=-0.075\n"
          "SET_GCODE_VARIABLE MACRO=TOOL_OFFSET VARIABLE=t2_off_z VALUE=-0.075");
}

TEST_CASE("tool offsets: only klipper-toolchanger's save restarts Klipper", "[tool_offsets]") {
    // SAVE_TOOL_PARAMETER stages a config change that SAVE_CONFIG commits, and
    // that restarts Klipper. SAVE_VARIABLE lands immediately. A UI that gets
    // this backwards either leaves the value uncommitted or sits waiting out a
    // restart that never comes.
    CHECK(to_::persist_requires_save_config(toolchanger_printer()));
    CHECK_FALSE(to_::persist_requires_save_config(tool_offset_macro_printer()));
    CHECK_FALSE(to_::persist_requires_save_config(plain_printer()));
}

TEST_CASE("tool offsets: a printer without the capability emits nothing", "[tool_offsets]") {
    // An empty string is the "this printer needs no such call" answer, matching
    // helix::zoffset. A caller that sends it blindly would inject a bare
    // newline, so the emptiness is the contract.
    PrinterDiscovery hw = plain_printer();

    CHECK(to_::set_tool_z_gcode(hw, 0, -50).empty());
    CHECK(to_::save_tool_z_gcode(hw, 0, -50).empty());
}

TEST_CASE("tool offsets: a negative tool index emits nothing", "[tool_offsets]") {
    // -1 is ToolState's "no active tool". Formatting it would send T=-1 or
    // t-1_off_z, which Klipper rejects with an error the caller only logs.
    PrinterDiscovery hw = toolchanger_printer();

    CHECK(to_::set_tool_z_gcode(hw, -1, -50).empty());
    CHECK(to_::save_tool_z_gcode(hw, -1, -50).empty());
    CHECK(to_::set_tool_z_gcode(tool_offset_macro_printer(), -1, -50).empty());
}

// ============================================================================
// Calibration - measuring the offsets rather than carrying them
// ============================================================================

namespace {

/// A klipper-toolchanger machine that can measure its own offsets: the extra
/// AND the wrapper macro the app actually sends.
PrinterDiscovery calibrating_printer() {
    PrinterDiscovery hw;
    json objects = json::array({"gcode_move", "toolhead", "extruder", "toolchanger",
                                "tools_calibrate", "gcode_macro CALIBRATE_TOOL_OFFSETS"});
    for (int i = 0; i < 4; ++i) {
        objects.push_back("tool T" + std::to_string(i));
    }
    hw.parse_objects(objects);
    return hw;
}

/// The same machine with no calibration macro - offsets typed in by hand.
PrinterDiscovery manual_printer() {
    PrinterDiscovery hw;
    hw.parse_objects(toolchanger_objects(4));
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
          "[tool_offsets][calibration]") {
    PrinterDiscovery hw = calibrating_printer();

    CHECK(to_::calibration_supported(hw));
    CHECK(to_::provider_name(hw) == "klipper-toolchanger");
}

TEST_CASE("tool offset calibration: a tool changer without the macro does not",
          "[tool_offsets][calibration]") {
    // We gate on what we SEND. Without CALIBRATE_TOOL_OFFSETS the Calibrate
    // button would issue a command Klipper rejects as unknown.
    CHECK_FALSE(to_::calibration_supported(manual_printer()));
    // The row still MATCHES - this machine carries per-tool offsets and can be
    // nudged, it just cannot measure them. That is the whole point of the two
    // questions sharing one provider.
    CHECK(to_::provider_name(manual_printer()) == "klipper-toolchanger");
    CHECK(to_::supports_per_tool_z(manual_printer()));
}

TEST_CASE("tool offset calibration: the extra without its wrapper macro is unsupported",
          "[tool_offsets][calibration]") {
    // A deliberate, and lossy, tradeoff. CALIBRATE_TOOL_OFFSETS is shipped as a
    // printer.cfg EXAMPLE rather than as part of the extra, so a machine can run
    // tools_calibrate and still not have it. Gating on the macro means such a
    // printer reads as unsupported - which is the honest answer, because the
    // alternative is driving the extra's primitives ourselves and guessing at
    // every precondition the wrapper exists to own.
    PrinterDiscovery hw;
    hw.parse_objects(json::array({"gcode_move", "toolhead", "extruder", "toolchanger",
                                  "tools_calibrate", "tool T0", "tool T1"}));

    CHECK_FALSE(to_::calibration_supported(hw));
}

TEST_CASE("tool offset calibration: a single-toolhead printer does not",
          "[tool_offsets][calibration]") {
    // [tools_calibrate] with no toolchanger is meaningless - there is no second
    // tool to measure against the first.
    CHECK_FALSE(to_::calibration_supported(plain_printer()));
}

// ============================================================================
// Presentation - the seam that lets a second firmware show different numbers
// ============================================================================

TEST_CASE("tool offset calibration: the panel queries every tool object",
          "[tool_offsets][calibration]") {
    // The panel does a one-shot query rather than riding the tool-changer
    // subscription, so the objects have to be named here or the rows never
    // populate.
    std::vector<std::string> objects = to_::calibration_status_objects(calibrating_printer());
    std::sort(objects.begin(), objects.end());

    CHECK(objects == std::vector<std::string>{"tool T0", "tool T1", "tool T2", "tool T3"});
}

TEST_CASE("tool offset calibration: an unsupported printer asks for nothing",
          "[tool_offsets][calibration]") {
    CHECK(to_::calibration_status_objects(manual_printer()).empty());
}

// ============================================================================
// Reading results
// ============================================================================

TEST_CASE("tool offset calibration: a tool's three offsets read off its own object",
          "[tool_offsets][calibration]") {
    json status = tool_frame("T1", 0.412, -0.087, -0.135);

    auto r = to_::read_tool_offsets_microns(status, 1, "T1");
    REQUIRE(r.has_value());
    CHECK(r->x == 412);
    CHECK(r->y == -87);
    CHECK(r->z == -135);
}

TEST_CASE("tool offset calibration: zero is a value, not an absence",
          "[tool_offsets][calibration]") {
    // A reported 0.000 is a number the firmware reported, and gets shown as
    // one. Only the ABSENCE of the field is "no news". Conflating the two would
    // blank a row whose offset genuinely is zero - and on the firmwares that
    // express tools against a base tool, that is a row the operator sees every
    // session.
    json status = tool_frame("T0", 0.0, 0.0, 0.0);

    auto r = to_::read_tool_offsets_microns(status, 0, "T0");
    REQUIRE(r.has_value());
    CHECK(r->x == 0);
}

TEST_CASE("tool offset calibration: a frame with no news about the tool is nullopt",
          "[tool_offsets][calibration]") {
    // Moonraker republishes only what CHANGED, so a frame about T1 carrying
    // nothing for T2 is routine. Answering zero would silently wipe T2's row.
    json status = tool_frame("T1", 0.4, -0.08, -0.13);

    CHECK_FALSE(to_::read_tool_offsets_microns(status, 2, "T2").has_value());
}

TEST_CASE("tool offset calibration: a partial tool object is not a reading",
          "[tool_offsets][calibration]") {
    // Two real numbers beside a fabricated zero would read as a measured axis
    // that was never measured.
    json status;
    status["tool T1"] = {{"gcode_x_offset", 0.4}, {"gcode_y_offset", -0.08}};

    CHECK_FALSE(to_::read_tool_offsets_microns(status, 1, "T1").has_value());
}

TEST_CASE("tool offset calibration: a malformed or empty frame is ignored",
          "[tool_offsets][calibration]") {
    CHECK_FALSE(to_::read_tool_offsets_microns(json::object(), 0, "T0").has_value());
    CHECK_FALSE(to_::read_tool_offsets_microns(json::array(), 0, "T0").has_value());
    CHECK_FALSE(to_::read_tool_offsets_microns(json(nullptr), 0, "T0").has_value());

    json wrong_type;
    wrong_type["tool T0"] = "not an object";
    CHECK_FALSE(to_::read_tool_offsets_microns(wrong_type, 0, "T0").has_value());
}

TEST_CASE("tool offset calibration: a negative tool index reads nothing",
          "[tool_offsets][calibration]") {
    // -1 is ToolState's "no active tool".
    json status = tool_frame("T0", 0.0, 0.0, 0.0);

    CHECK_FALSE(to_::read_tool_offsets_microns(status, -1, "T0").has_value());
}

TEST_CASE("tool offset calibration: an unnamed tool reads nothing", "[tool_offsets][calibration]") {
    // klipper-toolchanger keys the status object off the tool's NAME, so an
    // empty name would query the object literally called "tool ".
    json status = tool_frame("T0", 0.0, 0.0, 0.0);

    CHECK_FALSE(to_::read_tool_offsets_microns(status, 0, "").has_value());
}

// ============================================================================
// Commands
// ============================================================================

TEST_CASE("tool offset calibration: the whole run is one command", "[tool_offsets][calibration]") {
    // The wrapper owns the reference pass, the machine state it needs, the
    // temperature, and which tools exist - so there is nothing for us to
    // sequence, and no per-tool form to offer.
    CHECK(to_::calibrate_gcode(calibrating_printer()) ==
          std::vector<std::string>{"CALIBRATE_TOOL_OFFSETS"});
}

TEST_CASE("tool offset calibration: the macro is also the instruction text",
          "[tool_offsets][calibration]") {
    // Its `description:` is the firmware's own words for its own hardware,
    // which beats anything we could write generically.
    CHECK(to_::calibration_hint_command(calibrating_printer()) == "CALIBRATE_TOOL_OFFSETS");
    CHECK(to_::calibration_hint_command(manual_printer()).empty());
}

TEST_CASE("tool offset calibration: persisting stages every axis, then commits",
          "[tool_offsets][calibration]") {
    // SAVE_TOOL_PARAMETER takes no value - it persists whatever the tool
    // currently HOLDS. Staging explicitly is what makes this correct without
    // knowing where the calibration pass put its result; a bare SAVE_CONFIG
    // would be a bet that the pass had already staged a pending config change,
    // and would persist nothing if it had not.
    PrinterDiscovery hw = calibrating_printer();

    CHECK(to_::persist_requires_save_config(hw));
    CHECK(to_::save_calibration_gcode(hw, {1}) ==
          std::vector<std::string>{"SAVE_TOOL_PARAMETER T=1 PARAMETER=gcode_x_offset",
                                   "SAVE_TOOL_PARAMETER T=1 PARAMETER=gcode_y_offset",
                                   "SAVE_TOOL_PARAMETER T=1 PARAMETER=gcode_z_offset",
                                   "SAVE_CONFIG"});
}

TEST_CASE("tool offset calibration: the commit comes once, after every tool",
          "[tool_offsets][calibration]") {
    // SAVE_CONFIG restarts Klipper. One per tool would restart it three times
    // and lose the tools staged after the first restart.
    const auto lines = to_::save_calibration_gcode(calibrating_printer(), {0, 2});

    CHECK(std::count(lines.begin(), lines.end(), std::string("SAVE_CONFIG")) == 1);
    CHECK(lines.back() == "SAVE_CONFIG");
    CHECK(lines.size() == 7); // 3 axes x 2 tools + the commit
}

TEST_CASE("tool offset calibration: nothing to persist emits nothing",
          "[tool_offsets][calibration]") {
    // A bare SAVE_CONFIG with no staged tool would restart Klipper for nothing.
    CHECK(to_::save_calibration_gcode(calibrating_printer(), {}).empty());
    CHECK(to_::save_calibration_gcode(calibrating_printer(), {-1}).empty());
}

TEST_CASE("tool offset calibration: an unsupported printer emits no commands",
          "[tool_offsets][calibration]") {
    // Empty is the "this printer needs no such call" answer. A caller that
    // sent it blindly would inject a bare newline.
    PrinterDiscovery hw = manual_printer();

    CHECK(to_::calibrate_gcode(hw).empty());
    CHECK(to_::save_calibration_gcode(hw, {0}).empty());
    // persist_requires_save_config() belongs to the VALUE capability, which
    // this printer HAS - it can be nudged and the nudge persisted, it just
    // cannot measure. Asserting it false here would be asserting the two
    // questions are one.
    CHECK(to_::persist_requires_save_config(hw));
}
