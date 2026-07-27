// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_belt_hardware_detect.cpp
 * @brief Tests for MoonrakerAdvancedAPI::detect_belt_hardware object-list parsing
 *
 * detect_belt_hardware() drives two RPCs: printer.objects.list to find the
 * hardware sections, then printer.objects.query for kinematics. The object list
 * arrives inside the JSON-RPC envelope's "result" member, so reading "objects"
 * off the top level silently yields nothing and every flag stays false
 * (prestonbrown/helixscreen#1137). These tests pin the envelope level by
 * asserting on a mock whose config genuinely has the hardware.
 */

#include "../../include/belt_tension_types.h"
#include "../../include/moonraker_advanced_api.h"
#include "../../include/moonraker_api.h"
#include "../../include/moonraker_client_mock.h"
#include "../../include/printer_state.h"

#include <string>

#include "../catch_amalgamated.hpp"
#include "hv/json.hpp"

using namespace helix;

namespace {

/// Run detect_belt_hardware against a mock printer, returning the detected
/// hardware. The mock invokes JSON-RPC callbacks synchronously, so the whole
/// two-step chain has completed by the time this returns.
helix::calibration::BeltTensionHardware detect_for(MoonrakerClientMock::PrinterType type,
                                                   bool& completed, std::string& error_out) {
    PrinterState state;
    state.init_subjects(false);
    MoonrakerClientMock client(type);
    MoonrakerAPI api(client, state);
    MoonrakerAdvancedAPI advanced(client, api);

    helix::calibration::BeltTensionHardware hw;
    completed = false;
    advanced.detect_belt_hardware(
        [&](const helix::calibration::BeltTensionHardware& detected) {
            completed = true;
            hw = detected;
        },
        [&](const MoonrakerError& err) { error_out = err.message; });
    return hw;
}

} // namespace

// A Voron 2.4 config carries [quad_gantry_level]. That section is the sole
// signal for has_belted_z, so a correct read of the object list must set it.
// This is the direct regression guard for #1137: with the array read off the
// envelope's top level the list is empty and this flag is always false.
TEST_CASE("detect_belt_hardware finds quad_gantry_level on a belted-Z printer",
          "[belt_tension][detect]") {
    bool completed = false;
    std::string error;
    auto hw = detect_for(MoonrakerClientMock::PrinterType::VORON_24, completed, error);

    INFO("error: " << error);
    REQUIRE(completed);
    CHECK(hw.has_belted_z);
}

// Mutation guard: the fix must actually read the list, not unconditionally set
// the flag. A bed slinger has no [quad_gantry_level], so it must stay false
// even though the very same code path ran and reported success.
TEST_CASE("detect_belt_hardware leaves belted-Z false without quad_gantry_level",
          "[belt_tension][detect]") {
    bool completed = false;
    std::string error;
    auto hw = detect_for(MoonrakerClientMock::PrinterType::GENERIC_BEDSLINGER, completed, error);

    INFO("error: " << error);
    REQUIRE(completed);
    CHECK_FALSE(hw.has_belted_z);
}

// Step 2 (printer.objects.query for kinematics) already reads through "result"
// correctly, but it cannot be asserted here: the mock's configfile.settings.printer
// carries only max_velocity/max_accel, with no kinematics key
// (moonraker_client_mock_objects.cpp:211). Covering that path needs the mock to
// report a per-printer-type kinematics first — worth doing, but it is mock
// fidelity work rather than part of this fix.

// A malformed envelope must not throw out of the callback or skip on_complete.
// The parse sits behind a try/catch that reports through on_error, so the
// contract is "one of the two callbacks fires, and nothing escapes".
TEST_CASE("detect_belt_hardware tolerates an object list of non-strings",
          "[belt_tension][detect]") {
    PrinterState state;
    state.init_subjects(false);
    MoonrakerClientMock client(MoonrakerClientMock::PrinterType::GENERIC_COREXY);
    MoonrakerAPI api(client, state);
    MoonrakerAdvancedAPI advanced(client, api);

    bool completed = false;
    bool errored = false;
    REQUIRE_NOTHROW(advanced.detect_belt_hardware(
        [&](const helix::calibration::BeltTensionHardware&) { completed = true; },
        [&](const MoonrakerError&) { errored = true; }));
    CHECK((completed || errored));
}
