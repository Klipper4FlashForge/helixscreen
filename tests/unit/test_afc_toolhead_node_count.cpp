// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_afc_toolhead_node_count.cpp
 * @brief Toolhead node count on a multi-unit AFC toolchanger (#1229 defect 1).
 *
 * The reporter's overview drew SEVEN toolhead nodes for a machine with SIX
 * extruders. `AFC.extruders` is the authority for how many toolheads exist — the
 * reporter confirmed that directly — and it lists six here.
 *
 * compute_system_tool_layout() derives the count per unit from topology rather
 * than from the extruder list, which happens to agree on this machine:
 *
 *   HTLF_1  2 direct lanes + 1 hub group = 3   (extruder, extruder1, extruder2)
 *   Tools   2 standalone lanes           = 2   (extruder4, extruder5)
 *   AMS_1   4 lanes, all hub-routed      = 1   (extruder3)
 *                                          --
 *                                           6
 *
 * The seventh node appears only if AMS_1 is mis-typed as MIXED, which is exactly
 * what counting an unknown lane routing as `direct` did before f6a7c8c55. This
 * test pins the count against the real capture so the agreement is enforced
 * rather than coincidental.
 */

#include "ams_backend_afc.h"
#include "ams_types.h"
#include "ui/ams_drawing_utils.h"

#include <spdlog/spdlog.h>

#include <fstream>
#include <set>
#include <string>
#include <vector>

#include "../catch_amalgamated.hpp"

using namespace helix;

namespace {

std::string fixture_dir() {
    std::string src = __FILE__;
    auto pos = src.rfind("/tests/unit/");
    if (pos != std::string::npos) {
        return src.substr(0, pos) + "/tests/fixtures/";
    }
    return "tests/fixtures/";
}

nlohmann::json load_fixture(const std::string& name) {
    const std::string path = fixture_dir() + name;
    std::ifstream f(path);
    INFO("fixture missing or unreadable: " << path);
    REQUIRE(f.is_open());
    nlohmann::json j;
    f >> j;
    return j;
}

class AfcLayoutHelper : public AmsBackendAfc {
  public:
    AfcLayoutHelper() : AmsBackendAfc(nullptr, nullptr) {}

    void discover(const nlohmann::json& object_list) {
        std::vector<std::string> lanes;
        std::vector<std::string> hubs;
        for (const auto& entry : object_list) {
            const std::string name = entry.get<std::string>();
            if (name.rfind("AFC_lane ", 0) == 0) {
                lanes.push_back(name.substr(9));
            } else if (name.rfind("AFC_hub ", 0) == 0) {
                hubs.push_back(name.substr(8));
            }
        }
        set_discovered_lanes(lanes, hubs);
    }

    void feed(const nlohmann::json& params_inner) {
        nlohmann::json notification;
        notification["params"] = nlohmann::json::array({params_inner, 0.0});
        handle_status_update(notification);
    }

    /// The first frame creates the slots, so per-lane data parsed during it
    /// lands before they exist. Feed until it has settled.
    void feed_until_settled(const nlohmann::json& params, int frames = 3) {
        for (int i = 0; i < frames; ++i) {
            feed(params);
        }
    }
};

/// Distinct extruders named by the capture's AFC_lane objects — the authority
/// for how many toolheads the machine has.
size_t distinct_extruders(const nlohmann::json& status) {
    std::set<std::string> names;
    for (auto& item : status.items()) {
        if (item.key().rfind("AFC_lane ", 0) == 0 && item.value().contains("extruder") &&
            item.value()["extruder"].is_string()) {
            names.insert(item.value()["extruder"].get<std::string>());
        }
    }
    return names.size();
}

} // namespace

TEST_CASE("AFC toolchanger: one toolhead node per extruder, not per lane group",
          "[ams][afc][1229][toolchanger][topology]") {
    auto fixture = load_fixture("afc_toolchanger_multiunit.json");

    // Ground truth, derived from the capture rather than hardcoded.
    const size_t extruder_count = distinct_extruders(fixture["status"]);
    REQUIRE(extruder_count == 6);
    REQUIRE(fixture["status"]["AFC"]["extruders"].size() == 6);

    AfcLayoutHelper afc;
    afc.discover(fixture["object_list"]);
    afc.feed_until_settled(fixture["status"]);

    const AmsSystemInfo info = afc.get_system_info();
    const auto layout = ams_draw::compute_system_tool_layout(info, &afc);

    spdlog::warn("=== #1229 toolhead layout ===");
    spdlog::warn("  total_physical_tools={} (extruders={})", layout.total_physical_tools,
                 extruder_count);
    for (size_t i = 0; i < info.units.size() && i < layout.units.size(); ++i) {
        spdlog::warn("  unit '{}' topology={} tool_count={} first_physical={}", info.units[i].name,
                     path_topology_to_string(afc.get_unit_topology(static_cast<int>(i))),
                     layout.units[i].tool_count, layout.units[i].first_physical_tool);
    }

    INFO("drew " << layout.total_physical_tools << " toolhead nodes for " << extruder_count
                 << " extruders");
    CHECK(layout.total_physical_tools == static_cast<int>(extruder_count));
}

TEST_CASE("AFC toolchanger: the shared-extruder unit contributes one node",
          "[ams][afc][1229][toolchanger][topology]") {
    auto fixture = load_fixture("afc_toolchanger_multiunit.json");

    AfcLayoutHelper afc;
    afc.discover(fixture["object_list"]);
    afc.feed_until_settled(fixture["status"]);

    const AmsSystemInfo info = afc.get_system_info();
    const auto layout = ams_draw::compute_system_tool_layout(info, &afc);

    int ams_index = -1;
    for (size_t i = 0; i < info.units.size(); ++i) {
        if (info.units[i].name.find("AMS_1") != std::string::npos) {
            ams_index = static_cast<int>(i);
        }
    }
    REQUIRE(ams_index >= 0);
    REQUIRE(ams_index < static_cast<int>(layout.units.size()));

    // All four AMS_1 lanes feed extruder3 through one hub. Four lanes, one
    // toolhead — the case that produced a second, phantom node.
    INFO("AMS_1's four hub-routed lanes did not collapse to a single toolhead");
    CHECK(layout.units[ams_index].tool_count == 1);
}
