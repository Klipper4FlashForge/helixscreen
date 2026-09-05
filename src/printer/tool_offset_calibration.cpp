// SPDX-License-Identifier: GPL-3.0-or-later

#include "tool_offset_calibration.h"

#include "printer_discovery.h"

#include <algorithm>

namespace helix::tool_offset_calibration {
namespace {

/// One firmware's tool-offset measuring procedure.
struct Provider {
    const char* name;
    /// Whether this printer measures offsets this way.
    bool (*detect)(const PrinterDiscovery& hw);
    /// Objects the panel must query for read() to answer.
    std::vector<std::string> (*status_objects)(const PrinterDiscovery& hw);
    /// Captions and layout - see Presentation.
    Presentation (*presentation)();
    /// One tool's numbers out of a status frame.
    std::optional<Reading> (*read_tool)(const nlohmann::json& status, int tool_index,
                                        const std::string& tool_name);
    /// The reference fixture's numbers, or nullopt when there is no such row.
    std::optional<Reading> (*read_reference)(const nlohmann::json& status);
    /// Establish the reference - empty carriage.
    std::vector<std::string> (*locate)(const PrinterDiscovery& hw);
    /// Measure one tool, in send order.
    std::vector<std::string> (*calibrate)(const PrinterDiscovery& hw, int tool_index);
    /// Persist a finished calibration, in send order.
    std::vector<std::string> (*save)(const PrinterDiscovery& hw);
    /// Whether save() only stages the change, awaiting SAVE_CONFIG.
    bool persist_needs_save_config;
};

/// status.<object> as an object, or nullptr.
const nlohmann::json* status_object(const nlohmann::json& status, const std::string& object) {
    if (!status.is_object()) {
        return nullptr;
    }
    auto it = status.find(object);
    if (it == status.end() || !it->is_object()) {
        return nullptr;
    }
    return &(*it);
}

bool has_object(const PrinterDiscovery& hw, const std::string& name) {
    const auto& objects = hw.printer_objects();
    return std::find(objects.begin(), objects.end(), name) != objects.end();
}

// --- viesturz/klipper-toolchanger, [tools_calibrate] -------------------------
//
// The tools_calibrate extra drives a nozzle-touch probe:
//
//   TOOL_LOCATE_SENSOR          once, with an EMPTY carriage - finds the probe
//   SELECT_TOOL T=<n>           pick the tool up
//   TOOL_CALIBRATE_TOOL_OFFSET  measure whatever is mounted against that probe
//
// The result is folded straight into the tool's own gcode_x/y/z_offset, which
// is why there is no reference row: the probe position is not a second set of
// numbers the operator reads, it is the zero the tool numbers are already
// expressed against. Each tool's offset is therefore a DELTA from the base
// tool, and the base tool reads 0.000 by definition.
//
// Reading the results off `tool T<n>` rather than off tools_calibrate's own
// last_x/y/z_result is deliberate. Those report the most recent measurement
// only, so they say nothing about the other three tools and go stale the moment
// the next tool is measured; the tool objects always carry the offset actually
// in effect. ToolState already parses these same three fields.
bool detect_toolchanger(const PrinterDiscovery& hw) {
    // Both halves matter. [tools_calibrate] alone is meaningless without tools
    // to measure, and a toolchanger without it has no probing hardware - its
    // offsets are typed in by hand, and offering a Calibrate button would run
    // a command Klipper rejects.
    return hw.has_tool_changer() && has_object(hw, "tools_calibrate");
}

std::vector<std::string> status_objects_toolchanger(const PrinterDiscovery& hw) {
    // The panel does a one-shot query rather than riding the subscription, so
    // it has to name the tool objects itself.
    std::vector<std::string> objects;
    for (const auto& name : hw.tool_names()) {
        objects.push_back("tool " + name);
    }
    return objects;
}

Presentation presentation_toolchanger() {
    return Presentation{{{"X", "Y", "Z"}},
                        "Offsets are measured against the base tool.",
                        /*has_reference_row=*/false,
                        ""};
}

std::optional<Reading> read_tool_toolchanger(const nlohmann::json& status, int /*tool_index*/,
                                             const std::string& tool_name) {
    if (tool_name.empty()) {
        return std::nullopt;
    }
    const nlohmann::json* tool = status_object(status, "tool " + tool_name);
    if (!tool) {
        return std::nullopt;
    }
    // All three or nothing. A frame carrying a partial tool object would
    // otherwise render two real numbers beside a fabricated zero, which reads
    // as a measured axis that was never measured.
    Reading r;
    const auto pull = [&tool](const char* key, double& out) {
        auto it = tool->find(key);
        if (it == tool->end() || !it->is_number()) {
            return false;
        }
        out = it->get<double>();
        return true;
    };
    if (!pull("gcode_x_offset", r.x) || !pull("gcode_y_offset", r.y) ||
        !pull("gcode_z_offset", r.z)) {
        return std::nullopt;
    }
    return r;
}

std::optional<Reading> read_reference_none(const nlohmann::json& /*status*/) {
    return std::nullopt;
}

std::vector<std::string> locate_toolchanger(const PrinterDiscovery& /*hw*/) {
    return {"TOOL_LOCATE_SENSOR"};
}

std::vector<std::string> calibrate_toolchanger(const PrinterDiscovery& /*hw*/, int tool_index) {
    // TOOL_CALIBRATE_TOOL_OFFSET takes no arguments - it measures whatever is
    // on the carriage - so the selection has to lead or it measures the tool
    // that happened to be mounted.
    return {"SELECT_TOOL T=" + std::to_string(tool_index), "TOOL_CALIBRATE_TOOL_OFFSET"};
}

std::vector<std::string> save_toolchanger(const PrinterDiscovery& /*hw*/) {
    // The measurement goes through configfile.set(), i.e. a pending config
    // change. Nothing survives a restart until SAVE_CONFIG commits it - and
    // SAVE_CONFIG itself restarts Klipper.
    return {"SAVE_CONFIG"};
}

const std::vector<Provider>& providers() {
    static const std::vector<Provider> table = {
        {"klipper-toolchanger", &detect_toolchanger, &status_objects_toolchanger,
         &presentation_toolchanger, &read_tool_toolchanger, &read_reference_none,
         &locate_toolchanger, &calibrate_toolchanger, &save_toolchanger, true},
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

} // namespace

bool supported(const PrinterDiscovery& hw) {
    return match(hw) != nullptr;
}

std::vector<std::string> required_status_objects(const PrinterDiscovery& hw) {
    if (const Provider* p = match(hw)) {
        return p->status_objects(hw);
    }
    return {};
}

Presentation presentation(const PrinterDiscovery& hw) {
    if (const Provider* p = match(hw)) {
        return p->presentation();
    }
    return {};
}

std::optional<Reading> read_tool(const nlohmann::json& status, int tool_index,
                                 const std::string& tool_name) {
    // Read by schema rather than by detected firmware: this runs off a status
    // frame, which has no PrinterDiscovery to hand. One provider answering is
    // enough - the schemas do not overlap.
    if (tool_index < 0) {
        return std::nullopt;
    }
    for (const auto& p : providers()) {
        if (auto reading = p.read_tool(status, tool_index, tool_name)) {
            return reading;
        }
    }
    return std::nullopt;
}

std::optional<Reading> read_reference(const nlohmann::json& status) {
    for (const auto& p : providers()) {
        if (auto reading = p.read_reference(status)) {
            return reading;
        }
    }
    return std::nullopt;
}

std::vector<std::string> locate_reference_gcode(const PrinterDiscovery& hw) {
    if (const Provider* p = match(hw)) {
        return p->locate(hw);
    }
    return {};
}

std::vector<std::string> calibrate_tool_gcode(const PrinterDiscovery& hw, int tool_index) {
    const Provider* p = match(hw);
    if (!p || tool_index < 0) {
        return {};
    }
    return p->calibrate(hw, tool_index);
}

bool persist_requires_save_config(const PrinterDiscovery& hw) {
    const Provider* p = match(hw);
    return p && p->persist_needs_save_config;
}

std::vector<std::string> save_gcode(const PrinterDiscovery& hw) {
    if (const Provider* p = match(hw)) {
        return p->save(hw);
    }
    return {};
}

std::string provider_name(const PrinterDiscovery& hw) {
    const Provider* p = match(hw);
    return p ? p->name : std::string{};
}

} // namespace helix::tool_offset_calibration
