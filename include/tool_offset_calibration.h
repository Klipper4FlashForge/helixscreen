// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Per-tool offset CALIBRATION - the measuring procedure, not the stored value.
//
// helix::tool_offsets owns what a tool's z-offset IS and how to write it. This
// module owns how a machine MEASURES all three axes for itself: which commands
// drive a probe, whether a reference pass has to run first, and how to read the
// result back out of a status frame.
//
// The two are separate capabilities. A printer can carry per-tool offsets with
// no way to measure them (the operator types numbers in), and the firmwares
// that can measure disagree on almost everything: the command names, whether
// there is a reference fixture at all, what the resulting numbers MEAN, and
// therefore what the screen should call them.
//
// They also disagree on whether the measured value and the adjustable one are
// even the same storage. One firmware writes a measurement into the very field
// the tune panel nudges - two layers, and re-measuring discards the nudge.
// Another keeps measured geometry, a per-tool adjustment, and the global
// baby-step as three separate things. Each Provider names the objects its own
// firmware uses, and nothing above them depends on which shape it is.
//
// That last one is why Presentation exists. What the numbers MEAN is the
// firmware's business, not the panel's: one machine reports each tool against a
// base tool, another against a fixed fixture it probes first and shows that
// fixture as a row of its own. The panel renders numbers and captions and
// interprets neither - it does not know which model it is showing, and must not
// need to. So the provider hands it both.
//
// Generic code (the calibration panel, its entry point) asks these functions
// and never names a firmware. Adding one means adding a Provider to the table
// in tool_offset_calibration.cpp - no call site changes.

#include <array>
#include <optional>
#include <string>
#include <vector>

#include "hv/json.hpp"

namespace helix {
class PrinterDiscovery;
}

namespace helix::tool_offset_calibration {

/// One row's three numbers, in mm, as the provider wants them shown.
struct Reading {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

/// How this firmware's results read on screen.
///
/// Held by value and pointing at string literals owned by the provider table,
/// so it outlives any call and needs no ownership handling at the call site.
struct Presentation {
    /// Column captions, left to right. Three, because every firmware here
    /// measures three axes.
    std::array<const char*, 3> columns{{"X", "Y", "Z"}};

    /// One line above the tool list saying what the numbers mean. Shown
    /// verbatim, so it is a full sentence and a translation tag.
    const char* caption = "";

    /// Whether a reference pass must run before any tool can be measured, and
    /// therefore whether the panel shows a reference row above the tools.
    ///
    /// False on klipper-toolchanger: TOOL_LOCATE_SENSOR establishes the probe
    /// position but the result is folded into the tool offsets rather than
    /// shown, so there is no second set of numbers to display.
    bool has_reference_row = false;

    /// Caption for the reference row. Empty when has_reference_row is false.
    const char* reference_caption = "";
};

/// Whether this printer can measure its own tool offsets. False on every
/// printer without the probing hardware, which is what gates the entry point.
bool supported(const PrinterDiscovery& hw);

/// Status objects the panel must query for read_tool() to return a value,
/// beyond what the tool-changer subscription already requests.
std::vector<std::string> required_status_objects(const PrinterDiscovery& hw);

/// This firmware's captions and layout. Returns a default-constructed
/// Presentation when the printer has no provider, so a caller that renders it
/// anyway shows empty captions rather than dereferencing nothing.
Presentation presentation(const PrinterDiscovery& hw);

/// One tool's measured offsets, read out of a Moonraker status frame.
///
/// nullopt means "not measured" - either never calibrated, or this frame
/// carries no news about it. Moonraker republishes only what CHANGED, so a
/// frame that omits the tool is routine and must never read as zero.
///
/// @param tool_index tool number, as used in T<n>
/// @param tool_name  Klipper's name for the tool ("T0"), for firmwares that
///                   key their status object off it rather than the index
std::optional<Reading> read_tool(const nlohmann::json& status, int tool_index,
                                 const std::string& tool_name);

/// The reference fixture's position, for firmwares that show one.
/// Always nullopt when presentation().has_reference_row is false.
std::optional<Reading> read_reference(const nlohmann::json& status);

/// Gcode calibrating EVERY tool, in send order. Empty when the printer has no
/// provider.
///
/// The whole machine at once, not one tool: the firmwares here wrap the
/// procedure in a single command that owns the reference pass, the machine
/// state it needs, the temperature to measure at, and which tools exist. That
/// last one is why there is no per-tool form - the wrapper reads the tool list
/// off the toolchanger, so it is right on a 2-head or a 5-head machine, and a
/// caller asking for "just T2" would be asking for something no firmware here
/// exposes.
///
/// What state the machine must be in is the firmware's business - it refuses on
/// its own terms and the refusal is surfaced as-is.
std::vector<std::string> calibrate_gcode(const PrinterDiscovery& hw);

/// Whether the measured offsets are only staged and need a SAVE_CONFIG to
/// commit - which restarts Klipper, so the caller must drive it through the
/// same save-and-restart handling a probe calibration uses.
bool persist_requires_save_config(const PrinterDiscovery& hw);

/// Gcode persisting the calibration of @p tools, in send order. Empty when the
/// firmware writes durably as it measures, when @p tools is empty, or when the
/// printer has no provider.
///
/// Takes the tools rather than persisting wholesale because the durable write
/// is per tool on every firmware here, and a run may have measured only one.
std::vector<std::string> save_gcode(const PrinterDiscovery& hw, const std::vector<int>& tools);

/// The gcode command whose `description:` the panel shows as its on-screen
/// instruction, or empty when this firmware has none.
///
/// Some firmwares ship an all-in-one wrapper macro carrying a written
/// procedure ("take the build plate off, ..."). Where one exists it is the
/// firmware's own words for its own hardware, which beats anything we could
/// write generically. klipper-toolchanger's calibration is a Python extra
/// registering bare commands, so it has no description to read and the panel
/// falls back to its own text.
std::string hint_command(const PrinterDiscovery& hw);

/// Human-readable name of the matched provider, for logging. Empty when none.
std::string provider_name(const PrinterDiscovery& hw);

} // namespace helix::tool_offset_calibration
