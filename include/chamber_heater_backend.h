// include/chamber_heater_backend.h
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "hv/json.hpp"

namespace helix::chamber {

/// Generic chamber-heater diagnostics — the ONLY shape subjects/UI ever see.
/// Vendor JSON schemas are translated to this at the backend border.
struct ChamberHeaterDiagnostics {
    bool fault = false;
    bool inhibited = false;
    /// Another controller (device web UI, physical button) is currently driving
    /// the heater, not us. Display-only annotation in v1.
    bool externally_controlled = false;
    std::string fault_reason;      ///< empty when none
    double element_temp_c = NAN;   ///< heating-element temp; NAN = unknown
    int filter_fan_percent = -1;   ///< -1 = unknown
    std::string filter_fan_reason; ///< empty when none
};

/// One chamber-heater style/brand behind an interface (AMS-backend pattern).
/// Adding a brand = new subclass file + one registry() line. Vendor names live
/// ONLY in chamber_heater_backend_*.cpp.
class ChamberHeaterBackend {
  public:
    virtual ~ChamberHeaterBackend() = default;
    /// Stable id ("generic", "dragonbreath", "panda_breath") — logged, never UI-visible.
    virtual std::string_view id() const = 0;
    /// 0 = not mine. Heuristic keyword score for generic; 95 for appliance names.
    virtual int discovery_confidence(const std::string& object_name) const = 0;
    /// Status object carrying diagnostics ("" = this backend has none).
    virtual std::string_view diagnostics_object() const = 0;
    /// Binary filtration-fan output_pin ("" = none).
    virtual std::string_view filter_fan_pin() const = 0;
    /// Chamber cooling fan object for integrated chambers ("" = none).
    virtual std::string cooling_fan_object(const std::string& discovered_name) const {
        (void)discovered_name;
        return {};
    }
    /// Gcode clearing a latched fault ("" = none).
    virtual std::string_view fault_reset_gcode() const = 0;
    /// Conservative °C ceiling when configfile max_temp is unknown. 0 = no clamp.
    virtual double conservative_max_temp() const = 0;
    /// Device may self-drive the heater (stock-firmware Auto mode).
    virtual bool device_autonomous_control() const = 0;
    /// Parse this backend's status JSON. nullopt = payload is not mine/unusable.
    virtual std::optional<ChamberHeaterDiagnostics>
    parse_diagnostics(const nlohmann::json& status) const = 0;
};

struct MatchResult {
    const ChamberHeaterBackend* backend = nullptr;
    int confidence = 0;
};

/// Fixed-priority registry. Order matters only for logging; match() is by
/// highest confidence (generic never beats an exact appliance name because
/// appliance confidence 95 > generic's non-match 0).
const std::vector<const ChamberHeaterBackend*>& registry();

/// Best backend for an object name, or nullptr.
MatchResult match(const std::string& object_name);

/// Lookup by id (nullptr if unknown).
const ChamberHeaterBackend* backend_by_id(std::string_view id);

} // namespace helix::chamber
