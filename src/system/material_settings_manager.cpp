// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "material_settings_manager.h"

#include "config.h"

#include <spdlog/spdlog.h>

namespace helix {

MaterialSettingsManager& MaterialSettingsManager::instance() {
    static MaterialSettingsManager s_instance;
    return s_instance;
}

void MaterialSettingsManager::init() {
    if (initialized_) {
        return;
    }
    load_from_config();
    load_presets_from_config();
    initialized_ = true;
    spdlog::info("[MaterialSettingsManager] Initialized with {} override(s)", overrides_.size());
}

const filament::MaterialOverride*
MaterialSettingsManager::get_override(const std::string& name) const {
    auto it = overrides_.find(name);
    if (it != overrides_.end()) {
        return &it->second;
    }
    return nullptr;
}

void MaterialSettingsManager::set_override(const std::string& name,
                                           const filament::MaterialOverride& override) {
    overrides_[name] = override;
    save_to_config();
    spdlog::info("[MaterialSettingsManager] Set override for '{}'", name);
}

void MaterialSettingsManager::clear_override(const std::string& name) {
    if (overrides_.erase(name) > 0) {
        save_to_config();
        spdlog::info("[MaterialSettingsManager] Cleared override for '{}'", name);
    }
}

bool MaterialSettingsManager::has_override(const std::string& name) const {
    return overrides_.count(name) > 0;
}

void MaterialSettingsManager::load_from_config() {
    Config* config = Config::get_instance();
    if (!config || !config->exists("/material_overrides")) {
        return;
    }

    try {
        auto& overrides_json = config->get_json("/material_overrides");
        if (!overrides_json.is_object()) {
            return;
        }

        for (auto& [name, values] : overrides_json.items()) {
            filament::MaterialOverride ovr;
            if (values.contains("nozzle_min") && values["nozzle_min"].is_number_integer()) {
                ovr.nozzle_min = values["nozzle_min"].get<int>();
            }
            if (values.contains("nozzle_max") && values["nozzle_max"].is_number_integer()) {
                ovr.nozzle_max = values["nozzle_max"].get<int>();
            }
            if (values.contains("bed_temp") && values["bed_temp"].is_number_integer()) {
                ovr.bed_temp = values["bed_temp"].get<int>();
            }
            if (values.contains("preheat_macro") && values["preheat_macro"].is_string()) {
                ovr.preheat_macro = values["preheat_macro"].get<std::string>();
            }
            if (values.contains("macro_handles_heating") &&
                values["macro_handles_heating"].is_boolean()) {
                ovr.macro_handles_heating = values["macro_handles_heating"].get<bool>();
            }
            overrides_[name] = ovr;
        }
    } catch (const std::exception& e) {
        spdlog::warn("[MaterialSettingsManager] Failed to load overrides: {}", e.what());
    }
}

void MaterialSettingsManager::save_to_config() {
    Config* config = Config::get_instance();
    if (!config) {
        return;
    }

    // Build JSON object for all overrides
    nlohmann::json overrides_json = nlohmann::json::object();
    for (const auto& [name, ovr] : overrides_) {
        nlohmann::json entry = nlohmann::json::object();
        if (ovr.nozzle_min)
            entry["nozzle_min"] = *ovr.nozzle_min;
        if (ovr.nozzle_max)
            entry["nozzle_max"] = *ovr.nozzle_max;
        if (ovr.bed_temp)
            entry["bed_temp"] = *ovr.bed_temp;
        if (ovr.preheat_macro)
            entry["preheat_macro"] = *ovr.preheat_macro;
        if (ovr.macro_handles_heating)
            entry["macro_handles_heating"] = *ovr.macro_handles_heating;
        overrides_json[name] = entry;
    }

    config->get_json("/material_overrides") = overrides_json;
    config->save();
}

void MaterialSettingsManager::load_presets_from_config() {
    // Start from defaults; only override slots the stored config validly provides.
    for (int i = 0; i < 4; ++i) {
        preset_materials_[i] = DEFAULT_PRESET_MATERIALS[i];
    }

    Config* config = Config::get_instance();
    if (!config || !config->exists("/preset_materials")) {
        return;
    }
    try {
        auto& arr = config->get_json("/preset_materials");
        if (!arr.is_array() || arr.size() != 4) {
            return; // malformed → keep defaults
        }
        for (int i = 0; i < 4; ++i) {
            if (arr[i].is_string()) {
                std::string v = arr[i].get<std::string>();
                if (!v.empty()) {
                    preset_materials_[i] = v;
                }
            }
        }
    } catch (const std::exception& e) {
        spdlog::warn("[MaterialSettingsManager] Failed to load presets: {}", e.what());
        for (int i = 0; i < 4; ++i) {
            preset_materials_[i] = DEFAULT_PRESET_MATERIALS[i];
        }
    }
}

void MaterialSettingsManager::save_presets_to_config() {
    Config* config = Config::get_instance();
    if (!config) {
        return;
    }
    nlohmann::json arr = nlohmann::json::array();
    for (int i = 0; i < 4; ++i) {
        arr.push_back(preset_materials_[i]);
    }
    config->get_json("/preset_materials") = arr;
    config->save();
}

void MaterialSettingsManager::set_preset_material(int index, const std::string& material) {
    if (index < 0 || index >= 4 || material.empty()) {
        return;
    }
    preset_materials_[index] = material;
    save_presets_to_config();
    spdlog::info("[MaterialSettingsManager] Preset slot {} set to {}", index, material);
}

void MaterialSettingsManager::reset_preset_materials() {
    for (int i = 0; i < 4; ++i) {
        preset_materials_[i] = DEFAULT_PRESET_MATERIALS[i];
    }
    save_presets_to_config();
    spdlog::info("[MaterialSettingsManager] Presets reset to defaults");
}

} // namespace helix

// ============================================================================
// Bridge function for filament_database.h
// ============================================================================

namespace filament {

const MaterialOverride* get_material_override(std::string_view name) {
    auto& mgr = helix::MaterialSettingsManager::instance();
    return mgr.get_override(std::string(name));
}

} // namespace filament
