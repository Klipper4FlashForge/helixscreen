// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "moonraker_gcode_guards.h"

#include "gcode_homing.h"
#include "printer_state.h"
#include "spdlog/spdlog.h"

namespace helix::api {

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
