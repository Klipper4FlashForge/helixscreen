// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_temperature_utils.h"

#include "../test_fixtures.h"
#include "theme_manager.h"

#include <string>

#include "../catch_amalgamated.hpp"

using namespace helix::ui::temperature;

// ============================================================================
// heater_display() - Off state
// ============================================================================

TEST_CASE("heater_display: off state when target is 0", "[temperature][heater_display]") {
    auto result = heater_display(250, 0); // 25°C, off
    REQUIRE(result.temp == "25°C");
    REQUIRE(result.status == "Off");
    REQUIRE(result.pct == 0);
}

TEST_CASE("heater_display: off state when target is negative", "[temperature][heater_display]") {
    auto result = heater_display(250, -10); // 25°C, negative target
    REQUIRE(result.status == "Off");
    REQUIRE(result.pct == 0);
}

// ============================================================================
// heater_display() - Heating state
// ============================================================================

TEST_CASE("heater_display: heating state", "[temperature][heater_display]") {
    // 150°C current, 200°C target -> 75%
    auto result = heater_display(1500, 2000);
    REQUIRE(result.temp == "150 / 200°C");
    REQUIRE(result.status == "Heating...");
    REQUIRE(result.pct == 75);
}

TEST_CASE("heater_display: heating from zero", "[temperature][heater_display]") {
    auto result = heater_display(0, 2000);
    REQUIRE(result.temp == "0 / 200°C");
    REQUIRE(result.pct == 0);
    REQUIRE(result.status == "Heating...");
}

// ============================================================================
// heater_display() - Ready state (within tolerance)
// ============================================================================

TEST_CASE("heater_display: ready state within tolerance", "[temperature][heater_display]") {
    // 198°C with 200°C target -> within +/-2 -> Ready
    auto result = heater_display(1980, 2000);
    REQUIRE(result.temp == "198 / 200°C");
    REQUIRE(result.status == "Ready");
    REQUIRE(result.pct == 99);
}

TEST_CASE("heater_display: ready state at exact target", "[temperature][heater_display]") {
    auto result = heater_display(2000, 2000);
    REQUIRE(result.status == "Ready");
    REQUIRE(result.pct == 100);
}

// ============================================================================
// heater_display() - Cooling state
// ============================================================================

TEST_CASE("heater_display: cooling state above tolerance", "[temperature][heater_display]") {
    // 210°C with 200°C target -> 210 > 202 -> Cooling
    auto result = heater_display(2100, 2000);
    REQUIRE(result.status == "Cooling");
    REQUIRE(result.pct == 100);
}

// ============================================================================
// heater_display() - Tolerance boundaries
// ============================================================================

TEST_CASE("heater_display: exactly at lower tolerance boundary is Ready",
          "[temperature][heater_display]") {
    // 198°C with 200°C target -> 198 >= 200-2 -> Ready
    auto result = heater_display(1980, 2000);
    REQUIRE(result.status == "Ready");
}

TEST_CASE("heater_display: exactly at upper tolerance boundary is Ready",
          "[temperature][heater_display]") {
    // 202°C with 200°C target -> 202 <= 200+2 -> Ready
    auto result = heater_display(2020, 2000);
    REQUIRE(result.status == "Ready");
}

TEST_CASE("heater_display: just below lower tolerance boundary is Heating",
          "[temperature][heater_display]") {
    // 197°C with 200°C target -> 197 < 198 -> Heating
    auto result = heater_display(1970, 2000);
    REQUIRE(result.status == "Heating...");
}

TEST_CASE("heater_display: just above upper tolerance boundary is Cooling",
          "[temperature][heater_display]") {
    // 203°C with 200°C target -> 203 > 202 -> Cooling
    auto result = heater_display(2030, 2000);
    REQUIRE(result.status == "Cooling");
}

// ============================================================================
// heater_display() - Percentage clamping
// ============================================================================

TEST_CASE("heater_display: percentage clamps to 100 when over target",
          "[temperature][heater_display]") {
    auto result = heater_display(2500, 2000);
    REQUIRE(result.pct == 100);
}

TEST_CASE("heater_display: percentage clamps to 0 for negative temps",
          "[temperature][heater_display]") {
    auto result = heater_display(-10, 2000);
    REQUIRE(result.pct == 0);
}

// ============================================================================
// heater_display() - Color field matches get_heating_state_color()
// ============================================================================

TEST_CASE("heater_display: color matches get_heating_state_color for off state",
          "[temperature][heater_display]") {
    auto result = heater_display(250, 0);
    auto expected_color = get_heating_state_color(25, 0);
    REQUIRE(result.color.red == expected_color.red);
    REQUIRE(result.color.green == expected_color.green);
    REQUIRE(result.color.blue == expected_color.blue);
}

