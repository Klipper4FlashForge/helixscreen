// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_override_rebind.cpp
 * @brief External re-bind clears our override; eject honors the retention
 * setting (#1281). Firmware truth must win back a lane another writer re-bound.
 *
 * merge_override()'s rule matrix (test_filament_slot_override_store.cpp) pins
 * the pure function. This file pins the WIRING: AmsBackendAfc's live status
 * path must consult it, drop the in-memory record on an external re-bind, and
 * gate the eject clear on the keep-spool-info setting.
 */

#include "../lvgl_test_fixture.h"
#include "ams_backend_afc.h"
#include "ams_types.h"
#include "settings_manager.h"

#include <string>

#include "../catch_amalgamated.hpp"

using namespace helix;

class AfcRebindHelper : public AmsBackendAfc {
  public:
    AfcRebindHelper() : AmsBackendAfc(nullptr, nullptr) {
        std::vector<std::string> names{"lane1", "lane2"};
        initialize_slots(names);
    }

    void set_override(int slot_index, const helix::ams::FilamentSlotOverride& o) {
        std::lock_guard<std::mutex> lock(mutex_);
        overrides_[slot_index] = o;
    }

    /// Drive the live status path, so assertions see what the UI would paint.
    void feed_stepper(const std::string& lane, const nlohmann::json& data) {
        nlohmann::json params;
        params["AFC_stepper " + lane] = data;
        nlohmann::json notification;
        notification["params"] = nlohmann::json::array({params, 0.0});
        handle_status_update(notification);
    }

    [[nodiscard]] int visible_spool_id(int slot_index) const {
        return get_slot_info(slot_index).spoolman_id;
    }

    [[nodiscard]] std::string visible_brand(int slot_index) const {
        return get_slot_info(slot_index).brand;
    }

    [[nodiscard]] bool has_override(int slot_index) const {
        std::lock_guard<std::mutex> lock(mutex_);
        return overrides_.count(slot_index) > 0;
    }
};

namespace {

helix::ams::FilamentSlotOverride spool_override(int spoolman_id) {
    helix::ams::FilamentSlotOverride o;
    o.spoolman_id = spoolman_id;
    o.brand = "Polymaker";
    o.material = "PLA";
    return o;
}

} // namespace

TEST_CASE_METHOD(LVGLTestFixture, "AFC external re-bind clears our override (#1281 step 7)",
                 "[ams][afc][override-merge]") {
    // Post-change apply_overrides() reads the retention setting; give the
    // settings singleton the production-default world before any merge runs.
    SettingsManager::instance().init_subjects();

    AfcRebindHelper afc;
    afc.set_override(0, spool_override(42));
    // Firmware (via Mainsail/AFC macro) now reports a DIFFERENT spool:
    afc.feed_stepper("lane1", nlohmann::json{{"spool_id", 169}});
    CHECK(afc.visible_spool_id(0) == 169); // firmware truth paints
    CHECK(afc.visible_brand(0).empty());   // our stale brand no longer shadows
    CHECK_FALSE(afc.has_override(0));      // record dropped
}

TEST_CASE_METHOD(LVGLTestFixture, "AFC eject retains by default, clears with setting off (#1281)",
                 "[ams][afc][override-merge]") {
    auto& settings = SettingsManager::instance();
    settings.init_subjects();

    settings.set_ams_keep_spool_info_on_eject(true);
    AfcRebindHelper afc;
    afc.set_override(0, spool_override(42));
    afc.feed_stepper("lane1", nlohmann::json{{"spool_id", 42}}); // firmware echoes our id
    CHECK(afc.visible_spool_id(0) == 42);
    afc.feed_stepper("lane1", nlohmann::json{{"spool_id", nullptr}}); // eject: spool_id=None
    CHECK(afc.visible_spool_id(0) == 42);                             // designed retention
    CHECK(afc.has_override(0));

    settings.set_ams_keep_spool_info_on_eject(false);
    afc.set_override(1, spool_override(7));
    afc.feed_stepper("lane2", nlohmann::json{{"spool_id", 7}});
    afc.feed_stepper("lane2", nlohmann::json{{"spool_id", nullptr}});
    CHECK(afc.visible_spool_id(1) == 0); // start fresh
    CHECK_FALSE(afc.has_override(1));
    settings.set_ams_keep_spool_info_on_eject(true);
}
