// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_afc_toolchanger_from_status.cpp
 * @brief Toolchanger discovery from the payload the status subscription really sends.
 *
 * `num_extruders_` and `extruders_` were populated ONLY from `AFC.system`, and
 * AFC does not emit that object on the status subscription. Verified against
 * the add-on source on a live BoxTurtle: `AFC.py` `get_status()` publishes
 * current_load / current_lane / next_lane / current_state / spoolman /
 * error_state / units / lanes and no `system` key. The only writers of
 * `str["system"]['num_extruders']` are `_webhooks_status()` — the
 * `/printer/afc/status` HTTP endpoint, which we never call — and `save_vars()`,
 * which writes the vars file.
 *
 * So on real hardware `num_extruders_` stayed at its default 1 and `extruders_`
 * stayed empty, which made every toolchanger branch unreachable:
 * `load_filament()` and `select_tool()` both gate on `num_extruders_ > 1`, so
 * `AFC_SELECT_TOOL` was never dispatched on an actual AFC toolchanger.
 *
 * The existing multi-extruder tests all feed a `system` object, so they passed
 * while exercising a shape production never receives — the same failure mode as
 * the string `spool_id` fixtures in #1154. These tests use the flat top-level
 * `AFC.extruders` array that the firmware DOES send.
 *
 * `AFC.system` parsing is deliberately KEPT: it is real on the webhook surface
 * and carries strictly more (per-extruder lane_loaded and tool_stn distances),
 * so it stays authoritative whenever it is present.
 */

#include "ams_backend_afc.h"
#include "ams_types.h"

#include <string>
#include <vector>

#include "../catch_amalgamated.hpp"

using namespace helix;

class AfcToolchangerStatusHelper : public AmsBackendAfc {
  public:
    AfcToolchangerStatusHelper() : AmsBackendAfc(nullptr, nullptr) {
        std::vector<std::string> names{"lane1", "lane2"};
        initialize_slots(names);
        running_ = true;
    }

    /// Feed AFC's global object exactly as the status subscription delivers it.
    void feed_afc(const nlohmann::json& afc) {
        nlohmann::json params;
        params["AFC"] = afc;
        nlohmann::json notification;
        notification["params"] = nlohmann::json::array({params, 0.0});
        handle_status_update(notification);
    }

    [[nodiscard]] int extruder_count() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return num_extruders_;
    }

    [[nodiscard]] std::vector<std::string> extruder_infos() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<std::string> out;
        out.reserve(extruders_.size());
        for (const auto& e : extruders_) {
            out.push_back(e.name);
        }
        return out;
    }
};

namespace {

/// What get_status() actually publishes — note the absence of "system".
nlohmann::json status_payload(const std::vector<std::string>& extruders) {
    return nlohmann::json{{"current_state", "Idle"},
                          {"lanes", nlohmann::json::array({"lane1", "lane2"})},
                          {"extruders", extruders},
                          {"hubs", nlohmann::json::array({"Turtle_1"})}};
}

} // namespace

TEST_CASE("AFC discovers toolchanger extruders from the status payload", "[ams][afc][toolchanger]") {
    SECTION("a single extruder is not a toolchanger") {
        AfcToolchangerStatusHelper afc;
        afc.feed_afc(status_payload({"extruder"}));
        CHECK(afc.extruder_count() == 1);
        CHECK(afc.extruder_infos() == std::vector<std::string>{"extruder"});
    }

    SECTION("two extruders make a toolchanger, with no AFC.system present") {
        // This is the case that was broken: the firmware names both extruders in
        // the flat array, but nothing derived num_extruders_ from it, so every
        // toolchanger branch stayed unreachable.
        AfcToolchangerStatusHelper afc;
        afc.feed_afc(status_payload({"extruder", "extruder1"}));
        CHECK(afc.extruder_count() == 2);
        CHECK(afc.extruder_infos() == std::vector<std::string>{"extruder", "extruder1"});
    }

    SECTION("positional order is preserved — extruders_ is indexed as a tool number") {
        AfcToolchangerStatusHelper afc;
        afc.feed_afc(status_payload({"extruder", "extruder1", "extruder2"}));
        REQUIRE(afc.extruder_count() == 3);
        const auto names = afc.extruder_infos();
        REQUIRE(names.size() == 3);
        CHECK(names[0] == "extruder");
        CHECK(names[1] == "extruder1");
        CHECK(names[2] == "extruder2");
    }

    SECTION("an absent extruders key leaves the previous discovery alone") {
        AfcToolchangerStatusHelper afc;
        afc.feed_afc(status_payload({"extruder", "extruder1"}));
        REQUIRE(afc.extruder_count() == 2);

        // Deltas: a frame that omits `extruders` must not reset us to 1.
        afc.feed_afc(nlohmann::json{{"current_state", "Loading"}});
        CHECK(afc.extruder_count() == 2);
        CHECK(afc.extruder_infos().size() == 2);
    }

    SECTION("an empty extruders array does not claim a toolchanger") {
        AfcToolchangerStatusHelper afc;
        afc.feed_afc(status_payload({}));
        CHECK(afc.extruder_count() == 1);
    }
}

TEST_CASE("AFC.system still wins when present — it carries strictly more",
          "[ams][afc][toolchanger]") {
    // The webhook surface supplies per-extruder lane_loaded and tool_stn
    // distances that the flat name array cannot. Deriving from names must not
    // clobber a richer system-sourced record.
    AfcToolchangerStatusHelper afc;

    nlohmann::json payload = status_payload({"extruder", "extruder1"});
    payload["system"] = nlohmann::json{
        {"num_extruders", 2},
        {"extruders",
         nlohmann::json{{"extruder", nlohmann::json{{"lane_loaded", "lane1"}, {"tool_stn", 42.0}}},
                        {"extruder1", nlohmann::json{{"lane_loaded", ""}, {"tool_stn", 55.0}}}}}};

    afc.feed_afc(payload);
    CHECK(afc.extruder_count() == 2);
    CHECK(afc.extruder_infos() == std::vector<std::string>{"extruder", "extruder1"});
}
