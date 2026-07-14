// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file gcode_classify.h
 * @brief Classify a G-code script as "discretionary" (safe to refuse when busy).
 *
 * Used to gate convenience commands (fan, temp, non-homing moves, LED) at the
 * send boundary while the printer is executing a blocking non-print operation
 * (G28, BED_MESH_CALIBRATE, manual probe, ...). Those commands would otherwise
 * queue behind the blocking op and time out after 60 s. Recovery, homing,
 * probe-control, and macro scripts are NEVER discretionary so they always pass.
 * See the guards in IMoonrakerAPI::execute_gcode / MoonrakerMotionAPI::execute_gcode.
 */

#pragma once

#include <cctype>
#include <sstream>
#include <string>

namespace helix {

/// True if @p script consists ENTIRELY of discretionary commands — convenience
/// commands that are safe to refuse while the printer is busy with a blocking
/// operation. Default-ALLOW: returns true ONLY for the known discretionary set,
/// so anything unrecognized (recovery, homing, probe control, macros) is treated
/// as important and returns false.
///
/// Discretionary command set (first whitespace-delimited token of a line,
/// case-insensitive, WHOLE-token compare):
///   - Fan:  M106, M107, SET_FAN_SPEED
///   - Temp: M104, M140, M109, M190, M141 (chamber macro),
///           SET_HEATER_TEMPERATURE, SET_TEMPERATURE_FAN_TARGET
///     (every target-setting form IMoonrakerAPI::set_temperature can emit —
///      see build_heater_gcode())
///   - Move: G0, G1  (non-homing moves)
///   - Positioning mode: G90, G91 (absolute/relative — wrap our own jog moves,
///     which are emitted as "G91\nG0 X..\nG90"; pure modal state, safe to defer)
///   - LED:  SET_LED
///
/// Multi-line: returns true ONLY if the script is non-empty AND every non-blank
/// command line's first token is in the discretionary set. If ANY non-blank line
/// is non-discretionary (e.g. a compound macro containing a G28 or TESTZ), the
/// whole script is NOT discretionary (returns false → allowed). Blank lines,
/// whitespace-only lines, and comment-only lines are ignored. Inline `;` comments
/// are stripped before tokenizing, so "M106 S128 ; cool" tokenizes as "M106".
///
/// Whole-token matching keeps prefixes out: "M1090", "G10", "M10", and
/// "SET_FAN_SPEED_EXTRA" all return false — a substring match would wrongly trip.
inline bool is_discretionary_gcode(const std::string& script) {
    std::istringstream lines(script);
    std::string line;
    bool saw_command = false;
    while (std::getline(lines, line)) {
        // Strip an inline comment: everything from the first ';' onward.
        const size_t comment = line.find(';');
        if (comment != std::string::npos) {
            line.erase(comment);
        }
        // First non-blank character on the line.
        const size_t start = line.find_first_not_of(" \t\r");
        if (start == std::string::npos) {
            continue; // blank / whitespace-only / comment-only line
        }
        // First token spans until the next whitespace (or end of line).
        const size_t end = line.find_first_of(" \t\r", start);
        std::string token =
            line.substr(start, end == std::string::npos ? std::string::npos : end - start);
        for (char& c : token) {
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        }
        const bool discretionary =
            token == "M106" || token == "M107" || token == "SET_FAN_SPEED" ||
            token == "M104" || token == "M140" || token == "M109" || token == "M190" ||
            token == "M141" || token == "SET_HEATER_TEMPERATURE" ||
            token == "SET_TEMPERATURE_FAN_TARGET" || token == "G0" || token == "G1" ||
            token == "G90" || token == "G91" || token == "SET_LED";
        if (!discretionary) {
            return false; // any non-discretionary command → allow the whole script
        }
        saw_command = true;
    }
    return saw_command;
}

} // namespace helix
