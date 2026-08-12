// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "temp_graph_tooltip.h"

#include "temp_graph_internal.h"

#include <cstdint>
#include <limits>

namespace helix::temp_graph_internal {

std::optional<TempGraphHit> tooltip_hit_test(ui_temp_graph_t* graph, int32_t x, int32_t y) {
    if (!ui_temp_graph_is_valid(graph)) {
        return std::nullopt;
    }

    temp_graph_geometry_t geo{};
    if (!temp_graph_compute_geometry(graph, &geo)) {
        return std::nullopt;
    }

    const int32_t pc = static_cast<int32_t>(geo.point_count);
    const int32_t floor_y = geo.cy1 + geo.ch;
    const temp_graph_time_axis_t axis = temp_graph_time_axis(graph);

    int64_t best_d2 = std::numeric_limits<int64_t>::max();
    TempGraphHit best;
    bool found = false;

    // Ascending series order, then ascending logical index, gives the documented
    // deterministic tie-break for free (strict < never displaces an equal).
    for (int s = 0; s < UI_TEMP_GRAPH_MAX_SERIES; s++) {
        ui_temp_series_meta_t* meta = &graph->series_meta[s];
        if (!meta->chart_series || !meta->visible) {
            continue;
        }
        int32_t* y_data = lv_chart_get_y_array(graph->chart, meta->chart_series);
        if (!y_data) {
            continue;
        }
        // SHIFT mode is circular, not a memmove: lv_chart_set_next_value writes at
        // start_point and advances it (lv_chart.c:694-696). Logical index pc-1 is
        // the newest sample; leading logical slots stay POINT_NONE until full.
        const uint32_t sp = lv_chart_get_x_start_point(graph->chart, meta->chart_series);

        for (int32_t i = 0; i < pc; i++) {
            const int32_t v = y_data[(sp + i) % pc];
            if (v == LV_CHART_POINT_NONE) {
                continue;
            }
            // Same forward mapping the gradient walk draws with. Deriving this
            // any other way puts the marker dot visibly off the line.
            const int32_t px = geo.cx1 + i * (geo.cw - 1) / (pc - 1);
            const int32_t py = floor_y - lv_map(v, geo.y_min, geo.y_max, 0, geo.ch);

            const int64_t dx = px - x;
            const int64_t dy = py - y;
            const int64_t d2 = dx * dx + dy * dy;
            if (d2 >= best_d2) {
                continue;
            }

            best_d2 = d2;
            best.series_id = meta->id;
            best.logical_index = i;
            best.deci_temp = v;
            best.deci_target = 0; // populated in Task 3
            // True sample time, not the axis-label mapping. The axis spreads
            // total_ms (= pc * 3s) across pc-1 gaps, so the two differ by under
            // one sample interval; the caption describes an actual sample.
            best.timestamp_ms = axis.latest_ms - static_cast<int64_t>(pc - 1 - i) *
                                                     UI_TEMP_GRAPH_SAMPLE_INTERVAL_SEC * 1000;
            found = true;
        }
    }

    constexpr int64_t r = TEMP_GRAPH_TOOLTIP_HIT_RADIUS_PX;
    if (!found || best_d2 > r * r) {
        return std::nullopt;
    }
    return best;
}

} // namespace helix::temp_graph_internal
