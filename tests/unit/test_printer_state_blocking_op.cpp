// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_printer_state_blocking_op.cpp
 * @brief Tests for the "blocking non-print operation in progress" signal.
 *
 * PrinterState::is_blocking_operation_active() is the predicate that a later
 * send-boundary guard uses to refuse discretionary g-code (fan/temp/LED/moves)
 * while the printer is executing a blocking non-print op (G28, BED_MESH_CALIBRATE,
 * QGL, PROBE_ACCURACY, manual probe, long macro).
 *
 * Signal =
 *     (idle_timeout.state == "Printing" AND print_job_state NOT IN {PRINTING, PAUSED})
 *     OR manual_probe.is_active
 *
 * The idle_timeout.state == "Printing" flag is Klipper's canonical busy indicator,
 * true for the whole duration of ANY blocking command issued from idle. Excluding
 * real file prints (PRINTING/PAUSED) keeps mid-print fan/temp tweaks working.
 *
 * These tests drive the underlying subjects directly (no mock idle_timeout
 * plumbing) and also exercise the JSON parse path in
 * PrinterCalibrationState::update_from_status via PrinterState::update_from_status.
 */

#include "../../include/printer_state.h"
#include "../lvgl_test_fixture.h"

#include "../catch_amalgamated.hpp"

using namespace helix;

namespace {

class BlockingOpFixture : public LVGLTestFixture {
  public:
    BlockingOpFixture() {
        state.init_subjects(false);
        state.set_klippy_state_sync(KlippyState::READY);
    }

    void set_idle_timeout_printing(int v) {
        lv_subject_set_int(state.get_idle_timeout_printing_subject(), v);
    }

    void set_manual_probe(int v) {
        lv_subject_set_int(state.get_manual_probe_active_subject(), v);
    }

    void set_print_state(PrintJobState s) {
        lv_subject_set_int(state.get_print_state_enum_subject(), static_cast<int>(s));
    }

    PrinterState state;
};

} // namespace

// ============================================================================
// Case 1: predicate truth table
// ============================================================================

TEST_CASE_METHOD(BlockingOpFixture,
                 "is_blocking_operation_active reflects idle_timeout + print state",
                 "[printer_state][blocking_op]") {
    SECTION("idle everything -> not blocking") {
        set_idle_timeout_printing(0);
        set_manual_probe(0);
        set_print_state(PrintJobState::STANDBY);
        CHECK_FALSE(state.is_blocking_operation_active());
    }

    SECTION("idle_timeout Printing while STANDBY -> blocking (homing/leveling)") {
        set_idle_timeout_printing(1);
        set_manual_probe(0);
        set_print_state(PrintJobState::STANDBY);
        CHECK(state.is_blocking_operation_active());
    }

    SECTION("idle_timeout Printing during a real file print -> NOT blocking") {
        set_idle_timeout_printing(1);
        set_manual_probe(0);
        set_print_state(PrintJobState::PRINTING);
        CHECK_FALSE(state.is_blocking_operation_active());
    }

    SECTION("idle_timeout Printing while PAUSED -> NOT blocking") {
        set_idle_timeout_printing(1);
        set_manual_probe(0);
        set_print_state(PrintJobState::PAUSED);
        CHECK_FALSE(state.is_blocking_operation_active());
    }

    SECTION("manual probe active -> blocking regardless of print state") {
        set_manual_probe(1);

        set_idle_timeout_printing(0);
        set_print_state(PrintJobState::PRINTING);
        CHECK(state.is_blocking_operation_active());

        set_print_state(PrintJobState::PAUSED);
        CHECK(state.is_blocking_operation_active());

        set_print_state(PrintJobState::STANDBY);
        CHECK(state.is_blocking_operation_active());
    }
}

// ============================================================================
// Case 1b: is_external_blocking_operation_active attributes self-inflicted busy
// ============================================================================

TEST_CASE_METHOD(BlockingOpFixture,
                 "is_external_blocking_operation_active attributes self-busy",
                 "[printer_state][busy_guard]") {
    // Arrange like the "idle_timeout Printing while STANDBY -> blocking" section:
    // idle_timeout_printing = 1, print job state STANDBY, no manual probe.
    set_idle_timeout_printing(1);
    set_manual_probe(0);
    set_print_state(PrintJobState::STANDBY);

    SECTION("external busy: no app motion -> blocked") {
        CHECK(state.is_blocking_operation_active());
        CHECK(state.is_external_blocking_operation_active());
    }

    SECTION("self busy: app motion in flight -> not blocked") {
        state.app_motion_activity().note_sent();
        CHECK(state.is_blocking_operation_active()); // raw predicate unchanged
        CHECK_FALSE(state.is_external_blocking_operation_active());
        state.app_motion_activity().note_done();
    }

    SECTION("manual probe blocks even during app motion") {
        set_manual_probe(1);
        state.app_motion_activity().note_sent();
        CHECK(state.is_external_blocking_operation_active());
        state.app_motion_activity().note_done();
    }
}

// ============================================================================
// Case 2: idle_timeout.state JSON parse -> idle_timeout_printing_ subject
// ============================================================================

TEST_CASE_METHOD(BlockingOpFixture,
                 "update_from_status parses idle_timeout.state into subject",
                 "[printer_state][blocking_op]") {
    lv_subject_t* subj = state.get_idle_timeout_printing_subject();

    SECTION("state == Printing -> 1") {
        nlohmann::json status = {{"idle_timeout", {{"state", "Printing"}}}};
        state.update_from_status(status);
        CHECK(lv_subject_get_int(subj) == 1);
    }

    SECTION("state == Ready -> 0") {
        // First drive it high, then confirm Ready lowers it (exercise the transition).
        lv_subject_set_int(subj, 1);
        nlohmann::json status = {{"idle_timeout", {{"state", "Ready"}}}};
        state.update_from_status(status);
        CHECK(lv_subject_get_int(subj) == 0);
    }

    SECTION("state == Idle -> 0") {
        lv_subject_set_int(subj, 1);
        nlohmann::json status = {{"idle_timeout", {{"state", "Idle"}}}};
        state.update_from_status(status);
        CHECK(lv_subject_get_int(subj) == 0);
    }
}