TEST_CASE("heater_display: color matches get_heating_state_color for heating state",
          "[temperature][heater_display]") {
    auto result = heater_display(1500, 2000);
    auto expected_color = get_heating_state_color(150, 200);
    REQUIRE(result.color.red == expected_color.red);
    REQUIRE(result.color.green == expected_color.green);
    REQUIRE(result.color.blue == expected_color.blue);
}

TEST_CASE("heater_display: color matches get_heating_state_color for ready state",
          "[temperature][heater_display]") {
    auto result = heater_display(1990, 2000);
    auto expected_color = get_heating_state_color(199, 200);
    REQUIRE(result.color.red == expected_color.red);
    REQUIRE(result.color.green == expected_color.green);
    REQUIRE(result.color.blue == expected_color.blue);
}

TEST_CASE("heater_display: color matches get_heating_state_color for cooling state",
          "[temperature][heater_display]") {
    auto result = heater_display(2100, 2000);
    auto expected_color = get_heating_state_color(210, 200);
    REQUIRE(result.color.red == expected_color.red);
    REQUIRE(result.color.green == expected_color.green);
    REQUIRE(result.color.blue == expected_color.blue);
}

// ============================================================================
// get_heating_state_variant() - icon variant matches the 4-state color logic
// (drives the bed-icon tint in the nozzle-temps widget; must agree with the
// temp-label color so icon and label never contradict each other)
// ============================================================================

TEST_CASE("get_heating_state_variant: off when target is zero", "[temperature][heater_variant]") {
    REQUIRE(std::string(get_heating_state_variant(25, 0)) == "muted");
}

TEST_CASE("get_heating_state_variant: off when target is negative",
          "[temperature][heater_variant]") {
    REQUIRE(std::string(get_heating_state_variant(25, -5)) == "muted");
}

TEST_CASE("get_heating_state_variant: heating below tolerance", "[temperature][heater_variant]") {
    REQUIRE(std::string(get_heating_state_variant(150, 200)) == "danger");
}

TEST_CASE("get_heating_state_variant: at-temp within tolerance", "[temperature][heater_variant]") {
    REQUIRE(std::string(get_heating_state_variant(199, 200)) == "success");
    REQUIRE(std::string(get_heating_state_variant(200, 200)) == "success");
    REQUIRE(std::string(get_heating_state_variant(201, 200)) == "success");
}

TEST_CASE("get_heating_state_variant: cooling above tolerance", "[temperature][heater_variant]") {
    REQUIRE(std::string(get_heating_state_variant(210, 200)) == "info");
}

TEST_CASE("get_heating_state_variant: tolerance boundaries are at-temp",
          "[temperature][heater_variant]") {
    // DEFAULT_AT_TEMP_TOLERANCE = 2: target-2 and target+2 are still "at temp"
    REQUIRE(std::string(get_heating_state_variant(198, 200)) == "success"); // lower bound
    REQUIRE(std::string(get_heating_state_variant(202, 200)) == "success"); // upper bound
    REQUIRE(std::string(get_heating_state_variant(197, 200)) == "danger");  // just below -> heating
    REQUIRE(std::string(get_heating_state_variant(203, 200)) == "info");    // just above -> cooling
}

TEST_CASE("get_heating_state_variant: agrees with get_heating_state_color across states",
          "[temperature][heater_variant]") {
    // The variant string must map to the same theme token the color function
    // resolves, for every state. (Color is token-resolved; variant is the token
    // family name used by ui_icon_set_variant.)
    auto same = [](int cur, int tgt, const char* token) {
        auto color = get_heating_state_color(cur, tgt);
        auto expected = theme_manager_get_color(token);
        return color.red == expected.red && color.green == expected.green &&
               color.blue == expected.blue;
    };
    // off -> muted maps to text_muted; the other three share variant==token name
    REQUIRE(std::string(get_heating_state_variant(25, 0)) == "muted");
    REQUIRE(same(25, 0, "text_muted"));
    REQUIRE(std::string(get_heating_state_variant(150, 200)) == "danger");
    REQUIRE(same(150, 200, "danger"));
    REQUIRE(std::string(get_heating_state_variant(199, 200)) == "success");
    REQUIRE(same(199, 200, "success"));
    REQUIRE(std::string(get_heating_state_variant(210, 200)) == "info");
    REQUIRE(same(210, 200, "info"));
}

// ============================================================================
// classify_heat_state() - the single source of truth all three consumers share
// ============================================================================

