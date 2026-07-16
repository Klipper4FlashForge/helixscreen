// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>

class MoonrakerAPI;

namespace helix::ui {

/// Show the one-shot "Resume interrupted print?" modal for Snapmaker-fork
/// power-loss recovery. Button mapping:
///   - primary "Resume"   -> gcode SDCARD_PRINT_PL_RESTORE
///   - secondary "Discard" -> gcode SDCARD_PRINT_PL_CLEAR_ENV
///   - backdrop / ESC      -> nothing (recovery env left intact, re-offered on
///                            the next connect)
///
/// `api` is stored as borrowed user_data on the two buttons — NO heap context
/// is allocated, so a backdrop/ESC dismissal (which fires neither callback)
/// cannot leak. `api` must outlive the modal; the app-global MoonrakerAPI does.
void show_plr_recovery_prompt(MoonrakerAPI* api);

/// Pure, LVGL-free body-text builder (unit-testable). When `file_path` is
/// non-empty, its display filename (basename, gcode extension stripped) is
/// substituted into `with_file_fmt` (which must contain a single `{}`
/// placeholder). On an empty path, an empty resolved name, OR a format failure
/// (e.g. a mistranslated placeholder), `generic_body` is returned verbatim.
/// Translation is the caller's responsibility — pass lv_tr(...) strings.
std::string plr_prompt_body(const std::string& file_path, const char* with_file_fmt,
                            const char* generic_body);

} // namespace helix::ui
