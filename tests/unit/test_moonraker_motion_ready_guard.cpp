// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_moonraker_motion_ready_guard.cpp
 * @brief Tests for the "no motion G-code unless Klipper is READY" guard.
 *
 * MoonrakerMotionAPI::execute_gcode refuses jog/home/move G-code whenever
 * klippy_state != READY (STARTUP, SHUTDOWN, ERROR). Rationale: a jog issued
 * while the printer is initializing is either rejected by Klipper with a raw
 * "Printer not ready" error, or — during STARTUP — silently queued and fired
 * minutes later, long after the user has moved on. Motion is discretionary user
 * input with no recovery role, so it is blocked at the send boundary and a
 * friendly NOT_READY error is surfaced instead.
 *
 * This guard is stricter than MoonrakerAPI::execute_gcode's klippy gate, which
 * lets STARTUP through so queued recovery gcode can run.
 */

#include "../../include/moonraker_api.h"
#include "../../include/moonraker_client_mock.h"
#include "../../include/printer_state.h"
#include "../lvgl_test_fixture.h"

#include "../catch_amalgamated.hpp"

using namespace helix;

namespace {

class MotionReadyGuardFixture : public LVGLTestFixture {
  public:
    MotionReadyGuardFixture() : mock_client(MoonrakerClientMock::PrinterType::VORON_24) {
        state.init_subjects(false);
        state.set_klippy_state_sync(KlippyState::READY);
        mock_client.connect("ws://mock/websocket", []() {}, []() {});
        api = std::make_unique<MoonrakerAPI>(mock_client, state);
    }

    ~MotionReadyGuardFixture() override {
        mock_client.stop_temperature_simulation();
        mock_client.disconnect();
        api.reset();
    }

    void set_klippy(KlippyState s) { state.set_klippy_state_sync(s); }

    void error_cb(const MoonrakerError& err) {
        error_called = true;
        captured_error = err;
    }

    MoonrakerClientMock mock_client;
    PrinterState state;
    std::unique_ptr<MoonrakerAPI> api;

    bool error_called = false;
    MoonrakerError captured_error;
};

} // namespace

// ============================================================================
// Case 1: READY allows a jog move
// ============================================================================

TEST_CASE_METHOD(MotionReadyGuardFixture, "move_axis sends jog when Klipper is READY",
                 "[motion_guard][mock]") {
    set_klippy(KlippyState::READY);
    api->motion().move_axis('X', 10.0, 6000.0, nullptr,
                            [this](const MoonrakerError& err) { error_cb(err); });

    CHECK_FALSE(error_called);
    REQUIRE(mock_client.last_send_method() == "printer.gcode.script");
    CHECK_FALSE(mock_client.gcode_script_history().empty());
}

// ============================================================================
// Case 2: non-READY klippy states block the jog — no gcode sent, on_error fired
// ============================================================================

TEST_CASE_METHOD(MotionReadyGuardFixture, "move_axis refused unless Klipper is READY",
                 "[motion_guard][mock]") {
    auto expect_blocked = [this](KlippyState s) {
        set_klippy(s);
        mock_client.clear_gcode_script_history();
        error_called = false;

        api->motion().move_axis('X', 10.0, 6000.0, nullptr,
                                [this](const MoonrakerError& err) { error_cb(err); });

        CHECK(error_called);
        CHECK(captured_error.type == MoonrakerErrorType::NOT_READY);
        CHECK(mock_client.gcode_script_history().empty());
    };

    SECTION("STARTUP blocks jog (would otherwise queue-and-fire-late)") {
        expect_blocked(KlippyState::STARTUP);
    }
    SECTION("SHUTDOWN blocks jog") { expect_blocked(KlippyState::SHUTDOWN); }
    SECTION("ERROR blocks jog") { expect_blocked(KlippyState::ERROR); }
}

// ============================================================================
// Case 3: the same guard covers home_axes (also routes through execute_gcode)
// ============================================================================

TEST_CASE_METHOD(MotionReadyGuardFixture, "home_axes refused while Klipper is starting up",
                 "[motion_guard][mock]") {
    set_klippy(KlippyState::STARTUP);
    api->motion().home_axes("XY", nullptr,
                            [this](const MoonrakerError& err) { error_cb(err); });

    CHECK(error_called);
    CHECK(captured_error.type == MoonrakerErrorType::NOT_READY);
    CHECK(mock_client.gcode_script_history().empty());
}
