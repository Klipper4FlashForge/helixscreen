// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_heating_animator.h"
#include "ui_temperature_utils.h"

#include "theme_manager.h"

#include <type_traits>

#include "../catch_amalgamated.hpp"

using helix::ui::temperature::HeatState;

// The animator's state must be the same function of (current, target) that the
// temperature label uses — otherwise the icon and the number contradict each
// other. Values here are DECIDEGREES; the animator's tolerance is 20 (= 2 deg).

TEST_CASE("HeatingIconAnimator::State is the shared HeatState", "[animator][heat_state]") {
    STATIC_REQUIRE(std::is_same_v<HeatingIconAnimator::State, HeatState>);
}

TEST_CASE("HeatingIconAnimator: unattached update does not change state",
          "[animator][heat_state]") {
    HeatingIconAnimator animator;
    REQUIRE_FALSE(animator.is_attached());
    animator.update(1500, 2000);
    // update() early-returns when icon_ is null; state stays at its initial value.
    REQUIRE(animator.get_state() == HeatState::Off);
}

TEST_CASE("HeatingIconAnimator: classifies cooling above target", "[animator][heat_state]") {
    // The pre-change state machine had no cooling branch: current far above target
    // matched `current >= target - TEMP_TOLERANCE` and reported AT_TARGET.
    REQUIRE(helix::ui::temperature::classify_heat_state(2500, 2000, 20) == HeatState::Cooling);
}

TEST_CASE("HeatingIconAnimator: decidegree tolerance matches the label's degrees",
          "[animator][heat_state]") {
    using helix::ui::temperature::classify_heat_state;
    // 199.9 deg vs 200.0 deg target -> at-temp in both units
    REQUIRE(classify_heat_state(1999, 2000, 20) == HeatState::AtTemp);
    REQUIRE(classify_heat_state(199, 200, 2) == HeatState::AtTemp);
    // 197.0 vs 200.0 -> heating in both
    REQUIRE(classify_heat_state(1970, 2000, 20) == HeatState::Heating);
    REQUIRE(classify_heat_state(197, 200, 2) == HeatState::Heating);
}
