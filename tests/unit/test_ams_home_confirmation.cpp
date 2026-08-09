// SPDX-License-Identifier: GPL-3.0-or-later
#include "../lvgl_test_fixture.h"
#include "ams_backend_afc.h"
#include "moonraker_api_mock.h"
#include "moonraker_client_mock.h"
#include "printer_state.h"
#include "test_helpers/update_queue_test_access.h"

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

// =====================================================================
// dispatch_payload's "custom" branch (integration)
// =====================================================================
// Unit-style tests above use HomingProbeBackend, whose api_ is null -- they
// never leave dispatch_payload()'s legacy branch (the hardcoded execute_gcode
// virtuals), so they cannot prove a caller's non-default on_error/timeout_ms/
// silent actually reach MoonrakerAPI::execute_gcode(). MoonrakerAPIMock does
// NOT override execute_gcode() -- it inherits the real implementation and
// round-trips through MoonrakerClientMock, exactly like
// "QIDI Box on_started dispatches printer.objects.query (integration)" in
// test_ams_backend_qidi.cpp. That is the only way to reach the custom branch
// from a test: a live api_/client_ so !api_ doesn't short-circuit it first.
TEST_CASE("ensure_homed_then custom timeout/silent bypass the hardcoded virtuals and reach "
          "MoonrakerAPI::execute_gcode (integration)",
          "[ams][homing][integration]") {
    MoonrakerClientMock client(MoonrakerClientMock::PrinterType::VORON_24);
    helix::PrinterState state;
    state.init_subjects(false);
    MoonrakerAPIMock api(client, state);

    AmsBackendAfc backend(&api, &client);

    constexpr uint32_t kCustomTimeoutMs = 12345;
    REQUIRE(kCustomTimeoutMs != MoonrakerAPI::AMS_OPERATION_TIMEOUT_MS);
    // The mock handler for printer.gcode.script runs synchronously inside
    // send_jsonrpc(), so this fires before ensure_homed_then() even returns --
    // exercising the on_error leg of dispatch_payload()'s custom branch, not
    // just the dispatch itself.
    client.force_next_gcode_error(MoonrakerErrorType::JSON_RPC_ERROR, "boom", "BOX_LOAD");

    std::string seen;
    // skip_homing=true keeps this test on the payload leg, not G28 -- that leg
    // is already covered by the two tests above.
    auto err = backend.ensure_homed_then(
        "BOX_LOAD", nullptr, [&seen](const MoonrakerError& e) { seen = e.message; },
        kCustomTimeoutMs, /*skip_homing=*/true, /*silent=*/false);
    REQUIRE(err.success());

    // Dispatch went straight to MoonrakerAPI::execute_gcode() carrying OUR
    // timeout_ms/silent -- the hardcoded 1-arg/2-arg execute_gcode virtuals fix
    // AMS_OPERATION_TIMEOUT_MS/true and can never produce these values.
    CHECK(client.last_send_method() == "printer.gcode.script");
    CHECK(client.last_send_script() == "BOX_LOAD");
    CHECK(client.last_send_timeout_ms() == kCustomTimeoutMs);
    CHECK_FALSE(client.last_send_silent());

    // The error callback is marshalled through token.defer() (L081 Mechanism
    // C) rather than invoked inline -- drain the queue to run it.
    helix::ui::UpdateQueueTestAccess::drain_all(helix::ui::UpdateQueue::instance());
    CHECK(seen == "boom");
}
