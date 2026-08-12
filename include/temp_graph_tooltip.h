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

} // namespace helix::temp_graph_internal
