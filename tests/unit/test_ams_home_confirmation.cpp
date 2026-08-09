// SPDX-License-Identifier: GPL-3.0-or-later
#include "../lvgl_test_fixture.h"
#include "ams_backend_afc.h"

#include "../catch_amalgamated.hpp"

namespace {

/// Captures dispatched gcode and lets a test drive the homed answer.
/// Overrides BOTH execute_gcode forms so neither falls through to the base.
///
/// fail_next_gcode simulates a command failure with no live api_ to carry a
/// real async MoonrakerError: with api_ null, ensure_homed_then()'s only
/// failure signal for the (fixture-driven) G28/payload dispatch is the
/// AmsError these overrides return, which it translates into the
/// MoonrakerError passed to on_error. Deliberately NOT a stored
/// std::function<void(const MoonrakerError&)> callback invoked directly by
/// the override -- the override has no way to receive ensure_homed_then's
/// on_error as a parameter (the 1-arg/2-arg execute_gcode virtuals predate
/// on_error and can't grow it without breaking ~20 other fixtures), so the
/// return-value channel is what's actually reachable here.
class HomingProbeBackend : public AmsBackendAfc {
  public:
    HomingProbeBackend() : AmsBackendAfc(nullptr, nullptr) {}

    AmsError execute_gcode(const std::string& gcode) override {
        if (fail_next_gcode) {
            return AmsError(AmsResult::COMMAND_FAILED, "boom", "boom");
        }
        captured.push_back(gcode);
        return AmsErrorHelper::success();
    }
    AmsError execute_gcode(const std::string& gcode, std::function<void()> on_complete) override {
        if (fail_next_gcode) {
            return AmsError(AmsResult::COMMAND_FAILED, "boom", "boom");
        }
        captured.push_back(gcode);
        if (on_complete) {
            on_complete();
        }
        return AmsErrorHelper::success();
    }
    bool toolhead_homed() const override {
        return homed;
    }

    bool homed = true;
    bool fail_next_gcode = false;
    std::vector<std::string> captured;
};

} // namespace

TEST_CASE("ensure_homed_then dispatches directly when already homed", "[ams][homing]") {
    LVGLTestFixture fixture;
    HomingProbeBackend backend;
    backend.homed = true;

    backend.ensure_homed_then("CHANGE_TOOL LANE=lane1");

    REQUIRE(backend.captured.size() == 1);
    CHECK(backend.captured[0] == "CHANGE_TOOL LANE=lane1");
}

TEST_CASE("ensure_homed_then sends G28 before the payload when unhomed", "[ams][homing]") {
    LVGLTestFixture fixture;
    HomingProbeBackend backend;
    backend.homed = false;

    backend.ensure_homed_then("CHANGE_TOOL LANE=lane1");

    REQUIRE(backend.captured.size() == 2);
    CHECK(backend.captured[0] == "G28");
    CHECK(backend.captured[1] == "CHANGE_TOOL LANE=lane1");
}

TEST_CASE("ensure_homed_then skip_homing bypasses the home entirely", "[ams][homing]") {
    LVGLTestFixture fixture;
    HomingProbeBackend backend;
    backend.homed = false;

    backend.ensure_homed_then("BOX_LOAD", nullptr, nullptr, MoonrakerAPI::AMS_OPERATION_TIMEOUT_MS,
                              /*skip_homing=*/true);

    REQUIRE(backend.captured.size() == 1);
    CHECK(backend.captured[0] == "BOX_LOAD");
}

TEST_CASE("ensure_homed_then reports G28 failure through on_error", "[ams][homing]") {
    LVGLTestFixture fixture;
    HomingProbeBackend backend;
    backend.homed = false;
    backend.fail_next_gcode = true;

    std::string seen;
    backend.ensure_homed_then("CHANGE_TOOL LANE=lane1", nullptr,
                              [&seen](const MoonrakerError& e) { seen = e.message; });

    CHECK(seen == "boom");
    CHECK(backend.captured.empty());
}
