// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_remap_restore_confirmation.cpp
 * @brief PrintStartController::restore_filament_mapping() must not treat a
 *        restore it never delivered as done (#1270).
 *
 * Run with: ./build/bin/helix-tests "[remap-restore]"
 *
 * Background: every native backend's set_tool_mapping() routes into
 * AmsSubscriptionBackend::execute_gcode(), which fires the RPC and returns
 * AmsErrorHelper::success() unconditionally — failures land in an async callback
 * that only logs. The controller branched on that return value, so a command
 * Klipper refused counted as a restore. It then cleared the in-memory snapshot
 * AND deleted pending_remap.json, destroying the record crash recovery replays.
 *
 * A halted Klipper at print end is the normal shape of a cancelled or errored
 * print, which is exactly when restore runs — so this was not a rare path.
 */

#include "ui_print_start_controller.h"
#include "ui_update_queue.h"

#include "../lvgl_test_fixture.h"
#include "../test_helpers/print_start_controller_test_access.h"
#include "ams_backend_afc.h"
#include "ams_state.h"
#include "moonraker_api_mock.h"
#include "moonraker_client_mock.h"
#include "printer_state.h"

#include <memory>
#include <vector>

#include "../catch_amalgamated.hpp"

using namespace helix;
using namespace helix::ui;

namespace {

// Counts the restore commands the controller actually dispatches. Returns
// SUCCESS the way the real backends do (execute_gcode is fire-and-forget), so
// the test pins the CONTROLLER's behavior rather than a mock that reports
// failures the production code would never see.
class CountingAfcBackend : public AmsBackendAfc {
  public:
    CountingAfcBackend() : AmsBackendAfc(nullptr, nullptr) {}

    AmsError set_tool_mapping(int tool_number, int slot_index) override {
        calls.push_back({tool_number, slot_index});
        return AmsErrorHelper::success();
    }

    std::vector<int> get_tool_mapping() const override {
        return current;
    }

    struct Call {
        int tool;
        int slot;
    };
    std::vector<Call> calls;
    std::vector<int> current;
};

// Installs a counting backend at index 0 and removes it on scope exit.
struct ScopedCountingBackend {
    CountingAfcBackend* backend = nullptr;

    explicit ScopedCountingBackend(std::vector<int> current_mapping) {
        auto be = std::make_unique<CountingAfcBackend>();
        be->current = std::move(current_mapping);
        backend = be.get();
        AmsState::instance().set_backend(std::move(be));
    }
    ~ScopedCountingBackend() {
        AmsState::instance().set_backend(nullptr);
    }
};

// Controller + the PrinterState it observes, wired the way the panel does.
struct Harness {
    MoonrakerClientMock client{MoonrakerClientMock::PrinterType::VORON_24};
    PrinterState ps;
    MoonrakerAPIMock api{client, ps};
    PrintStartController controller{ps, &api};

    Harness() {
        ps.init_subjects(false);
    }

    void set_klippy(KlippyState state) {
        // _sync writes the subject without the async hop, but observe_int_sync's
        // handler still lands on the UpdateQueue (observer_factory.h:371), so the
        // deferred-restore observer only runs on a drain. Skipping it makes the
        // retry look like it never fired.
        ps.set_klippy_state_sync(state);
        helix::ui::UpdateQueue::instance().drain();
    }
};

} // namespace

// ============================================================================
// The #1270 failure: Klipper refuses everything, controller declares victory
// ============================================================================

TEST_CASE("remap restore: halted Klipper does not consume the saved mapping",
          "[remap-restore][1270]") {
    LVGLTestFixture fx;
    ScopedCountingBackend be{{1, 2}};
    Harness h;
    h.set_klippy(KlippyState::SHUTDOWN);

    // Print ended with T0->slot 2, T1->slot 1 needing to go back.
    PrintStartControllerTestAccess::seed_saved_mapping(h.controller, {2, 1}, 0);

    PrintStartControllerTestAccess::restore(h.controller);

    // Nothing can be delivered to a halted Klipper, so nothing should be sent...
    CHECK(be.backend->calls.empty());
    // ...and above all the snapshot must survive. Clearing it here is what
    // stranded the printer on the print's mapping with no record of the real one.
    CHECK_FALSE(PrintStartControllerTestAccess::saved_mapping(h.controller).empty());
    CHECK(PrintStartControllerTestAccess::saved_backend_index(h.controller) == 0);
}

TEST_CASE("remap restore: klippy ERROR and STARTUP are also not delivery",
          "[remap-restore][1270]") {
    LVGLTestFixture fx;

    auto state = GENERATE(KlippyState::ERROR, KlippyState::STARTUP);

    ScopedCountingBackend be{{1, 2}};
    Harness h;
    h.set_klippy(state);
    PrintStartControllerTestAccess::seed_saved_mapping(h.controller, {2, 1}, 0);

    PrintStartControllerTestAccess::restore(h.controller);

    CHECK(be.backend->calls.empty());
    CHECK_FALSE(PrintStartControllerTestAccess::saved_mapping(h.controller).empty());
}

// ============================================================================
// The gate must not break the normal path
// ============================================================================

TEST_CASE("remap restore: ready Klipper restores and consumes the snapshot",
          "[remap-restore][1270]") {
    LVGLTestFixture fx;
    ScopedCountingBackend be{{1, 2}}; // firmware currently on the print's mapping
    Harness h;
    h.set_klippy(KlippyState::READY);

    // Saved (pre-print) mapping differs from current in both slots.
    PrintStartControllerTestAccess::seed_saved_mapping(h.controller, {2, 1}, 0);

    PrintStartControllerTestAccess::restore(h.controller);

    REQUIRE(be.backend->calls.size() == 2);
    CHECK(be.backend->calls[0].tool == 0);
    CHECK(be.backend->calls[0].slot == 2);
    CHECK(be.backend->calls[1].tool == 1);
    CHECK(be.backend->calls[1].slot == 1);

    // Delivered to a ready Klipper — snapshot is spent.
    CHECK(PrintStartControllerTestAccess::saved_mapping(h.controller).empty());
    CHECK(PrintStartControllerTestAccess::saved_backend_index(h.controller) == -1);
}

TEST_CASE("remap restore: ready Klipper with nothing to change still clears",
          "[remap-restore][1270]") {
    LVGLTestFixture fx;
    ScopedCountingBackend be{{2, 1}}; // firmware already matches the saved mapping
    Harness h;
    h.set_klippy(KlippyState::READY);

    PrintStartControllerTestAccess::seed_saved_mapping(h.controller, {2, 1}, 0);

    PrintStartControllerTestAccess::restore(h.controller);

    CHECK(be.backend->calls.empty()); // no diff, nothing to send
    CHECK(PrintStartControllerTestAccess::saved_mapping(h.controller).empty());
}

// ============================================================================
// Deferral has to actually resolve, or the gate just leaks the snapshot
// ============================================================================

TEST_CASE("remap restore: deferred restore fires when Klipper becomes ready",
          "[remap-restore][1270]") {
    LVGLTestFixture fx;
    ScopedCountingBackend be{{1, 2}};
    Harness h;
    h.set_klippy(KlippyState::SHUTDOWN);
    PrintStartControllerTestAccess::seed_saved_mapping(h.controller, {2, 1}, 0);

    PrintStartControllerTestAccess::restore(h.controller);
    REQUIRE(be.backend->calls.empty());

    // Klipper comes back (FIRMWARE_RESTART, or the user clears the shutdown).
    // The pending restore must go out on its own — otherwise the snapshot is
    // retained forever and only a full app restart would replay it.
    h.set_klippy(KlippyState::READY);

    CHECK(be.backend->calls.size() == 2);
    CHECK(PrintStartControllerTestAccess::saved_mapping(h.controller).empty());
}
