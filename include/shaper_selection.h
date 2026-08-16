// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "calibration_types.h"

#include <string>
#include <vector>

namespace helix::calibration {

/**
 * @brief Which shaper the results page will act on for one axis.
 *
 * Produced by resolve_selected_shaper(). The results card renders it and the
 * apply path writes it to the printer's config, so the two must never disagree.
 */
struct SelectedShaper {
    std::string type;
    float frequency = 0.0f;
    float vibrations = 0.0f;
    float max_accel = 0.0f;
    /// True when a chip selection produced this, false when it fell back to
    /// the firmware recommendation.
    bool from_selection = false;
    /// False when the selected CSV curve had no counterpart in the console
    /// shaper list, so vibrations/max_accel are unknown rather than zero.
    bool metrics_known = true;

    [[nodiscard]] bool is_valid() const {
        return !type.empty() && frequency > 0.0f;
    }
};

/**
 * @brief Resolve which shaper a chart chip selection refers to.
 *
 * **This is deliberately not an index lookup into @p result.all_shapers.** The
 * two vectors come from opposite halves of Klipper and are parsed by different
 * code on our side:
 *
 *   - `result.all_shapers` is scraped from the SHAPER_CALIBRATE console output.
 *   - `curves` is parsed from the calibration CSV that Klipper writes to /tmp.
 *
 * Nothing guarantees they hold the same shapers, the same count, or the same
 * order — a shaper can appear in one and not the other, and the CSV column
 * order has no relationship to the order the console prints its fits. So the
 * join is by shaper *name*, case-insensitively: the CSV column names and the
 * console recommendation are produced by different halves of Klipper and do not
 * agree on case.
 *
 * When both sides have the shaper, the console's numbers win — the CSV header
 * frequency is rounded, and vibrations/max_accel exist only on the console side.
 * When only the CSV has it, the curve's own name and frequency are returned with
 * `metrics_known = false` so callers show blanks instead of inventing zeros.
 *
 * Pure function: no logging, no LVGL. Callers log.
 *
 * @param result          Parsed console result for the axis (recommendation +
 *                        every fitted shaper option).
 * @param curves          Per-shaper response curves parsed from the CSV.
 * @param selected_index  Index into @p curves of the chip the user picked, or a
 *                        negative value / out-of-range index for "no selection".
 * @return The shaper to display and apply. Falls back to the firmware
 *         recommendation (with `from_selection = false`) when nothing is
 *         selected.
 */
SelectedShaper resolve_selected_shaper(const InputShaperResult& result,
                                       const std::vector<ShaperResponseCurve>& curves,
                                       int selected_index);

} // namespace helix::calibration
