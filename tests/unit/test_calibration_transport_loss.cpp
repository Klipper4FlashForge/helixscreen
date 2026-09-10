// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_calibration_transport_loss.cpp
 * @brief A dropped WebSocket (or an RPC timeout a slow calibration outlived)
 *        is the transport vanishing, not the printer's opinion of the macro
 *        (prestonbrown/helixscreen#1543).
 *
 * Every calibration collector must absorb TIMEOUT/CONNECTION_LOST, keep its
 * notify_gcode_response handler registered, and let the result lines (or the
 * busy->idle follow-up) complete the run. Anything carrying Klipper's own
 * complaint is still terminal. Previously PID/MPC absorbed only TIMEOUT, the
 * other four treated everything as terminal, and no path absorbed
 * CONNECTION_LOST at all — so a dropped socket reported a still-running
 * calibration as failed.
 */

#include "ui_update_queue.h"

#include "../lvgl_ui_test_fixture.h"
#include "app_globals.h"
#include "calibration_types.h"
#include "i_moonraker_sub_apis.h"
#include "moonraker_advanced_api.h"
#include "moonraker_api.h"
#include "moonraker_client_mock.h"
#include "printer_state.h"

#include <atomic>
#include <memory>
#include <string>
#include <thread>

#include "../catch_amalgamated.hpp"

using helix::ui::UpdateQueue;

namespace {

struct TransportLossFixture : public LVGLUITestFixture {
    TransportLossFixture() : mock_client_(MoonrakerClientMock::PrinterType::VORON_24) {
        state().init_subjects(false);
        // The drivers arm the follow-up against the GLOBAL printer state's
        // idle subject — initialize it too, or the arm silently no-ops there.
        get_printer_state().init_subjects(false);
        state().set_klippy_state_sync(helix::KlippyState::READY);
        api_ = std::make_unique<MoonrakerAPI>(mock_client_, state());
    }

    ~TransportLossFixture() override {
        api_.reset();
        UpdateQueue::instance().drain();
    }

    /// Drain everything the driver queued (the idle-fallback arm runs here).
    void settle() {
        UpdateQueue::instance().drain();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    MoonrakerClientMock mock_client_;
    std::unique_ptr<MoonrakerAPI> api_;

    std::atomic<bool> success_{false};
    std::atomic<bool> error_{false};
    std::string captured_error_;
};

/// The follow-up arms against the GLOBAL printer state's idle subject — the
/// one the driver reads (get_printer_state()), not the per-fixture state.
lv_subject_t* global_idle_subject() {
    return get_printer_state().get_idle_timeout_printing_subject();
}

} // namespace

TEST_CASE_METHOD(TransportLossFixture, "PID absorbs CONNECTION_LOST and still delivers the result",
                 "[calibration][transport][1543]") {
    mock_client_.force_next_gcode_error(MoonrakerErrorType::CONNECTION_LOST,
                                        "Connection to printer lost", "PID_CALIBRATE");

    api_->advanced().start_pid_calibrate(
        "heater_bed", 60, [this](float, float, float) { success_.store(true); },
        [this](const MoonrakerError& err) {
            captured_error_ = err.message;
            error_.store(true);
        });

    settle();
    // The socket dropped: not a failure, and the collector must still be there.
    REQUIRE_FALSE(error_.load());

    mock_client_.dispatch_gcode_response(
        "PID parameters: pid_Kp=73.517 pid_Ki=1.132 pid_Kd=1194.093");
    settle();

    REQUIRE(success_.load());
    REQUIRE_FALSE(error_.load());
}

TEST_CASE_METHOD(TransportLossFixture, "MPC absorbs CONNECTION_LOST and still delivers the result",
                 "[calibration][transport][1543]") {
    mock_client_.force_next_gcode_error(MoonrakerErrorType::CONNECTION_LOST,
                                        "Connection to printer lost", "MPC_CALIBRATE");

    api_->advanced().start_mpc_calibrate(
        "extruder", 200, 0,
        [this](const MoonrakerAdvancedAPI::MPCResult&) { success_.store(true); },
        [this](const MoonrakerError& err) {
            captured_error_ = err.message;
            error_.store(true);
        });

    settle();
    REQUIRE_FALSE(error_.load());

    mock_client_.dispatch_gcode_response("Finished MPC calibration heater=extruder");
    mock_client_.dispatch_gcode_response("block_heat_capacity=18.5432 [J/K]");
    mock_client_.dispatch_gcode_response("sensor_responsiveness=0.123456 [K/s/K]");
    mock_client_.dispatch_gcode_response("ambient_transfer=0.078901 [W/K]");
    settle();

    REQUIRE(success_.load());
    REQUIRE_FALSE(error_.load());
}

TEST_CASE_METHOD(TransportLossFixture,
                 "Screws tilt absorbs CONNECTION_LOST and completes on the busy->idle edge",
                 "[calibration][transport][1543]") {
    get_printer_state().init_subjects(false);
    lv_subject_t* idle = global_idle_subject();
    REQUIRE(idle != nullptr);
    lv_subject_set_int(idle, 1); // the probe run holds the printer busy

    mock_client_.force_next_gcode_error(MoonrakerErrorType::CONNECTION_LOST,
                                        "Connection to printer lost", "SCREWS_TILT_CALCULATE");

    api_->advanced().calculate_screws_tilt(
        [this](const std::vector<ScrewTiltResult>& results) {
            CAPTURE(results.size());
            REQUIRE_FALSE(results.empty());
            success_.store(true);
        },
        [this](const MoonrakerError& err) {
            captured_error_ = err.message;
            error_.store(true);
        });

    settle();
    REQUIRE_FALSE(error_.load());

    // Result lines arriving while the socket was down are what the collector
    // has; the idle edge is the definitive completion signal for this driver.
    mock_client_.dispatch_gcode_response("// front_left (base) : x=-5.0, y=30.0, z=2.48750");
    settle();
    lv_subject_set_int(idle, 0); // macro finished, socket restored
    settle();

    REQUIRE(success_.load());
    REQUIRE_FALSE(error_.load());
}

TEST_CASE_METHOD(TransportLossFixture,
                 "Bed mesh absorbs CONNECTION_LOST and completes from its markers",
                 "[calibration][transport][1543]") {
    mock_client_.force_next_gcode_error(MoonrakerErrorType::CONNECTION_LOST,
                                        "Connection to printer lost", "BED_MESH_CALIBRATE");

    api_->advanced().start_bed_mesh_calibrate(
        IAdvancedAPI::BedMeshCommand{/*script=*/"BED_MESH_CALIBRATE", /*self_prepares=*/true},
        [](int, int) {}, [this]() { success_.store(true); },
        [this](const MoonrakerError& err) {
            captured_error_ = err.message;
            error_.store(true);
        },
        /*expected_probes=*/0, /*probe_samples=*/1);

    settle();
    REQUIRE_FALSE(error_.load());

    mock_client_.dispatch_gcode_response("Mesh Bed Leveling Complete");
    settle();

    REQUIRE(success_.load());
    REQUIRE_FALSE(error_.load());
}

TEST_CASE_METHOD(TransportLossFixture,
                 "Input shaper absorbs CONNECTION_LOST instead of blaming the wiring",
                 "[calibration][transport][1543]") {
    mock_client_.force_next_gcode_error(MoonrakerErrorType::CONNECTION_LOST,
                                        "Connection to printer lost", "SHAPER_CALIBRATE");

    api_->advanced().start_resonance_test(
        'X', [](int, ShaperCalibrationPhase) {},
        [this](const InputShaperResult&) { success_.store(true); },
        [this](const MoonrakerError& err) {
            captured_error_ = err.message;
            error_.store(true);
        });

    settle();
    REQUIRE_FALSE(error_.load());
    REQUIRE(captured_error_.empty());
}

TEST_CASE_METHOD(TransportLossFixture,
                 "Noise check absorbs CONNECTION_LOST instead of failing fast",
                 "[calibration][transport][1543]") {
    mock_client_.force_next_gcode_error(MoonrakerErrorType::CONNECTION_LOST,
                                        "Connection to printer lost", "MEASURE_AXES_NOISE");

    api_->advanced().measure_axes_noise([this](float) { success_.store(true); },
                                        [this](const MoonrakerError& err) {
                                            captured_error_ = err.message;
                                            error_.store(true);
                                        });

    settle();
    REQUIRE_FALSE(error_.load());
    REQUIRE(captured_error_.empty());
}

TEST_CASE_METHOD(TransportLossFixture,
                 "A JSON-RPC error is still terminal after the transport policy",
                 "[calibration][transport][1543]") {
    // Klipper's own complaint must keep failing fast — the absorb path exists
    // for a vanished transport, not for a rejection.
    mock_client_.force_next_gcode_error(MoonrakerErrorType::JSON_RPC_ERROR,
                                        "Heater extruder not configured", "PID_CALIBRATE");

    api_->advanced().start_pid_calibrate(
        "extruder", 200, [this](float, float, float) { success_.store(true); },
        [this](const MoonrakerError& err) {
            captured_error_ = err.message;
            error_.store(true);
        });

    settle();
    REQUIRE(error_.load());
    REQUIRE_FALSE(success_.load());
}

TEST_CASE_METHOD(TransportLossFixture,
                 "Line-driven collectors fail honestly after the edge grace window",
                 "[calibration][transport][1543]") {
    lv_subject_t* idle = global_idle_subject();
    REQUIRE(idle != nullptr);
    lv_subject_set_int(idle, 1); // in-flight macro holds the printer busy

    mock_client_.force_next_gcode_error(MoonrakerErrorType::CONNECTION_LOST,
                                        "Connection to printer lost", "PID_CALIBRATE");

    api_->advanced().start_pid_calibrate(
        "heater_bed", 60, [this](float, float, float) { success_.store(true); },
        [this](const MoonrakerError& err) {
            captured_error_ = err.message;
            error_.store(true);
        });

    settle();
    REQUIRE_FALSE(error_.load());

    // The socket comes back with no result lines behind it: idle edge, and the
    // collector waits out the grace window before concluding the results were
    // lost. process_lvgl() moves virtual time, so the 3s window elapses fast.
    lv_subject_set_int(idle, 0);
    settle();
    REQUIRE_FALSE(error_.load()); // grace still running

    process_lvgl(4000);
    settle();

    REQUIRE(error_.load());
    REQUIRE_FALSE(success_.load());
    REQUIRE(captured_error_.find("result unavailable") != std::string::npos);
}
