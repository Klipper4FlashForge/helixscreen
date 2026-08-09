// tests/unit/test_print_control_view.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include "print_control_view.h"

#include <cstring>
#include <string>

#include "../catch_amalgamated.hpp"

using helix::PrintJobState;
using helix::ui::compute_control_button_view;
using helix::ui::PendingAction;

TEST_CASE("control view: printing shows enabled Pause", "[print_control_view]") {
    auto v = compute_control_button_view(PrintJobState::PRINTING, PendingAction::None,
                                         /*pause*/ true, /*resume*/ true, /*cancel*/ true);
    REQUIRE(std::string(v.primary_label) == "Pause");
    REQUIRE(v.primary_enabled);
    REQUIRE(v.stop_enabled);
}

TEST_CASE("control view: paused shows enabled Resume (play icon)", "[print_control_view]") {
    auto v =
        compute_control_button_view(PrintJobState::PAUSED, PendingAction::None, true, true, true);
    REQUIRE(std::string(v.primary_label) == "Resume");
    REQUIRE(std::string(v.primary_icon) == "\xF3\xB0\x90\x8A"); // play
    REQUIRE(v.primary_enabled);
}

TEST_CASE("control view: idle disables both buttons", "[print_control_view]") {
    for (auto s : {PrintJobState::STANDBY, PrintJobState::COMPLETE, PrintJobState::CANCELLED,
                   PrintJobState::ERROR}) {
        auto v = compute_control_button_view(s, PendingAction::None, true, true, true);
        REQUIRE_FALSE(v.primary_enabled);
        REQUIRE_FALSE(v.stop_enabled);
    }
}

TEST_CASE("control view: pending Pausing -> hourglass, disabled, transitional label",
          "[print_control_view]") {
    auto v = compute_control_button_view(PrintJobState::PRINTING, PendingAction::Pausing, true,
                                         true, true);
    REQUIRE(std::string(v.primary_icon) == "\xF3\xB0\x94\x9F"); // hourglass
    REQUIRE(std::string(v.primary_label) == "Pausing...");
    REQUIRE_FALSE(v.primary_enabled);
    REQUIRE(v.stop_enabled);
}

TEST_CASE("control view: pending Resuming -> hourglass + Resuming label", "[print_control_view]") {
    auto v = compute_control_button_view(PrintJobState::PAUSED, PendingAction::Resuming, true, true,
                                         true);
    REQUIRE(std::string(v.primary_icon) == "\xF3\xB0\x94\x9F");
    REQUIRE(std::string(v.primary_label) == "Resuming...");
    REQUIRE_FALSE(v.primary_enabled);
}

// The user-visible consequence of a stuck pending action, stated as one
// before/after pair: while Resuming is pending the paused printer's primary
// button is BOTH mislabelled and un-tappable, and clearing the pending action is
// the entire difference. On the reporter's AD5X that window lasted 150s because
// nothing linked Klipper's `!!` rejection back to the pending state — they could
// not retry even after clearing the runout (bundle JX2FVRB9). See
// PrintControlButtons::notify_printer_error(), which performs this transition.
TEST_CASE("control view: clearing a pending Resume makes the button tappable again",
          "[print_control_view]") {
    auto stuck = compute_control_button_view(PrintJobState::PAUSED, PendingAction::Resuming,
                                             /*pause*/ true, /*resume*/ true, /*cancel*/ true);
    REQUIRE(std::string(stuck.primary_label) == "Resuming...");
    REQUIRE_FALSE(stuck.primary_enabled);

    auto released = compute_control_button_view(PrintJobState::PAUSED, PendingAction::None,
                                                /*pause*/ true, /*resume*/ true, /*cancel*/ true);
    REQUIRE(std::string(released.primary_label) == "Resume");
    REQUIRE(released.primary_enabled);
}

TEST_CASE("control view: missing macro slot disables primary", "[print_control_view]") {
    auto v = compute_control_button_view(PrintJobState::PRINTING, PendingAction::None,
                                         /*pause*/ false, /*resume*/ true, /*cancel*/ true);
    REQUIRE_FALSE(v.primary_enabled);
    auto v2 = compute_control_button_view(PrintJobState::PAUSED, PendingAction::None,
                                          /*pause*/ true, /*resume*/ false, /*cancel*/ true);
    REQUIRE_FALSE(v2.primary_enabled);
    auto v3 = compute_control_button_view(PrintJobState::PRINTING, PendingAction::None, true, true,
                                          /*cancel*/ false);
    REQUIRE_FALSE(v3.stop_enabled);
}
