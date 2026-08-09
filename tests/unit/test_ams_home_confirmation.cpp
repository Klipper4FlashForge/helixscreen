// SPDX-License-Identifier: GPL-3.0-or-later
#include "../lvgl_test_fixture.h"
#include "ams_backend_afc.h"

#include "../catch_amalgamated.hpp"

namespace {

/// Captures dispatched gcode and lets a test drive the homed answer.
/// Overrides BOTH execute_gcode forms so neither falls through to the base.
class HomingProbeBackend : public AmsBackendAfc {
  public:
    HomingProbeBackend() : AmsBackendAfc(nullptr, nullptr) {}

    AmsError execute_gcode(const std::string& gcode) override {
        captured.push_back(gcode);
        return AmsErrorHelper::success();
    }
    AmsError execute_gcode(const std::string& gcode, std::function<void()> on_complete) override {
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
