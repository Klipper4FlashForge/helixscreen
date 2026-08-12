// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "temp_graph_tooltip.h"

#include "temp_graph_internal.h"

#include <spdlog/spdlog.h>

#include <cstdint>
#include <limits>
#include <new>

// State definition. Kept out of ui_temp_graph.h on purpose.
struct temp_graph_tooltip_t {
    bool has_pin = false;
    helix::temp_graph_internal::TempGraphHit pin{};
};

namespace helix::temp_graph_internal {

int16_t target_deci_at(const ui_temp_series_meta_t* meta, int point_count, int logical_index) {
    if (!meta || !meta->target_deci_buf || meta->target_head <= 0) {
        return 0;
    }
    const int lead = point_count - meta->target_head; // chart slots with no target entry
    const int t_idx = logical_index - lead;
    if (t_idx < 0 || t_idx >= meta->target_head) {
        return 0;
    }
    return meta->target_deci_buf[t_idx];
}

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
            best.deci_target = target_deci_at(meta, pc, i);
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

static void tooltip_press_cb(lv_event_t* e) {
    auto* graph = static_cast<ui_temp_graph_t*>(lv_event_get_user_data(e));
    if (!ui_temp_graph_is_valid(graph) || !graph->tooltip) {
        return;
    }
    lv_indev_t* indev = lv_indev_active();
    if (!indev) {
        return;
    }
    lv_point_t p;
    lv_indev_get_point(indev, &p);

    auto hit = tooltip_hit_test(graph, p.x, p.y);
    if (hit.has_value()) {
        temp_graph_tooltip_pin(graph, *hit);
    } else {
        temp_graph_tooltip_clear(graph); // tap-away dismisses
    }
}

void temp_graph_tooltip_pin(ui_temp_graph_t* graph, const TempGraphHit& hit) {
    if (!ui_temp_graph_is_valid(graph) || !graph->tooltip) {
        return;
    }
    graph->tooltip->pin = hit;
    graph->tooltip->has_pin = true;
    spdlog::debug("[TempGraph] Caption pinned: series={} idx={} {}d {}ms", hit.series_id,
                  hit.logical_index, hit.deci_temp, hit.timestamp_ms);
    lv_obj_invalidate(graph->chart);
}

const TempGraphHit* temp_graph_tooltip_pinned(const ui_temp_graph_t* graph) {
    if (!graph || !graph->tooltip || !graph->tooltip->has_pin) {
        return nullptr;
    }
    return &graph->tooltip->pin;
}

void temp_graph_tooltip_clear(ui_temp_graph_t* graph) {
    if (!graph || !graph->tooltip || !graph->tooltip->has_pin) {
        return;
    }
    graph->tooltip->has_pin = false;
    if (graph->chart) {
        lv_obj_invalidate(graph->chart);
    }
}

void temp_graph_tooltip_on_sample_pushed(ui_temp_graph_t* graph, int series_id) {
    const TempGraphHit* pin = temp_graph_tooltip_pinned(graph);
    if (!pin || pin->series_id != series_id) {
        return;
    }
    if (graph->tooltip->pin.logical_index <= 0) {
        temp_graph_tooltip_clear(graph); // scrolled off the left edge
        return;
    }
    graph->tooltip->pin.logical_index--;
    lv_obj_invalidate(graph->chart);
}

void temp_graph_tooltip_on_series_hidden(ui_temp_graph_t* graph, int series_id) {
    const TempGraphHit* pin = temp_graph_tooltip_pinned(graph);
    if (pin && pin->series_id == series_id) {
        temp_graph_tooltip_clear(graph);
    }
}

void temp_graph_tooltip_destroy(ui_temp_graph_t* graph) {
    if (!graph || !graph->tooltip) {
        return;
    }
    delete graph->tooltip;
    graph->tooltip = nullptr;
}

} // namespace helix::temp_graph_internal

void ui_temp_graph_set_tooltip_enabled(ui_temp_graph_t* graph, bool enabled) {
    if (!ui_temp_graph_is_valid(graph)) {
        return;
    }
    if (enabled == (graph->tooltip != nullptr)) {
        return;
    }
    if (enabled) {
        graph->tooltip = new (std::nothrow) temp_graph_tooltip_t();
        if (!graph->tooltip) {
            spdlog::error("[TempGraph] Failed to allocate tooltip state");
            return;
        }
        lv_obj_add_flag(graph->chart, LV_OBJ_FLAG_CLICKABLE);
        // CLICKED (release without scroll), not PRESSED, so a scroll gesture that
        // happens to begin over the chart does not raise a caption.
        // DECLARATIVE_OK: the chart is a C++-created widget with no XML layer,
        // and the handler needs the raw indev coordinates.
        lv_obj_add_event_cb(graph->chart, helix::temp_graph_internal::tooltip_press_cb,
                            LV_EVENT_CLICKED, graph);
    } else {
        helix::temp_graph_internal::temp_graph_tooltip_clear(graph);
        lv_obj_remove_event_cb_with_user_data(graph->chart,
                                              helix::temp_graph_internal::tooltip_press_cb, graph);
        lv_obj_remove_flag(graph->chart, LV_OBJ_FLAG_CLICKABLE);
        helix::temp_graph_internal::temp_graph_tooltip_destroy(graph);
    }
}

bool ui_temp_graph_tooltip_is_enabled(const ui_temp_graph_t* graph) {
    return graph && graph->tooltip != nullptr;
}
