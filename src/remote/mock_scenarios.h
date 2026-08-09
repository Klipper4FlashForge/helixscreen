// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/**
 * @file mock_scenarios.h
 * @brief Mock scenario registry for remote control testing
 *
 * Each scenario sets multiple LVGL subjects to simulate a specific printer state.
 * Used via `helixctl scenario <name>` to quickly set up test conditions.
 */

#include <functional>
#include <string>
#include <vector>

namespace helix {

struct MockScenario {
    std::string name;
    std::string description;
    std::function<void()> apply; // Must be called on UI thread
};

/**
 * @brief Get all registered mock scenarios
 */
const std::vector<MockScenario>& get_mock_scenarios();

/**
 * @brief Find a scenario by name
 * @return Pointer to scenario, or nullptr if not found
 */
const MockScenario* find_scenario(const std::string& name);

} // namespace helix
