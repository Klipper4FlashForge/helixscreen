// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_controls_quick_actions.cpp
 * @brief The guard contract behind the ControlsPanel quick-action buttons.
 *
 * Homing, Quad Gantry Level and Z-Tilt all run through one guarded body, so the
 * rules that body enforces are the rules for every one of the seven buttons: a
 * second tap while an operation is live is refused, a tap with no printer
 * connection says so instead of doing nothing, and the guard is released on both
 * terminal outcomes rather than only on success.
 *
 * The dispatch is supplied by the test, so these assertions need no printer —
 * what is under test is the bookkeeping around the command, not the gcode.
 */

#include "ui_panel_controls.h"
#include "ui_update_queue.h"

#include "../test_fixtures.h"
#include "../test_helpers/controls_panel_test_access.h"
#include "../test_helpers/update_queue_test_access.h"

#include <string>
#include <utility>

#include "../catch_amalgamated.hpp"

using helix::ui::ControlsPanelTestAccess;
using helix::ui::UpdateQueueTestAccess;

namespace {

/// A QuickActionText whose fields are distinguishable but otherwise uninteresting.
ControlsPanelTestAccess::QuickActionText probe_text() {
    return {"started", "completed", "guard timed out", "rpc timed out", "failed: {}"};
}

void drain() {
    UpdateQueueTestAccess::drain_all(helix::ui::UpdateQueue::instance());
}

} // namespace

TEST_CASE_METHOD(XMLTestFixture, "Quick action refuses to dispatch with no API",
                 "[controls][quick_action]") {
    ControlsPanel panel(state(), nullptr);

    bool dispatched = false;
    ControlsPanelTestAccess::run_quick_action(
        panel, 1000, probe_text(),
        [&dispatched](IMoonrakerAPI::SuccessCallback, IMoonrakerAPI::ErrorCallback) {
            dispatched = true;
        });

    REQUIRE_FALSE(dispatched);
    // The guard must stay clear, or the button is dead until the timeout fires.
    REQUIRE_FALSE(ControlsPanelTestAccess::guard_active(panel));
}

TEST_CASE_METHOD(XMLTestFixture, "Quick action refuses a second dispatch while one is live",
                 "[controls][quick_action]") {
    ControlsPanel panel(state(), &api());

    int dispatches = 0;
    // Hold the first tap's callbacks instead of completing them, so the guard
    // stays armed across the second tap.
    IMoonrakerAPI::SuccessCallback held_ok;
    ControlsPanelTestAccess::run_quick_action(
        panel, 1000, probe_text(),
        [&](IMoonrakerAPI::SuccessCallback ok, IMoonrakerAPI::ErrorCallback) {
            ++dispatches;
            held_ok = std::move(ok);
        });
    REQUIRE(dispatches == 1);
    REQUIRE(ControlsPanelTestAccess::guard_active(panel));

    ControlsPanelTestAccess::run_quick_action(
        panel, 1000, probe_text(),
        [&](IMoonrakerAPI::SuccessCallback, IMoonrakerAPI::ErrorCallback) { ++dispatches; });

    REQUIRE(dispatches == 1);
}

TEST_CASE_METHOD(XMLTestFixture, "Quick action releases the guard on success",
                 "[controls][quick_action]") {
    ControlsPanel panel(state(), &api());

    ControlsPanelTestAccess::run_quick_action(
        panel, 1000, probe_text(),
        [](IMoonrakerAPI::SuccessCallback ok, IMoonrakerAPI::ErrorCallback) { ok(); });
    drain();

    REQUIRE_FALSE(ControlsPanelTestAccess::guard_active(panel));
}

TEST_CASE_METHOD(XMLTestFixture, "Quick action releases the guard on failure",
                 "[controls][quick_action]") {
    ControlsPanel panel(state(), &api());

    ControlsPanelTestAccess::run_quick_action(
        panel, 1000, probe_text(),
        [](IMoonrakerAPI::SuccessCallback, IMoonrakerAPI::ErrorCallback err) {
            err(MoonrakerError::validation_error("test", "boom"));
        });
    drain();

    REQUIRE_FALSE(ControlsPanelTestAccess::guard_active(panel));
}

TEST_CASE("Homing quick actions carry a message for every outcome", "[controls][quick_action]") {
    // A quick action with an empty `completed` leaves the user watching a button
    // that never reports finishing. Holding all five strings in one struct is
    // what makes that omission visible, so assert none of them is blank.
    const auto text = ControlsPanelTestAccess::homing_text("Homing X...");

    REQUIRE(text.started == std::string("Homing X..."));
    REQUIRE_FALSE(text.completed.empty());
    REQUIRE_FALSE(text.guard_timed_out.empty());
    REQUIRE_FALSE(text.rpc_timed_out.empty());
    REQUIRE(text.failed_fmt.find("{}") != std::string::npos);
}
