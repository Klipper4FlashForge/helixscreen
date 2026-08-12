// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file temp_graph_tooltip.h
 * @brief Tap-to-caption for the temperature graph.
 *
 * Opt-in per graph instance. Only the full-screen TempGraphOverlay enables it:
 * the home-panel mini graph already uses a tap to OPEN that overlay
 * (src/ui/panel_widgets/temp_graph_widget.cpp), so a tooltip enabled globally
 * would collide with that gesture.
 */

#pragma once

#include "ui_temp_graph.h"

#include <cstdint>
#include <optional>

namespace helix::temp_graph_internal {

/// Tap radius in pixels. Trades "hard to hit a 2px line with a fingertip"
/// against "hard to dismiss because nothing is ever a miss". Tune on a real
/// 480x272 panel.
constexpr int32_t TEMP_GRAPH_TOOLTIP_HIT_RADIUS_PX = 28;

/// A resolved tap: one plotted sample of one visible series.
struct TempGraphHit {
    int series_id = -1;
    int logical_index = -1;  ///< 0 = oldest slot, point_count-1 = newest
    int32_t deci_temp = 0;   ///< value x 10
    int16_t deci_target = 0; ///< target x 10 in effect at that sample; 0 = off
    int64_t timestamp_ms = 0;
};

/**
 * Target in effect at a chart sample, in deci-degrees. 0 = heater off / none.
 *
 * The two buffers are indexed differently and nothing else in the codebase says
 * so. The chart is circular via start_point; target_deci_buf is linear, oldest
 * at [0], target_head valid entries, shift-left on overflow. push_target_sample
 * appends one entry per sample, so the buffer always holds the newest
 * target_head samples and they align with the LAST target_head chart slots:
 * chart logical index i maps to target_deci_buf[i - (point_count - target_head)].
 *
 * Earlier chart slots have no target entry and return 0. They hold
 * LV_CHART_POINT_NONE in array mode, or synthetic copies of the first reading
 * in push mode (which backfills the whole buffer on the first value).
 *
 * Getting this wrong is silently wrong by up to a full window for the first 20
 * minutes after launch, then quietly correct once the buffer fills.
 */
int16_t target_deci_at(const ui_temp_series_meta_t* meta, int point_count, int logical_index);

/**
 * Resolve a tap in absolute display coordinates to the nearest plotted sample.
 *
 * Considers only visible series and non-LV_CHART_POINT_NONE slots. Ties break
 * to the lowest series index, then the lowest logical index, so results are
 * deterministic under test.
 *
 * @return nullopt when nothing lies within TEMP_GRAPH_TOOLTIP_HIT_RADIUS_PX.
 *         That is also the caller's signal to dismiss an open caption.
 */
std::optional<TempGraphHit> tooltip_hit_test(ui_temp_graph_t* graph, int32_t x, int32_t y);

/// Pin a resolved hit as the displayed caption, replacing any current one.
void temp_graph_tooltip_pin(ui_temp_graph_t* graph, const TempGraphHit& hit);

/// @return the pinned hit, or nullptr when nothing is displayed.
const TempGraphHit* temp_graph_tooltip_pinned(const ui_temp_graph_t* graph);

/// Dismiss any pinned caption. Safe when the tooltip is disabled.
void temp_graph_tooltip_clear(ui_temp_graph_t* graph);

/// Called after a sample is pushed to `series_id`. Walks the pin one slot left
/// when it belongs to that series, dismissing it once it falls off the edge.
/// Per-series because each lv_chart_series_t carries its own start_point.
void temp_graph_tooltip_on_sample_pushed(ui_temp_graph_t* graph, int series_id);

/// Called when `series_id` is hidden. Dismisses the caption if it was pinned there.
void temp_graph_tooltip_on_series_hidden(ui_temp_graph_t* graph, int series_id);

/// Free tooltip state. Called from ui_temp_graph_destroy.
void temp_graph_tooltip_destroy(ui_temp_graph_t* graph);

} // namespace helix::temp_graph_internal
