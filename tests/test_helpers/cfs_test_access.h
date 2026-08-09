// tests/test_helpers/cfs_test_access.h
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ams_backend_cfs.h"
#include "ams_error.h"
#include "ams_types.h"
#include "filament_slot_override.h"
#include "filament_slot_override_store.h"

#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>

#include "hv/json.hpp"

// Friend-class shim for AmsBackendCfs -- declared as friend in the backend
// header (`friend class ::CfsTestAccess;`). Gives tests narrow accessors for
// private override state and the homing-dispatch surface without going
// through the public get_slot_info path (which layers apply_overrides on top
// and obscures what the internal maps actually hold), and without widening
// any member's access level in production code just to let a test reach it.
class CfsTestAccess {
  public:
    static void handle_status(helix::printer::AmsBackendCfs& b, const nlohmann::json& n) {
        b.handle_status_update(n);
    }
    static void seed_override(helix::printer::AmsBackendCfs& b, int slot_index,
                              const helix::ams::FilamentSlotOverride& ovr) {
        std::lock_guard<std::mutex> lock(b.mutex_);
        b.overrides_[slot_index] = ovr;
    }
    static std::optional<helix::ams::FilamentSlotOverride>
    get_override(const helix::printer::AmsBackendCfs& b, int slot_index) {
        std::lock_guard<std::mutex> lock(b.mutex_);
        auto it = b.overrides_.find(slot_index);
        if (it == b.overrides_.end())
            return std::nullopt;
        return it->second;
    }
    static void inject_override_store(helix::printer::AmsBackendCfs& b,
                                      std::unique_ptr<helix::ams::FilamentSlotOverrideStore> s) {
        b.override_store_ = std::move(s);
    }
    static std::optional<std::string> last_rfid_uid(const helix::printer::AmsBackendCfs& b,
                                                    int slot_index) {
        std::lock_guard<std::mutex> lock(b.mutex_);
        return b.rfid_tracker_.baseline(slot_index);
    }
    static helix::printer::CfsMacroVariant macro_variant(const helix::printer::AmsBackendCfs& b) {
        return b.macro_variant_;
    }
    // Seed the nozzle-loaded signal + preloaded-slot index used by change_tool's
    // WITH/WITHOUT-material selection (#968 Phase 2). filament_loaded reflects
    // filament physically at the nozzle; current_slot can be a *preloaded*
    // (cassette) slot with the nozzle still empty on K1 CFS.
    static void set_loaded_state(helix::printer::AmsBackendCfs& b, bool filament_loaded,
                                 int current_slot) {
        std::lock_guard<std::mutex> lock(b.mutex_);
        b.system_info_.filament_loaded = filament_loaded;
        b.system_info_.current_slot = current_slot;
    }
    static void set_last_rfid_uid(helix::printer::AmsBackendCfs& b, int slot_index,
                                  const std::string& uid) {
        std::lock_guard<std::mutex> lock(b.mutex_);
        b.rfid_tracker_.observe(slot_index, uid);
    }
    static void set_macro_variant_k1(helix::printer::AmsBackendCfs& b) {
        b.macro_variant_ = helix::printer::CfsMacroVariant::K1;
    }
    static void set_macro_variant_fork(helix::printer::AmsBackendCfs& b) {
        b.macro_variant_ = helix::printer::CfsMacroVariant::Fork;
    }
    // Seed N connected CFS units (unit_index 0..N-1) so device-action code that
    // iterates system_info_.units (e.g. refresh_rfid → BOX_INFO_REFRESH) has
    // addressable units without a live Moonraker parse.
    static void set_connected_units(helix::printer::AmsBackendCfs& b, int count) {
        std::lock_guard<std::mutex> lock(b.mutex_);
        b.system_info_.units.clear();
        for (int u = 0; u < count; ++u) {
            AmsUnit unit;
            unit.unit_index = u;
            unit.connected = true;
            unit.slot_count = 4;
            unit.first_slot_global_index = u * 4;
            b.system_info_.units.push_back(std::move(unit));
        }
    }

    /// Call the REAL dispatch_action_script implementation directly. The
    /// caller does not subclass AmsBackendCfs to reach this -- the virtual
    /// stays private, and this friend shim is the one sanctioned way in.
    static AmsError call_dispatch_action_script(helix::printer::AmsBackendCfs& b,
                                                std::string gcode) {
        return b.dispatch_action_script(std::move(gcode));
    }
};
