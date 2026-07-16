// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_moonraker_api_busy_guard.cpp
 * @brief Tests for the "refuse discretionary gcode while homing/probing" guard.
 *
 * A blocking non-print operation (G28, BED_MESH_CALIBRATE, QGL, PROBE_ACCURACY,
 * manual probe) holds Klipper's single-threaded gcode lock. Discretionary gcode
 * (fan/temp/non-homing move/LED) sent meanwhile would queue behind it and time
 * out after 60s. The guard in BOTH MoonrakerAPI::execute_gcode and
 * MoonrakerMotionAPI::execute_gcode refuses such commands early with a toast,
 * while letting recovery/homing/probe-control and macros pass, and never blocks
 * during a real file print.
 *
 * Motivated by debug bundle 7CT79XXK (Sovol SV08): 4 fan commands each timed out
 * at 60s during a Cartographer calibration.
 */

#include "../../include/moonraker_api.h"
#include "../../include/moonraker_client_mock.h"
#include "../../include/printer_state.h"
#include "../lvgl_test_fixture.h"

#include "../catch_amalgamated.hpp"

using namespace helix;

namespace {

class BusyGuardApiFixture : public LVGLTestFixture {
  public:
    BusyGuardApiFixture() : mock_client(MoonrakerClientMock::PrinterType::VORON_24) {
        state.init_subjects(false);
        // execute_gcode()'s klippy-halted gate would otherwise reject everything:
        // subjects initialize to SHUTDOWN until a real state update arrives.
        state.set_klippy_state_sync(KlippyState::READY);
        // Default to idle/standby: nothing blocking.
        set_print_state(PrintJobState::STANDBY);
        mock_client.connect("ws://mock/websocket", []() {}, []() {});
        api = std::make_unique<MoonrakerAPI>(mock_client, state);
    }

    ~BusyGuardApiFixture() override {
        mock_client.stop_temperature_simulation();
        mock_client.disconnect();
        api.reset();
    }

