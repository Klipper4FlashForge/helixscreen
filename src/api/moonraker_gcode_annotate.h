// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file moonraker_gcode_annotate.h
 * @brief Shared outgoing-G-code annotation + send-layer guards.
 *
 * HelixScreen appends a provenance comment (GCODE_SOURCE_COMMENT) to outgoing
 * G-code it initiates, for traceability in Klipper logs. Three send paths need
 * the same logic (moonraker_client.cpp, moonraker_api_controls.cpp,
 * moonraker_motion_api.cpp); this header is the single source of truth.
 *
 * The comment is dropped when:
 *   - the command is user-typed console input (never tag user G-code), or
 *   - the connected printer's firmware re-echoes received G-code (e.g. the
 *     FlashForge AD5X wraps each line into a quoted RESPOND MSG="..."). Klipper
 *     truncates at the first ';' even inside quotes, so a trailing "; from
 *     helixscreen" leaves an unterminated quote -> "Malformed command".
 */

#pragma once

#include <functional>
#include <string>

#include "moonraker_error.h"

namespace helix {
class PrinterState;
}

namespace helix::api {

/// Provenance comment appended to HelixScreen-initiated outgoing G-code lines.
inline constexpr const char* GCODE_SOURCE_COMMENT = " ; from helixscreen";

/// True for M117 (display message) / M118 (console echo) whose argument Klipper
/// treats as a literal text payload — it does NOT strip a trailing "; ..."
/// comment before storing/echoing it, so those lines must never be annotated.
/// Case-insensitive; tolerates leading whitespace.
bool is_gcode_text_payload_command(const std::string& line);

/// Annotate multi-line G-code with GCODE_SOURCE_COMMENT.
/// When @p add_comment is false the input is returned unchanged. When true,
/// the comment is appended to each non-empty line except M117/M118 payloads.
std::string annotate_gcode(const std::string& gcode, bool add_comment);

/// Reject app-initiated homing while a print is active (PRINTING or PAUSED).
/// On a loadcell-Z printer (e.g. AD5X) a mid-print G28 drives the nozzle into
/// the part. Returns true when it rejected the command (the caller must then
/// return immediately); logs a warning prefixed with @p log_tag (unless
/// @p silent) and invokes @p on_error with a NOT_READY error. Returns false to
/// let the command through.
bool reject_homing_during_active_print(const std::string& gcode, helix::PrinterState& state,
                                       bool silent,
                                       const std::function<void(const MoonrakerError&)>& on_error,
                                       const char* log_tag);

} // namespace helix::api