TEST_CASE("classify_heat_state: off when target is zero", "[temperature][heat_state]") {
    REQUIRE(classify_heat_state(25, 0) == HeatState::Off);
}

TEST_CASE("classify_heat_state: off when target is negative", "[temperature][heat_state]") {
    // Previously get_heating_state_color() treated a negative target as "cooling"
    // (blue) while get_heating_state_variant() treated it as off (muted).
    REQUIRE(classify_heat_state(25, -5) == HeatState::Off);
}

TEST_CASE("classify_heat_state: heating below tolerance", "[temperature][heat_state]") {
    REQUIRE(classify_heat_state(150, 200) == HeatState::Heating);
}

TEST_CASE("classify_heat_state: cooling above tolerance", "[temperature][heat_state]") {
    REQUIRE(classify_heat_state(210, 200) == HeatState::Cooling);
}

TEST_CASE("classify_heat_state: tolerance boundaries are at-temp", "[temperature][heat_state]") {
    REQUIRE(classify_heat_state(198, 200) == HeatState::AtTemp);
    REQUIRE(classify_heat_state(200, 200) == HeatState::AtTemp);
    REQUIRE(classify_heat_state(202, 200) == HeatState::AtTemp);
    REQUIRE(classify_heat_state(197, 200) == HeatState::Heating);
    REQUIRE(classify_heat_state(203, 200) == HeatState::Cooling);
}

TEST_CASE("classify_heat_state: honors a custom tolerance", "[temperature][heat_state]") {
    // Decidegree callers pass tolerance=20 for the same 2 degrees.
    REQUIRE(classify_heat_state(1980, 2000, 20) == HeatState::AtTemp);
    REQUIRE(classify_heat_state(1970, 2000, 20) == HeatState::Heating);
    REQUIRE(classify_heat_state(2030, 2000, 20) == HeatState::Cooling);
}

TEST_CASE("get_heating_state_color: negative target is off, not cooling",
          "[temperature][heat_state]") {
    // Regression: the color function used `target == 0`, so a negative target fell
    // through to the cooling branch and rendered blue.
    auto color = get_heating_state_color(25, -5);
    auto expected = theme_manager_get_color("text_muted");
    REQUIRE(color.red == expected.red);
    REQUIRE(color.green == expected.green);
    REQUIRE(color.blue == expected.blue);
}

TEST_CASE("classify_heat_state: color and variant both agree with it",
          "[temperature][heat_state]") {
    struct Case {
        int current;
        int target;
        HeatState state;
        const char* variant;
        const char* token;
    };
    const Case cases[] = {
        {25, 0, HeatState::Off, "muted", "text_muted"},
        {150, 200, HeatState::Heating, "danger", "danger"},
        {199, 200, HeatState::AtTemp, "success", "success"},
        {210, 200, HeatState::Cooling, "info", "info"},
    };
    for (const auto& c : cases) {
        REQUIRE(classify_heat_state(c.current, c.target) == c.state);
        REQUIRE(std::string(get_heating_state_variant(c.current, c.target)) == c.variant);
        auto color = get_heating_state_color(c.current, c.target);
        auto expected = theme_manager_get_color(c.token);
        REQUIRE(color.red == expected.red);
        REQUIRE(color.green == expected.green);
        REQUIRE(color.blue == expected.blue);
    }
}

// ============================================================================
// classify_heat_state_with_mode() — chamber-only mode-aware classifier.
//
// Off/Heating must be an exact passthrough to classify_heat_state() (target is
// a genuine heat goal there). Maintaining is the whole point of this function:
// target is a cooling CEILING, so Heating must never come out of it — only
// Cooling (above ceiling) or Neutral (at/below ceiling, including "current is
// nowhere near the ceiling", which the old bug rendered as heating-red).
// ============================================================================

TEST_CASE("classify_heat_state_with_mode: Off/Heating are an exact passthrough",
          "[temperature][heat_state][chamber_mode]") {
    // Off: target<=0 either way.
    REQUIRE(classify_heat_state_with_mode(25, 0, helix::ChamberMode::Off) == HeatState::Off);
    REQUIRE(classify_heat_state(25, 0) == HeatState::Off);

    // Heating: target is a real heat goal, full 4-state logic applies.
    struct Case {
        int current;
        int target;
    };
    const Case cases[] = {{150, 200}, {199, 200}, {200, 200}, {210, 200}};
    for (const auto& c : cases) {
        REQUIRE(classify_heat_state_with_mode(c.current, c.target, helix::ChamberMode::Heating) ==
                classify_heat_state(c.current, c.target));
    }
}