    void set_print_state(PrintJobState s) {
        lv_subject_set_int(state.get_print_state_enum_subject(), static_cast<int>(s));
    }
    void set_idle_printing(bool on) {
        lv_subject_set_int(state.get_idle_timeout_printing_subject(), on ? 1 : 0);
    }
    void set_manual_probe(bool on) {
        lv_subject_set_int(state.get_manual_probe_active_subject(), on ? 1 : 0);
    }

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
// Discretionary gcode blocked while a blocking non-print op is active
// ============================================================================

TEST_CASE_METHOD(BusyGuardApiFixture,
                 "execute_gcode refuses discretionary gcode while homing/leveling",
                 "[busy_guard][mock]") {
    SECTION("idle_timeout Printing + not a file print blocks a fan command") {
        set_idle_printing(true); // homing/leveling holds the gcode lock
        set_print_state(PrintJobState::STANDBY);

        api->execute_gcode("M106 S255", nullptr,
                           [this](const MoonrakerError& err) { error_cb(err); });

        CHECK(error_called);
        CHECK(captured_error.type == MoonrakerErrorType::NOT_READY);
        CHECK(captured_error.message == "Printer is busy — try again in a moment");
        CHECK(mock_client.gcode_script_history().empty());
    }

    SECTION("manual probe active blocks a temp command") {
        set_idle_printing(false); // idle_timeout can bounce to Ready between TESTZ
        set_manual_probe(true);

        api->execute_gcode("M104 S200", nullptr,
                           [this](const MoonrakerError& err) { error_cb(err); });

        CHECK(error_called);
        CHECK(captured_error.type == MoonrakerErrorType::NOT_READY);
        CHECK(mock_client.gcode_script_history().empty());
    }
}

// ============================================================================
// Discretionary gcode is NOT blocked when the printer is idle
// ============================================================================

TEST_CASE_METHOD(BusyGuardApiFixture, "execute_gcode allows discretionary gcode when idle",
                 "[busy_guard][mock]") {
    set_idle_printing(false);
    set_manual_probe(false);
    set_print_state(PrintJobState::STANDBY);

    api->execute_gcode("M106 S128", nullptr,
                       [this](const MoonrakerError& err) { error_cb(err); });

    CHECK_FALSE(error_called);
    REQUIRE(mock_client.last_send_method() == "printer.gcode.script");
    CHECK_FALSE(mock_client.gcode_script_history().empty());
}

// ============================================================================
// Discretionary gcode is NOT blocked during a real file print
// ============================================================================

TEST_CASE_METHOD(BusyGuardApiFixture,
                 "execute_gcode allows discretionary gcode during an active print",
                 "[busy_guard][mock]") {
    // idle_timeout is "Printing" during a file print too, but mid-print fan/temp
    // tweaks are legitimate — Klipper queues them between moves, no 60s stall.
    set_idle_printing(true);

    SECTION("PRINTING allows fan") {
        set_print_state(PrintJobState::PRINTING);
        api->execute_gcode("M106 S255", nullptr,
                           [this](const MoonrakerError& err) { error_cb(err); });
        CHECK_FALSE(error_called);
        REQUIRE(mock_client.last_send_method() == "printer.gcode.script");
    }

    SECTION("PAUSED allows fan") {
        set_print_state(PrintJobState::PAUSED);
        api->execute_gcode("M106 S255", nullptr,
                           [this](const MoonrakerError& err) { error_cb(err); });
        CHECK_FALSE(error_called);
        REQUIRE(mock_client.last_send_method() == "printer.gcode.script");
    }
}

// ============================================================================
// Non-discretionary (recovery/homing) gcode is NEVER blocked by the busy guard
// ============================================================================

TEST_CASE_METHOD(BusyGuardApiFixture,
                 "execute_gcode always allows non-discretionary gcode even while busy",
                 "[busy_guard][mock]") {
    set_idle_printing(true);
    set_print_state(PrintJobState::STANDBY);

    SECTION("emergency stop passes") {
        api->execute_gcode("M112", nullptr,
                           [this](const MoonrakerError& err) { error_cb(err); });
        CHECK_FALSE(error_called);
        REQUIRE(mock_client.last_send_method() == "printer.gcode.script");
    }

    SECTION("manual-probe control (ACCEPT) passes so the user can finish/abort") {
        set_manual_probe(true);
        api->execute_gcode("ACCEPT", nullptr,
                           [this](const MoonrakerError& err) { error_cb(err); });
        CHECK_FALSE(error_called);
        REQUIRE(mock_client.last_send_method() == "printer.gcode.script");
    }
}

// ============================================================================
// Self-inflicted busy (app's own jog in flight) is NOT blocked
// ============================================================================

TEST_CASE_METHOD(BusyGuardApiFixture,
                 "execute_gcode allows discretionary gcode while app motion is in flight",
                 "[busy_guard][mock]") {
    // idle_timeout reports "Printing" during any move, including our own jog. When
    // the app itself has motion outstanding, the busy-ness is self-inflicted, so
    // back-to-back jogs must pass instead of self-blocking with a busy toast.
    set_idle_printing(true);
    set_print_state(PrintJobState::STANDBY);
    state.app_motion_activity().note_sent();

    api->execute_gcode("M106 S255", nullptr,
                       [this](const MoonrakerError& err) { error_cb(err); });

    CHECK_FALSE(error_called);
    REQUIRE(mock_client.last_send_method() == "printer.gcode.script");
    CHECK_FALSE(mock_client.gcode_script_history().empty());

    state.app_motion_activity().note_done();
}

// ============================================================================
// The refusal reason survives all the way to the user-facing string
// ============================================================================

TEST_CASE("MoonrakerError::user_message prefers a populated message for NOT_READY",
          "[busy_guard][errors]") {
    // user_message() returned the type-based string BEFORE checking `message`,
    // so every guard's specific reason was overridden by the generic
    // "wait for initialization" text — which is actively wrong for a transient
    // blocking op the user should just retry.
    MoonrakerError busy;
    busy.type = MoonrakerErrorType::NOT_READY;
    busy.message = "Printer is busy — try again in a moment";
    CHECK(busy.user_message() == "Printer is busy — try again in a moment");

    MoonrakerError homing;
    homing.type = MoonrakerErrorType::NOT_READY;
    homing.message = "Homing is disabled while a print is in progress";
    CHECK(homing.user_message() == "Homing is disabled while a print is in progress");

    // An empty message still falls back to the generic type text.
    MoonrakerError bare;
    bare.type = MoonrakerErrorType::NOT_READY;
    CHECK(bare.user_message() == "Printer is not ready. Please wait for initialization.");

    // TIMEOUT / CONNECTION_LOST deliberately keep their curated type text: their
    // `message` fields carry diagnostic detail ("WebSocket connection lost"),
    // which is jargon beside the friendly string. Narrow fix, not a blanket
    // "message always wins".
    CHECK(MoonrakerError::connection_lost("printer.gcode.script").user_message() ==
          "Connection to printer lost.");
    CHECK(MoonrakerError::timeout("printer.gcode.script", 30000).user_message() ==
          "Request timed out. The printer may be busy.");
}

TEST_CASE_METHOD(BusyGuardApiFixture, "busy refusal reaches the user as the busy reason",
                 "[busy_guard][mock][errors]") {
    set_idle_printing(true);
    set_print_state(PrintJobState::STANDBY);

    api->execute_gcode("M106 S255", nullptr,
                       [this](const MoonrakerError& err) { error_cb(err); });

    REQUIRE(error_called);
    // What the toast actually renders — the whole point of the guard's message.
    CHECK(captured_error.user_message() == "Printer is busy — try again in a moment");
}

// ============================================================================
// Motion API routes through the same guard
// ============================================================================

TEST_CASE_METHOD(BusyGuardApiFixture,
                 "motion execute_gcode refuses discretionary move while busy",
                 "[busy_guard][mock][motion]") {
    set_idle_printing(true);
    set_print_state(PrintJobState::STANDBY);

    // move_axis emits a wrapped relative move ("G91\nG0 X..\nG90") — every line
    // is discretionary, so the whole jog is refused.
    SECTION("discretionary jog blocked") {
        api->motion().move_axis('X', 10.0, 3000.0, nullptr,
                                [this](const MoonrakerError& err) { error_cb(err); });
        CHECK(error_called);
        CHECK(captured_error.type == MoonrakerErrorType::NOT_READY);
        CHECK(mock_client.gcode_script_history().empty());
    }

    SECTION("jog allowed once idle") {
        set_idle_printing(false);
        api->motion().move_axis('X', 10.0, 3000.0, nullptr,
                                [this](const MoonrakerError& err) { error_cb(err); });
        CHECK_FALSE(error_called);
        REQUIRE(mock_client.last_send_method() == "printer.gcode.script");
    }
}
