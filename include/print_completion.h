// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ui_observer_guard.h"

#include <string>

// Forward declarations
class IMoonrakerAPI;

namespace helix {

/**
 * @brief Initialize print completion notification system
 *
 * Registers an observer on PrinterState's print_state_enum subject that
 * triggers toast or modal notifications when prints complete, are cancelled,
 * or fail. Uses SettingsManager to determine notification mode.
 *
 * @return ObserverGuard that manages the observer's lifetime
 */
ObserverGuard init_print_completion_observer();

/**
 * @brief Rendered stat strings for the print-completion modal
 *
 * Optional fields are empty when the underlying value is unknown; the modal
 * binds their visibility off that.
 */
struct CompletionStats {
    std::string duration;
    std::string estimate;
    std::string layers;
    std::string filament;
};

/**
 * @brief Build the completion modal's stat strings
 *
 * Pure: no subjects, no LVGL objects. Every value routes through the shared
 * formatters in ui_format_utils so the modal cannot disagree with the rest of
 * the UI about how a layer count or a filament length reads.
 */
CompletionStats build_completion_stats(int duration_secs, int estimated_secs, int total_layers,
                                       int filament_mm);

/**
 * @brief Clean up stale .helix_temp files on startup
 *
 * Deletes all files in the .helix_temp directory on Moonraker.
 * These are temp files created when modifying G-code for prints.
 * Should be called after Moonraker connection is established.
 *
 * @param api IMoonrakerAPI instance to use for file operations
 */
void cleanup_stale_helix_temp_files(IMoonrakerAPI* api);

} // namespace helix