TEST_CASE("classify_heat_state_with_mode: Maintaining never returns Heating",
          "[temperature][heat_state][chamber_mode]") {
    // Regression for the bug this function exists to fix: a chamber cold and
    // far below its Maintaining ceiling must NOT classify as Heating (which
    // rendered heating-red and pulsed, contradicting the neutral-colored
    // label). 0 current, 200 ceiling — as far below as it gets.
    REQUIRE(classify_heat_state_with_mode(0, 200, helix::ChamberMode::Maintaining) ==
            HeatState::Neutral);
    REQUIRE(classify_heat_state_with_mode(150, 200, helix::ChamberMode::Maintaining) ==
            HeatState::Neutral);
}

TEST_CASE("classify_heat_state_with_mode: Maintaining at/below ceiling is Neutral, "
          "above is Cooling",
          "[temperature][heat_state][chamber_mode]") {
    // Below ceiling.
    REQUIRE(classify_heat_state_with_mode(150, 200, helix::ChamberMode::Maintaining) ==
            HeatState::Neutral);
    // Exactly at ceiling.
    REQUIRE(classify_heat_state_with_mode(200, 200, helix::ChamberMode::Maintaining) ==
            HeatState::Neutral);
    // At the tolerance boundary — still Neutral (matches AtTemp's inclusive bound).
    REQUIRE(classify_heat_state_with_mode(202, 200, helix::ChamberMode::Maintaining) ==
            HeatState::Neutral);
    // Just past the tolerance boundary — Cooling.
    REQUIRE(classify_heat_state_with_mode(203, 200, helix::ChamberMode::Maintaining) ==
            HeatState::Cooling);
    // Well above ceiling.
    REQUIRE(classify_heat_state_with_mode(300, 200, helix::ChamberMode::Maintaining) ==
            HeatState::Cooling);
}

TEST_CASE("classify_heat_state_with_mode: honors a custom tolerance in Maintaining mode",
          "[temperature][heat_state][chamber_mode]") {
    // Decidegree-scale tolerance (20 = 2 degrees), mirroring HeatingIconAnimator's
    // TEMP_TOLERANCE usage.
    REQUIRE(classify_heat_state_with_mode(2015, 2000, helix::ChamberMode::Maintaining, 20) ==
            HeatState::Neutral);
    REQUIRE(classify_heat_state_with_mode(2025, 2000, helix::ChamberMode::Maintaining, 20) ==
            HeatState::Cooling);
}

// Needs XMLTestFixture: theme_manager_get_color() resolves tokens through
// lv_xml consts registered by theme_manager_init(). With no theme loaded every
// token — "text" and "text_muted" alike — falls through to black, which makes
// the Neutral-vs-Off distinction unassertable (and every other color comparison
// in this file vacuously true).
TEST_CASE_METHOD(XMLTestFixture,
                 "get_heating_state_color(HeatState): Neutral resolves to the text token",
                 "[temperature][heat_state][chamber_mode]") {
    auto color = get_heating_state_color(HeatState::Neutral);
    auto expected = theme_manager_get_color("text");
    REQUIRE(color.red == expected.red);
    REQUIRE(color.green == expected.green);
    REQUIRE(color.blue == expected.blue);

    // Distinct from Off's text_muted — Neutral is NOT the same token as Off,
    // which is exactly the design subtlety this function exists to preserve.
    auto off_color = get_heating_state_color(HeatState::Off);
    auto muted = theme_manager_get_color("text_muted");
    REQUIRE(off_color.red == muted.red);
    REQUIRE(off_color.green == muted.green);
    REQUIRE(off_color.blue == muted.blue);
    bool colors_match = (color.red == off_color.red) && (color.green == off_color.green) &&
                        (color.blue == off_color.blue);
    REQUIRE_FALSE(colors_match);
}

TEST_CASE("get_heating_state_color(HeatState) overload agrees with the int overload for every "
          "mode-unaware state",
          "[temperature][heat_state][chamber_mode]") {
    // get_heating_state_color(int,int,int) is defined as
    // get_heating_state_color(classify_heat_state(...)) — pin that equivalence
    // so the two overloads can never drift apart.
    struct Case {
        int current;
        int target;
    };
    const Case cases[] = {{25, 0}, {150, 200}, {199, 200}, {210, 200}};
    for (const auto& c : cases) {
        auto state = classify_heat_state(c.current, c.target);
        auto via_state = get_heating_state_color(state);
        auto via_ints = get_heating_state_color(c.current, c.target);
        REQUIRE(via_state.red == via_ints.red);
        REQUIRE(via_state.green == via_ints.green);
        REQUIRE(via_state.blue == via_ints.blue);
    }
}
