// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_preheat_multi_tool.cpp
 * @brief Tests for multi-tool preheat logic in PreheatWidget
 *
 * Verifies that when preheating on a multi-tool printer, the correct set of
 * heaters are targeted based on tool_target (-1 = all, 0..N = specific tool).
 */

#include "preheat_widget.h"
#include "tool_state.h"

#include "../catch_amalgamated.hpp"

using namespace helix;

// ============================================================================
// Helper: build a vector of ToolInfo for testing
// ============================================================================

static std::vector<ToolInfo> make_test_tools(int count) {
    std::vector<ToolInfo> tools;
    for (int i = 0; i < count; ++i) {
        ToolInfo t;
        t.index = i;
        t.name = "T" + std::to_string(i);
        t.extruder_name = (i == 0) ? "extruder" : ("extruder" + std::to_string(i));
        tools.push_back(t);
    }
    return tools;
}

// ============================================================================
// collect_preheat_heaters: all tools
// ============================================================================

TEST_CASE("PreheatWidget: collect_preheat_heaters returns all tool heaters when target is -1",
          "[preheat][panel_widget]") {
    auto tools = make_test_tools(3);

    auto heaters = PreheatWidget::collect_preheat_heaters(tools, -1);

    REQUIRE(heaters.size() == 3);
    REQUIRE(heaters[0] == "extruder");
    REQUIRE(heaters[1] == "extruder1");
    REQUIRE(heaters[2] == "extruder2");
}

TEST_CASE("PreheatWidget: collect_preheat_heaters returns single tool heater for specific target",
          "[preheat][panel_widget]") {
    auto tools = make_test_tools(3);

    SECTION("tool 0") {
        auto heaters = PreheatWidget::collect_preheat_heaters(tools, 0);
        REQUIRE(heaters.size() == 1);
        REQUIRE(heaters[0] == "extruder");
    }

    SECTION("tool 1") {
        auto heaters = PreheatWidget::collect_preheat_heaters(tools, 1);
        REQUIRE(heaters.size() == 1);
        REQUIRE(heaters[0] == "extruder1");
    }

    SECTION("tool 2") {
        auto heaters = PreheatWidget::collect_preheat_heaters(tools, 2);
        REQUIRE(heaters.size() == 1);
        REQUIRE(heaters[0] == "extruder2");
    }
}

// ============================================================================
// collect_preheat_heaters: skips tools with no valid heater
// ============================================================================

TEST_CASE("PreheatWidget: collect_preheat_heaters skips tools with no heater",
          "[preheat][panel_widget]") {
    auto tools = make_test_tools(3);
    // Tool 1 has neither extruder_name nor heater_name
    tools[1].extruder_name = std::nullopt;
    tools[1].heater_name = std::nullopt;

    auto heaters = PreheatWidget::collect_preheat_heaters(tools, -1);

    REQUIRE(heaters.size() == 2);
    REQUIRE(heaters[0] == "extruder");
    REQUIRE(heaters[1] == "extruder2");
}

// ============================================================================
// collect_preheat_heaters: prefers heater_name over extruder_name
// ============================================================================

TEST_CASE("PreheatWidget: collect_preheat_heaters uses effective_heater (heater_name priority)",
          "[preheat][panel_widget]") {
    auto tools = make_test_tools(2);
    tools[0].heater_name = "heater_generic nozzle0";

    auto heaters = PreheatWidget::collect_preheat_heaters(tools, -1);

    REQUIRE(heaters.size() == 2);
    REQUIRE(heaters[0] == "heater_generic nozzle0");
    REQUIRE(heaters[1] == "extruder1");
}

// ============================================================================
// collect_preheat_heaters: out-of-range target returns empty
// ============================================================================

TEST_CASE("PreheatWidget: collect_preheat_heaters returns empty for out-of-range target",
          "[preheat][panel_widget]") {
    auto tools = make_test_tools(3);

    SECTION("target beyond tool count") {
        auto heaters = PreheatWidget::collect_preheat_heaters(tools, 5);
        REQUIRE(heaters.empty());
    }

    SECTION("negative target other than -1") {
        auto heaters = PreheatWidget::collect_preheat_heaters(tools, -2);
        REQUIRE(heaters.empty());
    }
}

