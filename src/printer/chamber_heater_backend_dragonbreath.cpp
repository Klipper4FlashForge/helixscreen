// src/printer/chamber_heater_backend_dragonbreath.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
// VENDOR_OK: DragonBreath-firmware knowledge lives here and nowhere else.
// Status schema verified live against the U1 rig 2026-08-19 (issue #1290).
#include "chamber_heater_backend.h"

#include <spdlog/spdlog.h>

#include <cctype>

namespace helix::chamber {
namespace {

class DragonbreathBackend : public ChamberHeaterBackend {
  public:
    std::string_view id() const override {
        return "dragonbreath";
    }

    int discovery_confidence(const std::string& object_name) const override {
        std::string lower;
        lower.reserve(object_name.size());
        for (char c : object_name) {
            lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }
        return lower.find("dragonbreath") != std::string::npos ? 95 : 0;
    }

    std::string_view diagnostics_object() const override {
        return "dragonbreath";
    }
    std::string_view filter_fan_pin() const override {
        return "output_pin dragonbreath_filter";
    }
    std::string_view fault_reset_gcode() const override {
        return "DRAGONBREATH_RESET";
    }
    // Firmware hard-caps the target at 70 C; configfile usually says 75. If we
    // ever have NO ceiling data, assume the stock chamber cap.
    double conservative_max_temp() const override {
        return 60.0;
    }
    // Lease semantics arbitrate: the device invalidates our lease when another
    // controller takes over and we mirror authoritative state.
    bool device_autonomous_control() const override {
        return false;
    }

    std::optional<ChamberHeaterDiagnostics>
    parse_diagnostics(const nlohmann::json& status) const override {
        if (!status.is_object() || !status.contains("ptc_temp")) {
            return std::nullopt; // not a dragonbreath diagnostics frame
        }
        ChamberHeaterDiagnostics d;
        if (status.contains("fault") && status["fault"].is_boolean()) {
            d.fault = status["fault"].get<bool>();
        }
        if (status.contains("inhibited") && status["inhibited"].is_boolean()) {
            d.inhibited = status["inhibited"].get<bool>();
        }
        if (status.contains("fault_reason") && status["fault_reason"].is_string()) {
            d.fault_reason = status["fault_reason"].get<std::string>();
        }
        if (status["ptc_temp"].is_number()) {
            d.element_temp_c = status["ptc_temp"].get<double>();
        }
        if (status.contains("fan_percent") && status["fan_percent"].is_number()) {
            d.filter_fan_percent = status["fan_percent"].get<int>();
        }
        if (status.contains("fan_reason") && status["fan_reason"].is_string()) {
            d.filter_fan_reason = status["fan_reason"].get<std::string>();
        }
        // Externally driven: heater active, but neither klipper source nor our lease.
        bool heating = status.value("mode", "") == "power_on";
        std::string source = status.value("source", "");
        bool ours = status.value("lease_owned", false) || source == "klipper";
        d.externally_controlled = heating && !ours;
        return d;
    }
};

const DragonbreathBackend kDragonbreath;

} // namespace

// registry hookup lives in chamber_heater_backend_generic.cpp; expose via friend
// free function so this file owns its instance.
const ChamberHeaterBackend* dragonbreath_backend_instance() {
    return &kDragonbreath;
}

} // namespace helix::chamber
