// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "shaper_selection.h"

#include <algorithm>
#include <cctype>

namespace helix::calibration {

/**
 * @brief Case-insensitive shaper type comparison
 *
 * The CSV column names and the console recommendation are produced by different
 * halves of Klipper and do not agree on case.
 */
static bool shaper_names_equal(const std::string& a, const std::string& b) {
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); i++) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    }
    return true;
}

SelectedShaper resolve_selected_shaper(const InputShaperResult& result,
                                       const std::vector<ShaperResponseCurve>& curves,
                                       int selected_index) {
    SelectedShaper sel;

    if (selected_index < 0 || selected_index >= static_cast<int>(curves.size())) {
        // Nothing selected - fall back to the firmware recommendation.
        sel.type = result.shaper_type;
        sel.frequency = result.shaper_freq;
        sel.vibrations = result.vibrations;
        sel.max_accel = result.max_accel;
        sel.from_selection = false;
        sel.metrics_known = true;
        return sel;
    }

    const ShaperResponseCurve& curve = curves[static_cast<size_t>(selected_index)];
    sel.from_selection = true;

    auto opt_it =
        std::find_if(result.all_shapers.begin(), result.all_shapers.end(),
                     [&](const ShaperOption& o) { return shaper_names_equal(o.type, curve.name); });
    if (opt_it != result.all_shapers.end()) {
        // Prefer the console's fit - the CSV header frequency is rounded.
        sel.type = opt_it->type;
        sel.frequency = opt_it->frequency;
        sel.vibrations = opt_it->vibrations;
        sel.max_accel = opt_it->max_accel;
        sel.metrics_known = true;
        return sel;
    }

    // CSV curve with no console counterpart: report what the curve knows and
    // leave the metrics the console owns unknown rather than inventing them.
    sel.type = curve.name;
    sel.frequency = curve.frequency;
    sel.vibrations = 0.0f;
    sel.max_accel = 0.0f;
    sel.metrics_known = false;
    return sel;
}

} // namespace helix::calibration
