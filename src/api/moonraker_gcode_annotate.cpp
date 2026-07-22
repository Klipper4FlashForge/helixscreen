// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "moonraker_gcode_annotate.h"

#include "gcode_homing.h"
#include "printer_state.h"
#include "spdlog/spdlog.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace helix::api {

// M117 (display message) and M118 (console echo) consume the rest of the line
// as a literal text payload — Klipper does NOT strip a trailing " ; ..." comment
// before storing/echoing it (confirmed live: "M117 Hello World" rendered
// on-screen as "Hello World ; from helixscreen"). Every other command treats
// ";" as an end-of-line comment Klipper safely ignores, so only these two need
// to be skipped. Case-insensitive; tolerates leading whitespace. This codebase
// never emits Nxxx line-number prefixes on outgoing G-code, so that case is not
// handled.
bool is_gcode_text_payload_command(const std::string& line) {
    size_t start = line.find_first_not_of(" \t\r");
    if (start == std::string::npos || line.size() < start + 4) {
        return false;
    }
    char c0 = static_cast<char>(std::tolower(static_cast<unsigned char>(line[start])));
    char c1 = static_cast<char>(std::tolower(static_cast<unsigned char>(line[start + 1])));
    char c2 = line[start + 2];
    char c3 = line[start + 3];
    if (c0 != 'm' || c1 != '1' || c2 != '1' || (c3 != '7' && c3 != '8')) {
        return false;
    }
    // Boundary check: the char after "M117"/"M118" (if any) must not continue
    // the token — "M1170" is a different (unknown) command, not M117.
    if (line.size() > start + 4 && std::isalnum(static_cast<unsigned char>(line[start + 4]))) {
        return false;
    }
    return true;
}

std::string annotate_gcode(const std::string& gcode, bool add_comment) {
    if (!add_comment) {
        return gcode;
    }

    std::string result;
    result.reserve(gcode.size() + 20 * std::count(gcode.begin(), gcode.end(), '\n') + 20);

    std::istringstream stream(gcode);
    std::string line;
    bool first = true;

    while (std::getline(stream, line)) {
        if (!first) {
            result += '\n';
        }
        first = false;

        // Only add comment to non-empty lines, and never to M117/M118 whose
        // argument is a literal text payload (see is_gcode_text_payload_command).
        if (!line.empty() && line.find_first_not_of(" \t\r") != std::string::npos &&
            !is_gcode_text_payload_command(line)) {
            result += line + GCODE_SOURCE_COMMENT;
        } else {
            result += line;
        }
    }

    return result;
}

bool reject_homing_during_active_print(const std::string& gcode, helix::PrinterState& state,
                                       bool silent,
                                       const std::function<void(const MoonrakerError&)>& on_error,
                                       const char* log_tag) {
    if (!helix::is_homing_gcode(gcode)) {
        return false;
    }
    const helix::PrintJobState pstate = state.get_print_job_state();
    if (pstate != helix::PrintJobState::PRINTING && pstate != helix::PrintJobState::PAUSED) {
        return false;
    }
    if (!silent) {
        spdlog::warn("{} Refusing homing G-code during active print (state={}): '{}'", log_tag,
                     static_cast<int>(pstate), gcode.substr(0, 60));
    }
    if (on_error) {
        on_error(MoonrakerError::not_ready("printer.gcode.script",
                                           "Homing is disabled while a print is in progress"));
    }
    return true;
}

} // namespace helix::api
