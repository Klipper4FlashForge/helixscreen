// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_queue_update_lifetime.cpp
 * @brief Queued main-thread callbacks must not outlive the object they mutate
 *
 * A `queue_update` lambda holding a raw `this` runs against freed memory if the
 * owner dies before the drain — and if the body touches a member `lv_subject_t`,
 * `lv_subject_notify` then walks a freed observer list. The SIGSEGV lands on
 * whichever unrelated test drained next, which is what made #1146 expensive to
 * diagnose. The repair (#1165) is a generation guard on the producing side.
 *
 * These cases pin the guard's observable contract: once the owner has torn down
 * its subjects (or been destroyed), a callback still sitting in the queue is
 * dropped rather than applied.
 */

#include "ui_spool_wizard.h"
#include "ui_update_queue.h"

#include "../lvgl_test_fixture.h"
#include "app_globals.h"
#include "moonraker_api_mock.h"
#include "moonraker_client_mock.h"
#include "printer_print_state.h"
#include "printer_state.h"

#include "../catch_amalgamated.hpp"

using namespace helix;
using namespace helix::ui;

// ============================================================================
// PrinterPrintState — setters defer their subject writes
// ============================================================================

TEST_CASE_METHOD(LVGLTestFixture,
                 "PrinterPrintState drops queued setter callbacks once subjects are deinited",
                 "[print_state][lifetime][queue_update]") {
    UpdateQueue::instance().drain();

    PrinterPrintState print_state;
    print_state.init_subjects(false);

    // Queued, not yet applied.
    print_state.set_print_layer_total(42);
    REQUIRE(UpdateQueue::instance().pending_count() > 0);
    REQUIRE(lv_subject_get_int(print_state.get_print_layer_total_subject()) == 0);

    // Tear the subjects down and stand them back up, exactly as the test
    // isolation listener and reconnect paths do. The in-flight callback now
    // refers to a subject that has been deinited underneath it.
    print_state.deinit_subjects();
    print_state.init_subjects(false);

    UpdateQueue::instance().drain();

    // Without the generation guard the drain writes 42 into the reborn subject;
    // with it, the stale callback is skipped.
    CHECK(lv_subject_get_int(print_state.get_print_layer_total_subject()) == 0);
    CHECK(UpdateQueue::instance().pending_count() == 0);
}

TEST_CASE_METHOD(LVGLTestFixture,
                 "PrinterPrintState queued callbacks survive destruction of the owner",
                 "[print_state][lifetime][queue_update]") {
    UpdateQueue::instance().drain();

    {
        PrinterPrintState print_state;
        print_state.init_subjects(false);

        // Every deferring setter, so a regression on any one of them is caught.
        print_state.set_print_layer_total(9);
        print_state.set_print_layer_heights(0.2, 0.3);
        print_state.set_print_layer_current(3);
        print_state.set_print_start_state(PrintStartPhase::HEATING_BED, "heating", 50);
        print_state.reset_print_start_state();
        print_state.set_print_in_progress(true);
        print_state.set_estimated_print_time(600);

        REQUIRE(UpdateQueue::instance().pending_count() > 0);

        print_state.deinit_subjects();
    } // destroyed with callbacks still queued

    // Pre-fix this drains seven lambdas into freed member subjects. The
    // assertion below only proves the queue emptied; the use-after-free itself
    // is what an ASAN build of this case reports.
    UpdateQueue::instance().drain();
    CHECK(UpdateQueue::instance().pending_count() == 0);
}

// ============================================================================
// SpoolWizardOverlay — Spoolman responses land after the overlay is dismissed
// ============================================================================

namespace {

/// Installs a mock API as the process-wide one for the duration of the scope,
/// restoring whatever was there before. SpoolWizardOverlay reaches the API
/// through get_moonraker_api() rather than an injected pointer.
class ScopedGlobalApi {
  public:
    explicit ScopedGlobalApi(MoonrakerAPI* api) : previous_(get_moonraker_api()) {
        set_moonraker_api(api);
    }
    ~ScopedGlobalApi() {
        set_moonraker_api(previous_);
    }
    ScopedGlobalApi(const ScopedGlobalApi&) = delete;
    ScopedGlobalApi& operator=(const ScopedGlobalApi&) = delete;

  private:
    MoonrakerAPI* previous_;
};

} // namespace

TEST_CASE_METHOD(LVGLTestFixture, "SpoolWizard vendor load is dropped when the overlay closes",
                 "[spool_wizard][lifetime][queue_update]") {
    PrinterState state;
    MoonrakerClientMock client;
    MoonrakerAPIMock api(client, state);
    ScopedGlobalApi scoped_api(&api);

    UpdateQueue::instance().drain();

    SpoolWizardOverlay wizard;
    wizard.on_activate();

    // The mock answers both vendor fetches synchronously, so the merge/apply
    // step is queued by the time load_vendors() returns.
    wizard.load_vendors();
    REQUIRE(UpdateQueue::instance().pending_count() > 0);
    REQUIRE(wizard.all_vendors().empty());

    // User backs out of the wizard before the response is applied.
    wizard.on_deactivate();

    UpdateQueue::instance().drain();

    // Pre-fix the deferred body still ran and repopulated all_vendors_ against
    // an overlay that is no longer live.
    CHECK(wizard.all_vendors().empty());
    CHECK(UpdateQueue::instance().pending_count() == 0);
}

TEST_CASE_METHOD(LVGLTestFixture, "SpoolWizard vendor load applies while the overlay is live",
                 "[spool_wizard][lifetime][queue_update]") {
    PrinterState state;
    MoonrakerClientMock client;
    MoonrakerAPIMock api(client, state);
    ScopedGlobalApi scoped_api(&api);

    UpdateQueue::instance().drain();

    SpoolWizardOverlay wizard;
    wizard.on_activate();

    wizard.load_vendors();
    UpdateQueue::instance().drain();

    // Counterpart to the case above: the guard must not swallow the callback
    // when the overlay is still active, or the drop test would pass vacuously.
    CHECK_FALSE(wizard.all_vendors().empty());
}
