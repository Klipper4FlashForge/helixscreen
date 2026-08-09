// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "pause_cause.h"

#include <string>
#include <vector>

namespace helix {

/// Terminal-cause patterns for the Snapmaker U1.
///
/// Hardware-verified on a physical U1 (prestonbrown/helixscreen#991):
///   - Dirty bed:  exception {id:532, code:1, message:"detected dirty bed"} — Terminal.
///   - Runout:     exception {id:523, code:0, message:"e1_filament runout"} — Recoverable.
/// Two matchers cover dirty-bed by either id or message text. We deliberately do
/// NOT set require_sdcard_inactive: runout ALSO clears virtual_sdcard.is_active,
/// so sdcard state cannot discriminate terminal-vs-recoverable here.
std::vector<TerminalMatcher> snapmaker_terminal_matchers();

/// Extract the human-readable `msg` field from a Snapmaker firmware
/// JSON-coded error, e.g.
/// `{"coded":"0001-0531-0000-0005","msg":"...","action":"none"}`.
/// The JSON object may be embedded inside a longer gcode error string (e.g.
/// "!! Error executing script SDCARD_PRINT_PL_RESTORE: {...}"), so this scans
/// for and parses the first balanced `{...}` object rather than requiring the
/// whole string to be JSON. Returns `fallback` when no JSON object is found,
/// the JSON is malformed, or it has no string `msg` field.
std::string snapmaker_extract_coded_msg(const std::string& raw_error, const std::string& fallback);

} // namespace helix
