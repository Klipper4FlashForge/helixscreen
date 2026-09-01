// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Per-tool Z offset ("per-tool baby stepping").
//
// Klipper's own baby step is ONE number for the whole machine: SET_GCODE_OFFSET
// writes gcode_move.homing_origin, which every tool then rides on. That is the
// right model for a single-nozzle printer and the wrong one for a tool changer,
// where each nozzle sits at its own height and the operator wants to squish T1
// without moving T0.
//
// Some tool changer firmwares therefore keep a SECOND, per-tool correction that
// their tool-change transform applies for the mounted tool alone, leaving the
// global offset untouched. This module is the ONLY place that knows which
// firmwares have one, what object publishes it, and what command edits it.
// Generic code (ToolState, the discovery subscription, the tune overlay) asks
// the questions below and never names a firmware.
//
// The two axes are independent and both are real:
//
//   global   SET_GCODE_OFFSET Z_ADJUST=  -> homing_origin, all tools
//   per-tool <provider command>          -> that tool's own correction
//
// A UI that offers both must say which one it is about to move; silently
// picking one is how a user ends up dialling the whole machine down when they
// meant to fix one nozzle.
//
// Adding a firmware means adding one Provider to the table in
// tool_z_offset.cpp - no call site changes.

#include <optional>
#include <string>
#include <vector>

#include "hv/json.hpp"

namespace helix {
class PrinterDiscovery;
}

namespace helix::zoffset {

/// Whether this printer can adjust one tool's Z independently of the others.
/// False on every single-tool printer and on tool changers whose firmware only
/// exposes the global offset.
bool supports_per_tool_offset(const PrinterDiscovery& hw);

/// Human-readable name of the matched firmware, for logging. Empty when none.
std::string per_tool_provider_name(const PrinterDiscovery& hw);

/// Klipper status objects that must be subscribed for
/// read_tool_offset_microns() to ever return a value on this printer. Empty
/// when the printer has no per-tool offset.
///
/// @param tool_count how many tools to cover; the objects are per tool.
std::vector<std::string> per_tool_status_objects(const PrinterDiscovery& hw, int tool_count);

/// The per-tool correction for @p tool, in microns, read out of a Moonraker
/// status frame. nullopt when this frame carries no value for that tool -
/// either the printer has no per-tool offset, or the object simply is not in
/// this (delta-only) frame. Callers must treat nullopt as "no news", never as
/// "zero".
///
/// Read by schema rather than by detected firmware, like
/// read_persisted_offset_microns(): this runs on the status path, which has no
/// PrinterDiscovery to hand.
std::optional<int> read_tool_offset_microns(const nlohmann::json& status, int tool);

/// Command for a live per-tool baby step of @p delta_microns, or an empty
/// string when the printer has no per-tool offset.
///
/// Relative by construction: the provider resolves it against its own stored
/// value, so a UI that displayed a slightly stale number still lands on "what
/// the printer had, plus what the user asked for".
///
/// Takes effect immediately for the mounted tool and on the next tool change
/// for any other. It does not move the toolhead, exactly as SET_GCODE_OFFSET
/// Z_ADJUST without MOVE=1 does not.
std::string build_tool_adjust_gcode(const PrinterDiscovery& hw, int tool, int delta_microns);

/// Commands that set @p tool's correction to an absolute @p value_microns and
/// persist it, in the order they must be sent. Empty when the printer has no
/// per-tool offset.
///
/// Persisting is a config write, so on most providers the last command is
/// SAVE_CONFIG, which RESTARTS Klipper - never send this mid-print. Ask
/// per_tool_save_restarts_klipper() rather than assuming either way.
std::vector<std::string> build_tool_save_gcode(const PrinterDiscovery& hw, int tool,
                                               int value_microns);

/// Whether build_tool_save_gcode() ends in a Klipper restart, so the caller can
/// warn and can refuse while a print is running.
bool per_tool_save_restarts_klipper(const PrinterDiscovery& hw);

} // namespace helix::zoffset
