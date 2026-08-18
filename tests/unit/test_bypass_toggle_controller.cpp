// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_bypass_toggle_controller.cpp
 * @brief Guard matrix for the shared bypass toggle policy.
 *
 * Run with: ./build/bin/helix-tests "[bypass-home]"
 */

#include "ui_bypass_toggle_controller.h"
#include "ui_update_queue.h"

#include "../lvgl_test_fixture.h"
#include "ams_backend_mock.h"
#include "ams_state.h"
#include "ams_types.h"
#include "app_globals.h"
#include "printer_state.h"

#include <memory>

#include "../catch_amalgamated.hpp"

using namespace helix;
using namespace helix::ui;

namespace {

/// PrinterState helper: set print_state_enum the way a status update would.
/// Same accessor the controller reads (singleton via app_globals), so test and
/// code under test can never disagree on which PrinterState holds the state.
void seed_print_state(PrintJobState state) {
    lv_subject_set_int(get_printer_state().get_print_state_enum_subject(), static_cast<int>(state));
}

/// Install a started, zero-delay mock as AmsState's primary backend and hand
/// back the raw pointer — the controller resolves its backend through
/// AmsState::instance().get_backend(), so the mock must live there, not beside
/// the fixture. Same idiom as test_ams_bypass_preflight_wiring.cpp.
class BypassToggleFixture : public LVGLTestFixture {
  public:
    AmsBackendMock* backend = nullptr;
    BypassToggleController controller;

    BypassToggleFixture() {
        // PrinterState's subjects first — AmsState's init_subjects() observers
        // the print_state_enum subject, and the controller reads it raw. Same
        // order as test_abort_manager.cpp.
        get_printer_state().init_subjects(false);
        auto& ams = AmsState::instance();
        ams.init_subjects(false);

        auto owned = std::make_unique<AmsBackendMock>(4);
        backend = owned.get();
        backend->set_operation_delay(0);
        ams.set_backend(std::move(owned));
        REQUIRE(backend->start());
    }

    ~BypassToggleFixture() override {
        controller.cancel_pending();
        // Join any in-flight simulated op BEFORE detaching: the mock's threads
        // must not outlive the backend AmsState owns.
        if (backend) {
            backend->wait_for_operation_thread();
        }
        // Drain while the backend is still installed so queued backend-event
        // syncs do not leak into the next test.
        UpdateQueue::instance().drain();
        AmsState::instance().set_backend(nullptr);
    }
};

} // namespace

TEST_CASE("bypass toggle: refuses while printing", "[ams][bypass-home]") {
    BypassToggleFixture fx;
    REQUIRE_FALSE(fx.backend->is_bypass_active());
    seed_print_state(PrintJobState::PRINTING);
    fx.controller.toggle();
    CHECK_FALSE(fx.backend->is_bypass_active()); // no enable happened
    CHECK_FALSE(fx.controller.pending_enable());

    seed_print_state(PrintJobState::PAUSED);
    fx.controller.toggle();
    CHECK_FALSE(fx.backend->is_bypass_active());
}

TEST_CASE("bypass toggle: standby allows enable/disable", "[ams][bypass-home]") {
    BypassToggleFixture fx;
    seed_print_state(PrintJobState::STANDBY);

    // The mock boots with slot 0 loaded (demo appearance). Unload it first so
    // this case exercises the DIRECT enable/disable path — the unload-first
    // chain is covered by its own tests below.
    REQUIRE(fx.backend->unload_active_filament().result == AmsResult::SUCCESS);
    fx.backend->wait_for_operation_thread();
    UpdateQueue::instance().drain();

    fx.controller.toggle();
    CHECK(fx.backend->is_bypass_active());

    fx.controller.toggle();
    CHECK_FALSE(fx.backend->is_bypass_active());
}

TEST_CASE("bypass toggle chain: unload completes -> enable fires", "[ams][bypass-home]") {
    BypassToggleFixture fx;
    seed_print_state(PrintJobState::STANDBY);

    // Load a slot first so the toggle takes the unload-first path. The load
    // settles on the mock's operation thread — join it, then the system info
    // snapshot inside toggle() sees filament actually loaded.
    REQUIRE(fx.backend->load_filament(0).result == AmsResult::SUCCESS);
    fx.backend->wait_for_operation_thread();
    UpdateQueue::instance().drain();

    fx.controller.toggle();
    CHECK(fx.controller.pending_enable());
    CHECK_FALSE(fx.backend->is_bypass_active());

    // The real unload also settles on the operation thread; enable_bypass()
    // refuses while the mock reports a non-IDLE action, so join before the
    // chain step.
    fx.backend->wait_for_operation_thread();
    UpdateQueue::instance().drain();

    // The chain step: UNLOADING -> IDLE.
    CHECK(fx.controller.on_ams_action_changed(AmsAction::UNLOADING, AmsAction::IDLE));
    CHECK(fx.backend->is_bypass_active());
    CHECK_FALSE(fx.controller.pending_enable());
}

TEST_CASE("bypass toggle chain: unload ERROR disarms (regression)", "[ams][bypass-home]") {
    BypassToggleFixture fx;
    seed_print_state(PrintJobState::STANDBY);

    REQUIRE(fx.backend->load_filament(0).result == AmsResult::SUCCESS);
    fx.backend->wait_for_operation_thread();
    UpdateQueue::instance().drain();

    fx.controller.toggle();
    REQUIRE(fx.controller.pending_enable());
    fx.backend->wait_for_operation_thread();

    CHECK(fx.controller.on_ams_action_changed(AmsAction::UNLOADING, AmsAction::ERROR));
    CHECK_FALSE(fx.controller.pending_enable());
    CHECK_FALSE(fx.backend->is_bypass_active());
}

TEST_CASE("bypass toggle chain: event not ours is ignored", "[ams][bypass-home]") {
    BypassToggleFixture fx;
    seed_print_state(PrintJobState::STANDBY);
    CHECK_FALSE(fx.controller.on_ams_action_changed(AmsAction::IDLE, AmsAction::LOADING));
    CHECK_FALSE(fx.controller.on_ams_action_changed(AmsAction::UNLOADING, AmsAction::IDLE));
}
