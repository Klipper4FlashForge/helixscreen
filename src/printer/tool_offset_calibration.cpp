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
    /// Run the whole calibration, in send order.
    std::vector<std::string> (*calibrate)(const PrinterDiscovery& hw);
    /// Persist a finished calibration for @p tools, in send order.
    std::vector<std::string> (*save)(const PrinterDiscovery& hw, const std::vector<int>& tools);
    /// Whether save() only stages the change, awaiting SAVE_CONFIG.
    bool persist_needs_save_config;
    /// Command whose `description:` is the on-screen instruction, or nullptr.
    const char* hint_command;
};

/// The wrapper macro klipper-toolchanger ships as a printer.cfg example. It is
/// both the capability gate and the whole command surface - see
/// detect_toolchanger() for why we gate on the macro rather than the extra.
constexpr const char* CALIBRATE_MACRO = "CALIBRATE_TOOL_OFFSETS";

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

// --- viesturz/klipper-toolchanger, [tools_calibrate] -------------------------
//
// The tools_calibrate extra drives a nozzle-touch probe:
//
//   TOOL_LOCATE_SENSOR          once - establishes the reference
//   SELECT_TOOL T=<n>           pick the tool up
//   TOOL_CALIBRATE_TOOL_OFFSET  measure whatever is mounted against that probe
//
// What state the machine must be in for each of those - what is on the
// carriage, whether it is homed, whether the plate is off - is the firmware's
// business, and it refuses on its own terms. We send the passes in order and
// report what comes back; we do not restate its preconditions here, because a
// stale copy of them is worse than none.
//
// The result is folded straight into the tool's own gcode_x/y/z_offset, which
// is why there is no reference row: the probe position is not a second set of
// numbers the operator reads, it is absorbed into the tool numbers themselves.
// How those numbers relate to each other is the firmware's model, stated once
// in the caption below and nowhere else - nothing here interprets them.
//
// Reading the results off `tool T<n>` rather than off tools_calibrate's own
// last_x/y/z_result is deliberate. Those report the most recent measurement
// only, so they say nothing about the other three tools and go stale the moment
// the next tool is measured; the tool objects always carry the offset actually
// in effect. ToolState already parses these same three fields.
//
// NOTE, and it is load-bearing: on this firmware the measured offset and the
// operator's own per-tool adjustment are THE SAME FIELD. helix::tool_offsets
// writes gcode_z_offset for the tune panel's per-tool nudge, and a calibration
// pass writes the same three. So re-calibrating a tool discards whatever the
// operator had nudged it to, and a nudge moves what this panel displays. That
// is the firmware's model, not a defect here - but it is NOT general. A
// firmware that keeps the measured geometry and the operator's adjustment in
// separate stores (a station bore plus a per-tool z_adjust, say) has three
// layers where this has two, and its Provider reads different objects for each.
// That separation is precisely why measuring lives in this module and the
// adjustable value lives in helix::tool_offsets.
bool detect_toolchanger(const PrinterDiscovery& hw) {
    // The MACRO, not the [tools_calibrate] extra underneath it.
    //
    // We gate on what we actually send. CALIBRATE_TOOL_OFFSETS is the wrapper
    // klipper-toolchanger ships as a printer.cfg example: it owns the whole
    // procedure - the reference pass, heating, which tools exist and in what
    // order - and reads the tool list off the toolchanger itself, so it is
    // right on a 2-head or a 5-head machine without us modelling either.
    //
    // The tradeoff is deliberate: because it is config rather than part of the
    // extra, a machine running tools_calibrate WITHOUT that macro reads as
    // unsupported here. That is the honest answer - we have no command to send
    // such a printer that we could promise anything about.
    return hw.has_tool_changer() && hw.has_macro(CALIBRATE_MACRO);
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

std::vector<std::string> calibrate_toolchanger(const PrinterDiscovery& /*hw*/) {
    // One command for the whole machine. Everything the stepwise form had to
    // assume - whether a reference pass must lead, what state the machine must
    // be in, whether a tool must be selected before it can be measured, what
    // temperature to measure at, which tools exist - is the macro's business
    // and it does not need us to agree with it.
    return {CALIBRATE_MACRO};
}

std::vector<std::string> save_toolchanger(const PrinterDiscovery& /*hw*/,
                                          const std::vector<int>& tools) {
    // Persist EXPLICITLY rather than trusting a bare SAVE_CONFIG to pick the
    // measurement up.
    //
    // SAVE_TOOL_PARAMETER takes no value: it persists whatever the tool
    // currently HOLDS (klipper-toolchanger's Tool.save_parameter() is
    // configfile.set(self.name, name, self.params[name])). That property is
    // what makes this correct without knowing where the calibration pass put
    // its result - if it wrote the tool's offsets, this stages exactly those;
    // if it staged them itself already, this stages the same values again.
    // Either way what lands in printer.cfg is the offset the machine is
    // actually printing with, which is the only value worth persisting.
    //
    // A bare SAVE_CONFIG would instead be a bet that the pass had already
    // staged a pending config change, and would silently persist nothing if it
    // had not.
    std::vector<std::string> lines;
    for (int tool : tools) {
        if (tool < 0) {
            continue;
        }
        const std::string t = std::to_string(tool);
        for (const char* axis : {"gcode_x_offset", "gcode_y_offset", "gcode_z_offset"}) {
            lines.push_back("SAVE_TOOL_PARAMETER T=" + t + " PARAMETER=" + axis);
        }
    }
    if (lines.empty()) {
        return {};
    }
    // Staging alone changes nothing durable; SAVE_CONFIG commits, and restarts.
    lines.emplace_back("SAVE_CONFIG");
    return lines;
}

const std::vector<Provider>& providers() {
    static const std::vector<Provider> table = {
        // No hint command: the calibration is a Python extra registering bare
        // commands, not a macro with a written description.
        // The macro carries a written procedure in its `description:`, so the
        // panel shows the firmware's own words rather than anything generic.
        {"klipper-toolchanger", &detect_toolchanger, &status_objects_toolchanger,
         &presentation_toolchanger, &read_tool_toolchanger, &read_reference_none,
         &calibrate_toolchanger, &save_toolchanger, true, CALIBRATE_MACRO},
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

std::vector<std::string> calibrate_gcode(const PrinterDiscovery& hw) {
    if (const Provider* p = match(hw)) {
        return p->calibrate(hw);
    }
    return {};
}

bool persist_requires_save_config(const PrinterDiscovery& hw) {
    const Provider* p = match(hw);
    return p && p->persist_needs_save_config;
}

std::vector<std::string> save_gcode(const PrinterDiscovery& hw, const std::vector<int>& tools) {
    if (const Provider* p = match(hw)) {
        return p->save(hw, tools);
    }
    return {};
}

std::string hint_command(const PrinterDiscovery& hw) {
    const Provider* p = match(hw);
    return (p && p->hint_command) ? p->hint_command : std::string{};
}

std::string provider_name(const PrinterDiscovery& hw) {
    const Provider* p = match(hw);
    return p ? p->name : std::string{};
}

} // namespace helix::tool_offset_calibration
