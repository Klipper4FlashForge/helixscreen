// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui_breakpoint.h"

#include "lvgl/lvgl.h"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace helix {

/// Grid dimensions for a specific breakpoint
struct GridDimensions {
    int cols;
    int rows;
};

/// Pixel geometry of one grid track pair, gutters accounted for.
struct CellMetrics {
    float cell_w; ///< Width of a single column track, gutters excluded
    float cell_h; ///< Height of a single row track, gutters excluded
    int gutter;   ///< Inter-track gap in px, same on both axes
    int cols;
    int rows;
};

/// Track geometry for a cols x rows grid inside a content rectangle.
///
/// LVGL distributes LV_GRID_FR(1) tracks as (content - (n-1)*gutter) / n. The
/// content rectangle from lv_obj_get_content_coords() excludes padding and
/// border but NOT these inter-track gaps, so dividing content by the track
/// count overstates every track and the error compounds across the axis.
///
/// Degenerate inputs (zero or negative counts, gutters wider than the content)
/// yield zero-size tracks rather than negative or infinite ones.
CellMetrics grid_cell_metrics(int content_w, int content_h, int cols, int rows, int gutter);

/// Offset of track `index` from the start of the content area, in px.
inline float grid_track_origin(float cell, int gutter, int index) {
    return static_cast<float>(index) * (cell + static_cast<float>(gutter));
}

/// Pixel extent of `span` consecutive tracks, including the gutters between
/// them but not the ones on either outside edge.
inline float grid_track_extent(float cell, int gutter, int span) {
    if (span <= 0) {
        return 0.0f;
    }
    return static_cast<float>(span) * cell + static_cast<float>(span - 1) * gutter;
}

/// A widget placement on the grid
struct GridPlacement {
    std::string widget_id;
    int col;
    int row;
    int colspan;
    int rowspan;
};

/// Manages grid layout for the home panel dashboard.
/// Handles grid descriptor generation, widget placement, collision detection,
/// and breakpoint adaptation.
class GridLayout {
  public:
    /// Number of defined breakpoints
    static constexpr int NUM_BREAKPOINTS = 6;

    /// Target cell WIDTH in pixels. ULTRAWIDE and PORTRAIT divide screen width
    /// by this to derive a column count.
    static constexpr int TARGET_CELL_W_PX = 160;

    /// Target cell HEIGHT in pixels. PORTRAIT divides screen height by this to
    /// derive a row count.
    ///
    /// 120, not 160: ultrawide has always kept the fixed 4-row table on a 480px
    /// panel, i.e. a 120px row, so 120 is the row height the dashboard is
    /// actually authored against. Portrait used to reuse the 160px WIDTH target
    /// for both axes, which made its rows 164px tall — the tall screen got
    /// fewer, chunkier cells than the wide one (#1215). A 320x1480 panel now
    /// yields 12 rows instead of 9.
    static constexpr int TARGET_CELL_H_PX = 120;

    /// Clamp range for dynamically computed grid dimensions
    static constexpr int MIN_DYNAMIC_COLS = 4;
    static constexpr int MAX_DYNAMIC_COLS = 16;
    static constexpr int MIN_DYNAMIC_ROWS = 3;

    /// Row cap for dynamically computed grids.
    ///
    /// 16 mirrors MAX_DYNAMIC_COLS and covers the tallest panels in the wild at
    /// the 120px row target: 320x1480 lands on 12, and a 480x1920 ultratall
    /// lands exactly on 16. Past that a taller screen gets taller cells rather
    /// than more tracks — every extra row is a real LVGL grid track that costs
    /// descriptor memory and a layout pass whether or not a widget occupies it,
    /// and a 17th row on a 2-column grid buys cells no widget is authored for.
    static constexpr int MAX_DYNAMIC_ROWS = 16;

    /// Column floor for portrait, below the landscape floor of 4.
    ///
    /// A portrait panel is narrow by definition: 320px against MIN_DYNAMIC_COLS
    /// would give 80px cells, half of TARGET_CELL_W_PX, and every widget that
    /// branches on `colspan >= 2` would read as compact no matter how much of
    /// the screen it actually covers. Two columns keeps cells at their intended
    /// size and lets a full-width widget genuinely be full width.
    static constexpr int MIN_PORTRAIT_COLS = 2;

    /// Get grid dimensions for a given breakpoint
    static GridDimensions get_dimensions(UiBreakpoint bp);

    /// Get the number of columns for a breakpoint
    static int get_cols(UiBreakpoint bp);

    /// Get the number of rows for a breakpoint
    static int get_rows(UiBreakpoint bp);

    /// Inter-track gap the home grid is built with, in px.
    ///
    /// Single source of truth for the spacing token: PanelWidgetManager sets the
    /// container's pad_column/pad_row from this, and every consumer that
    /// converts pixels to cells reads the same value. Returns 0 when the theme
    /// is not initialized, which is also the correct answer for a grid that has
    /// not been styled yet.
    static int gutter_px();

    /// Generate LVGL column descriptor array for a breakpoint.
    /// Returns vector of int32_t values terminated by LV_GRID_TEMPLATE_LAST.
    static std::vector<int32_t> make_col_dsc(UiBreakpoint bp);

    /// Generate LVGL row descriptor array for a breakpoint.
    /// Returns vector of int32_t values terminated by LV_GRID_TEMPLATE_LAST.
    static std::vector<int32_t> make_row_dsc(UiBreakpoint bp);