// ============================================================================
// collect_preheat_heaters: empty tools
// ============================================================================

TEST_CASE("PreheatWidget: collect_preheat_heaters handles empty tools vector",
          "[preheat][panel_widget]") {
    std::vector<ToolInfo> tools;

    auto heaters = PreheatWidget::collect_preheat_heaters(tools, -1);
    REQUIRE(heaters.empty());
}

// ============================================================================
// collect_preheat_heaters: 6-tool printer (Voron Stealth Changer)
// ============================================================================

TEST_CASE("PreheatWidget: collect_preheat_heaters with 6 tools returns all 6 heaters",
          "[preheat][panel_widget]") {
    auto tools = make_test_tools(6);

    auto heaters = PreheatWidget::collect_preheat_heaters(tools, -1);

    REQUIRE(heaters.size() == 6);
    REQUIRE(heaters[0] == "extruder");
    for (int i = 1; i < 6; ++i) {
        REQUIRE(heaters[i] == "extruder" + std::to_string(i));
    }
}

// ============================================================================
// cycle_tool_target logic
// ============================================================================

TEST_CASE("PreheatWidget: cycle_tool_target cycles through all/specific/back to all",
          "[preheat][panel_widget]") {
    // -1 (all) -> 0 -> 1 -> 2 -> -1 (all)
    int target = -1;
    int tool_count = 3;

    // Simulate cycle logic: -1 -> 0
    target = (target == -1) ? 0 : (target + 1 >= tool_count ? -1 : target + 1);
    REQUIRE(target == 0);

    // 0 -> 1
    target = (target == -1) ? 0 : (target + 1 >= tool_count ? -1 : target + 1);
    REQUIRE(target == 1);

    // 1 -> 2
    target = (target == -1) ? 0 : (target + 1 >= tool_count ? -1 : target + 1);
    REQUIRE(target == 2);

    // 2 -> -1 (wrap)
    target = (target == -1) ? 0 : (target + 1 >= tool_count ? -1 : target + 1);
    REQUIRE(target == -1);
}

// ============================================================================
// collect_preheat_heaters: AMS lanes share a hotend
//
// set_ams_topology() expands ToolState's list to one entry per filament lane,
// and every lane on a single-hotend printer resolves to the same heater. The
// "all tools" preheat must send one target per heater, not one per lane. The
// count it returns is also what the confirmation toast reports.
// ============================================================================

TEST_CASE("PreheatWidget: collect_preheat_heaters collapses lanes sharing one heater",
          "[preheat][panel_widget]") {
    // What set_ams_topology() leaves behind on a 4-lane AMS + one extruder:
    // four tools, every one of them mapped to "extruder".
    std::vector<ToolInfo> tools;
    for (int i = 0; i < 4; ++i) {
        ToolInfo t;
        t.index = i;
        t.name = "T" + std::to_string(i);
        t.extruder_name = "extruder";
        tools.push_back(t);
    }

    auto heaters = PreheatWidget::collect_preheat_heaters(tools, -1);

    REQUIRE(heaters.size() == 1);
    REQUIRE(heaters[0] == "extruder");
}

TEST_CASE("PreheatWidget: collect_preheat_heaters keeps every distinct heater in tool order",
          "[preheat][panel_widget]") {
    // Paired with the case above: a collapse that kept only the first heater
    // would pass that one and silently stop heating a toolchanger's other
    // hotends. Two extra lanes are hung off T0's heater to prove the dedup
    // removes duplicates rather than truncating the list.
    auto tools = make_test_tools(3);
    ToolInfo lane;
    lane.index = 3;
    lane.name = "T3";
    lane.extruder_name = "extruder";
    tools.push_back(lane);
    lane.index = 4;
    lane.name = "T4";
    lane.extruder_name = "extruder2";
    tools.push_back(lane);

    auto heaters = PreheatWidget::collect_preheat_heaters(tools, -1);

    REQUIRE(heaters.size() == 3);
    REQUIRE(heaters[0] == "extruder");
    REQUIRE(heaters[1] == "extruder1");
    REQUIRE(heaters[2] == "extruder2");
}
