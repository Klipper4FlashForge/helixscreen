// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file temp_graph_internal.h
 * @brief Internals of the temperature graph shared between ui_temp_graph.cpp,
 *        temp_graph_tooltip.cpp, and the unit tests.
 *
 * NOT part of the widget's public API (that is ui_temp_graph.h). Anything here
 * is an implementation detail two translation units happen to need in common.
 */

#pragma once

#include "ui_temp_graph.h"

#include <cstdint>
#include <utility>
#include <vector>

namespace helix::temp_graph_internal {

/// Geometry of the chart's content rect (inside padding) plus the deci-degree
/// Y range. Computed from the chart object directly (no draw context), so it is
/// valid from a draw callback, from an async recompute, and from a press handler.
struct temp_graph_geometry_t {
    int32_t cx1, cy1; ///< content top-left (absolute display coords)
    int32_t cw, ch;   ///< content width / height
    int32_t y_min, y_max;
    uint32_t point_count;
};

/// @return false when the chart is too small or the range is degenerate.
bool temp_graph_compute_geometry(ui_temp_graph_t* graph, temp_graph_geometry_t* g);

/// The time span the chart currently represents, plus the tick cadence shared
/// by the X-axis labels and the vertical grid lines.
struct temp_graph_time_axis_t {
    int64_t latest_ms;   ///< Time at the right edge
    int64_t leftmost_ms; ///< Time at the left edge
    int64_t total_ms;    ///< Full window width in ms
    int64_t interval_ms; ///< Spacing between labels / grid lines
    int64_t first_ms;    ///< First tick at or after the left edge
};

temp_graph_time_axis_t temp_graph_time_axis(const ui_temp_graph_t* graph);

/// Contiguous runs of non-zero target samples in target_deci_buf.
std::vector<std::pair<int, int>> segment_target_buf(const int16_t* buf, int count);

/// Runs of equal target value within [from, to).
std::vector<std::pair<int, int>> coalesce_target_runs(const int16_t* buf, int from, int to);

} // namespace helix::temp_graph_internal