    /// Construct a GridLayout for a specific breakpoint
    explicit GridLayout(UiBreakpoint bp);

    /// Get the breakpoint this layout was constructed for
    UiBreakpoint breakpoint() const {
        return breakpoint_;
    }

    /// Get grid dimensions
    GridDimensions dimensions() const;
    int cols() const;
    int rows() const;

    /// Try to place a widget. Returns true if placed successfully.
    /// Fails if placement overlaps existing widgets or is out of bounds.
    bool place(const GridPlacement& placement);

    /// Remove a widget by ID. Returns true if found and removed.
    bool remove(const std::string& widget_id);

    /// Check if a placement would be valid (no collision, in bounds)
    bool can_place(int col, int row, int colspan, int rowspan) const;

    /// Find first available position for a widget of given size.
    /// Scans top-to-bottom, left-to-right (row-major order).
    std::optional<std::pair<int, int>> find_available(int colspan, int rowspan) const;

    /// Find first available position scanning bottom-to-top, right-to-left.
    /// Used by auto-placement to pack widgets toward the bottom of the grid.
    std::optional<std::pair<int, int>> find_available_bottom(int colspan, int rowspan) const;

    /// Why a flexible placement attempt failed.
    enum class PlacementFailure {
        None,            ///< Placed successfully
        GridFull,        ///< Fits the grid, but no free run of cells is left
        TooLargeForGrid, ///< Exceeds the grid even at the declared minimum span
    };

    /// Outcome of find_available_bottom_min(): position plus the span that was
    /// actually granted, or the reason nothing could be granted.
    struct SpanPlacement {
        int col = -1;
        int row = -1;
        int colspan = 0;
        int rowspan = 0;
        PlacementFailure failure = PlacementFailure::TooLargeForGrid;

        bool placed() const {
            return failure == PlacementFailure::None;
        }
    };

    /// Bottom-packed placement at a widget's DECLARED MINIMUM span.
    ///
    /// A widget authored for the 6-column landscape grid (tips: colspan 4)
    /// cannot exist in a 2-column portrait grid, but it advertises a minimum it
    /// *can* live at. Auto-placement asks for the minimum so widget count is
    /// maximised, then grows survivors back with grow_to_targets(). Handing out
    /// the largest span that fit instead let one widget eat the cells its
    /// neighbours needed and disabled them with "grid full" (#1216).
    ///
    /// `failure` distinguishes "no space left" from "larger than the whole
    /// grid" so the caller can say which condition actually failed.
    SpanPlacement find_available_bottom_min(int min_colspan, int min_rowspan) const;

    /// Short, user-facing phrase naming a placement failure. Kept next to the
    /// enum so the toast and the log cannot drift apart.
    static const char* failure_text(PlacementFailure reason);

    /// A placed widget's growth goal — the span its definition authors, which
    /// is where grow_to_targets() tries to get it back to.
    struct GrowthTarget {
        std::string widget_id;
        int colspan;
        int rowspan;
    };

    /// Expand one already-placed widget by a single row or column toward
    /// `target_colspan` x `target_rowspan`. Returns true when the placement
    /// changed.
    ///
    /// Directions are tried in a fixed order — RIGHT, DOWN, LEFT, UP. Right and
    /// down come first because they keep the widget's (col,row) origin fixed,
    /// so growth does not reshuffle the grid; left and up are the fallback for
    /// a widget already against the right or bottom edge, which is where
    /// bottom-packed minimum placement puts most of them. The whole new strip
    /// must be free and in bounds, so a widget never overruns a neighbour.
    ///
    /// The target is a ceiling, never a floor: a widget already at or past its
    /// target does not move.
    bool grow_once(const std::string& widget_id, int target_colspan, int target_rowspan);

    /// Round-robin expansion toward every target: each pass offers every widget
    /// in `targets` ONE growth step, and passes repeat until one changes
    /// nothing. Round-robin rather than one-widget-at-a-time so a single large
    /// widget cannot absorb all the slack while the widget behind it stays at
    /// its minimum. Returns the number of growth steps applied.
    ///
    /// Deterministic: the direction order is fixed and `targets` is walked in
    /// the caller's order, so the same grid and the same list always produce the
    /// same layout.
    int grow_to_targets(const std::vector<GrowthTarget>& targets);

    /// Current placement for a widget id, or nullptr when it is not placed.
    const GridPlacement* find_placement(const std::string& widget_id) const;

    /// Get all current placements
    const std::vector<GridPlacement>& placements() const {
        return placements_;
    }

    /// Check which placements from a list fit within this layout's grid.
    /// Returns two vectors: (fits, does_not_fit)
    static std::pair<std::vector<GridPlacement>, std::vector<GridPlacement>>
    filter_for_breakpoint(UiBreakpoint bp, const std::vector<GridPlacement>& placements);

    /// Clear all placements
    void clear();

    /// Check if a cell is occupied by any existing placement
    bool is_occupied(int col, int row) const;

  private:
    /// True when every cell of the rectangle is in bounds and unoccupied.
    /// Growth checks the strip it is about to swallow, which never overlaps the
    /// growing widget's own cells, so no self-exclusion is needed.
    bool strip_is_free(int col, int row, int colspan, int rowspan) const;

    GridPlacement* find_placement_mut(const std::string& widget_id);

    UiBreakpoint breakpoint_;
    std::vector<GridPlacement> placements_;
};

} // namespace helix
