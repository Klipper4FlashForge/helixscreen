// SPDX-License-Identifier: GPL-3.0-or-later

#include "tool_z_offset.h"

#include "printer_discovery.h"

#include <spdlog/fmt/fmt.h>

#include <cmath>

namespace helix::zoffset {
namespace {

/// One firmware that keeps a per-tool Z correction beside Klipper's global one.
struct Provider {
    const char* name;
    /// Identifies the firmware from the printer's object list.
    bool (*detect)(const PrinterDiscovery& hw);
    /// Status object carrying tool @p tool's value.
    std::string (*status_object)(int tool);
    /// Member of that object holding the correction, in mm.
    const char* value_field;
    /// Live relative nudge.
    std::string (*adjust)(int tool, double delta_mm);
    /// Absolute set + persist, in send order.
    std::vector<std::string> (*save)(int tool, double value_mm);
    /// Whether save() ends in a Klipper restart.
    bool save_restarts_klipper;
};

bool has_object_prefix(const PrinterDiscovery& hw, const std::string& prefix) {
    for (const auto& object : hw.printer_objects()) {
        if (object.rfind(prefix, 0) == 0) {
            return true;
        }
    }
    return false;
}

// --- Reforge / ff_toolchange (FlashForge Creator 5, Creator 5 Pro) ----------
//
// [ff_tool n] carries `z_adjust`, the operator's own per-tool number. The
// tool-change transform sits BELOW gcode_move and adds it for the mounted tool
// alone, which is what keeps it independent of homing_origin: the firmware
// deliberately refuses to fold the two together, because SET_GCODE_OFFSET Z=0
// (what any END_PRINT issues) cannot tell one from the other and would wipe
// both. TOOL_Z_ADJUST is live on issue and only staged to printer.cfg on
// SAVE=1, so persisting costs a SAVE_CONFIG restart.
bool detect_ff(const PrinterDiscovery& hw) {
    return has_object_prefix(hw, "ff_tool ");
}

std::string ff_object(int tool) {
    return fmt::format("ff_tool {}", tool);
}

std::string ff_adjust(int tool, double delta_mm) {
    return fmt::format("TOOL_Z_ADJUST TOOL={} ADJUST={:.3f}", tool, delta_mm);
}

std::vector<std::string> ff_save(int tool, double value_mm) {
    return {fmt::format("TOOL_Z_ADJUST TOOL={} VALUE={:.3f} SAVE=1", tool, value_mm),
            "SAVE_CONFIG"};
}

const std::vector<Provider>& providers() {
    static const std::vector<Provider> table = {
        {"Reforge", &detect_ff, &ff_object, "z_adjust", &ff_adjust, &ff_save, true},
    };
    return table;
}

const Provider* match(const PrinterDiscovery& hw) {
    for (const auto& p : providers()) {
        if (p.detect(hw)) {
            return &p;
        }
    }
    return nullptr;
}

/// status.<object>.<field> as a number, when present.
std::optional<double> number_field(const nlohmann::json& status, const std::string& object,
                                   const char* field) {
    if (!status.is_object()) {
        return std::nullopt;
    }
    auto outer = status.find(object);
    if (outer == status.end() || !outer->is_object()) {
        return std::nullopt;
    }
    auto value = outer->find(field);
    if (value == outer->end() || !value->is_number()) {
        return std::nullopt;
    }
    return value->get<double>();
}

int to_microns(double mm) {
    // Round rather than truncate: the stored value accumulates relative
    // adjustments, so a nominal -0.150 arrives as -0.1499999.
    return static_cast<int>(std::lround(mm * 1000.0));
}

} // namespace

bool supports_per_tool_offset(const PrinterDiscovery& hw) {
    return match(hw) != nullptr;
}

std::string per_tool_provider_name(const PrinterDiscovery& hw) {
    const Provider* p = match(hw);
    return p ? p->name : std::string{};
}

std::vector<std::string> per_tool_status_objects(const PrinterDiscovery& hw, int tool_count) {
    const Provider* p = match(hw);
    if (!p || tool_count <= 0) {
        return {};
    }
    std::vector<std::string> objects;
    objects.reserve(static_cast<size_t>(tool_count));
    for (int tool = 0; tool < tool_count; ++tool) {
        objects.push_back(p->status_object(tool));
    }
    return objects;
}

std::optional<int> read_tool_offset_microns(const nlohmann::json& status, int tool) {
    if (tool < 0) {
        return std::nullopt;
    }
    for (const auto& p : providers()) {
        if (auto mm = number_field(status, p.status_object(tool), p.value_field)) {
            return to_microns(*mm);
        }
    }
    return std::nullopt;
}

std::string build_tool_adjust_gcode(const PrinterDiscovery& hw, int tool, int delta_microns) {
    const Provider* p = match(hw);
    if (!p || tool < 0) {
        return {};
    }
    return p->adjust(tool, delta_microns / 1000.0);
}

std::vector<std::string> build_tool_save_gcode(const PrinterDiscovery& hw, int tool,
                                               int value_microns) {
    const Provider* p = match(hw);
    if (!p || tool < 0) {
        return {};
    }
    return p->save(tool, value_microns / 1000.0);
}

bool per_tool_save_restarts_klipper(const PrinterDiscovery& hw) {
    const Provider* p = match(hw);
    return p != nullptr && p->save_restarts_klipper;
}

} // namespace helix::zoffset
