// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "grid_layout.h"

#include "layout_manager.h"
#include "theme_manager.h"

#include <spdlog/spdlog.h>

#include <algorithm>

namespace helix {

namespace {
constexpr int GRID_TRACK_SCAN_MAX = 256;
} // namespace

static int clamp_bp(UiBreakpoint bp) {
    int32_t v = to_int(bp);
    if (v < 0)
        return 0;
    if (v >= GridLayout::NUM_BREAKPOINTS)
        return GridLayout::NUM_BREAKPOINTS - 1;
    return v;
}

// ---------------------------------------------------------------------------
// Static helpers
// ---------------------------------------------------------------------------

GridDimensions GridLayout::get_dimensions(UiBreakpoint bp) {
    const int track = GRID_CELL[static_cast<size_t>(clamp_bp(bp))];
    auto& lm = LayoutManager::instance();
    // Floored to a whole number of cells: a track is half a cell, so an odd
    // track count leaves a final half-cell that no whole-cell widget can ever
    // occupy. Dropping it also spreads less leftover across the remaining
    // tracks, which is what keeps the cell square.
    auto tracks = [track](int extent) {
        const int n = std::clamp(extent / track, MIN_TRACKS, MAX_TRACKS);
        return n - (n % GridLayout::TRACKS_PER_CELL);
    };
    return {tracks(lm.width()), tracks(lm.height())};
}

int GridLayout::get_cols(UiBreakpoint bp) {
    return get_dimensions(bp).cols;
}

int GridLayout::get_rows(UiBreakpoint bp) {
    return get_dimensions(bp).rows;
}

std::vector<int32_t> GridLayout::make_col_dsc(UiBreakpoint bp) {
    int ncols = get_cols(bp);
    std::vector<int32_t> dsc;
    dsc.reserve(static_cast<size_t>(ncols) + 1);
    for (int i = 0; i < ncols; ++i) {
        dsc.push_back(LV_GRID_FR(1));
    }
    dsc.push_back(LV_GRID_TEMPLATE_LAST);
    return dsc;
}

std::vector<int32_t> GridLayout::make_row_dsc(UiBreakpoint bp) {
    int nrows = get_rows(bp);
    std::vector<int32_t> dsc;
    dsc.reserve(static_cast<size_t>(nrows) + 1);
    for (int i = 0; i < nrows; ++i) {
        dsc.push_back(LV_GRID_FR(1));
    }
    dsc.push_back(LV_GRID_TEMPLATE_LAST);
    return dsc;
}

// ---------------------------------------------------------------------------
// Instance methods
// ---------------------------------------------------------------------------

GridLayout::GridLayout(UiBreakpoint bp) : breakpoint_(bp) {}

GridDimensions GridLayout::dimensions() const {
    return get_dimensions(breakpoint_);
}

int GridLayout::cols() const {
    return get_cols(breakpoint_);
}

int GridLayout::rows() const {
    return get_rows(breakpoint_);
}

bool GridLayout::is_occupied(int col, int row) const {
    for (const auto& p : placements_) {
        if (col >= p.col && col < p.col + p.colspan && row >= p.row && row < p.row + p.rowspan) {
            return true;
        }
    }
    return false;
}

bool GridLayout::can_place(int col, int row, int colspan, int rowspan) const {
    int ncols = cols();
    int nrows = rows();

    // Bounds check
    if (col < 0 || row < 0 || colspan <= 0 || rowspan <= 0)
        return false;
    if (col + colspan > ncols || row + rowspan > nrows)
        return false;

    // Collision check
    for (int c = col; c < col + colspan; ++c) {
        for (int r = row; r < row + rowspan; ++r) {
            if (is_occupied(c, r))
                return false;
        }
    }
    return true;
}

bool GridLayout::place(const GridPlacement& placement) {
    if (!can_place(placement.col, placement.row, placement.colspan, placement.rowspan)) {
        spdlog::debug("GridLayout: cannot place '{}' at ({},{}) span {}x{} in {}x{} grid",
                      placement.widget_id, placement.col, placement.row, placement.colspan,
                      placement.rowspan, cols(), rows());
        return false;
    }
    placements_.push_back(placement);
    return true;
}

bool GridLayout::remove(const std::string& widget_id) {
    auto it = std::find_if(placements_.begin(), placements_.end(),
                           [&](const GridPlacement& p) { return p.widget_id == widget_id; });
    if (it == placements_.end())
        return false;
    placements_.erase(it);
    return true;
}

std::optional<std::pair<int, int>> GridLayout::find_available(int colspan, int rowspan) const {
    int ncols = cols();
    int nrows = rows();

    // Scan top-to-bottom, left-to-right
    for (int r = 0; r <= nrows - rowspan; ++r) {
        for (int c = 0; c <= ncols - colspan; ++c) {
            if (can_place(c, r, colspan, rowspan)) {
                return std::make_pair(c, r);
            }
        }
    }
    return std::nullopt;
}

std::optional<std::pair<int, int>> GridLayout::find_available_bottom(int colspan,
                                                                     int rowspan) const {
    int ncols = cols();
    int nrows = rows();

    // Scan bottom-to-top, right-to-left
    for (int r = nrows - rowspan; r >= 0; --r) {
        for (int c = ncols - colspan; c >= 0; --c) {
            if (can_place(c, r, colspan, rowspan)) {
                return std::make_pair(c, r);
            }
        }
    }
    return std::nullopt;
}

GridLayout::SpanPlacement GridLayout::find_available_bottom_min(int min_colspan,
                                                                int min_rowspan) const {
    SpanPlacement out;

    // A span of 0 means "unset" in PanelWidgetDef; treat it as 1 cell.
    const int want_cols = std::max(1, min_colspan);
    const int want_rows = std::max(1, min_rowspan);

    // Larger than the whole grid even at the declared minimum: no arrangement of
    // free cells could ever hold it, so this is not a "grid full" condition.
    if (want_cols > cols() || want_rows > rows()) {
        out.failure = PlacementFailure::TooLargeForGrid;
        return out;
    }

    if (auto pos = find_available_bottom(want_cols, want_rows)) {
        return {pos->first, pos->second, want_cols, want_rows, PlacementFailure::None};
    }

    // It fits the grid's dimensions; there is simply no free run of cells left.
    out.failure = PlacementFailure::GridFull;
    return out;
}

const GridPlacement* GridLayout::find_placement(const std::string& widget_id) const {
    auto it = std::find_if(placements_.begin(), placements_.end(),
                           [&](const GridPlacement& p) { return p.widget_id == widget_id; });
    return it == placements_.end() ? nullptr : &*it;
}

GridPlacement* GridLayout::find_placement_mut(const std::string& widget_id) {
    auto it = std::find_if(placements_.begin(), placements_.end(),
                           [&](const GridPlacement& p) { return p.widget_id == widget_id; });
    return it == placements_.end() ? nullptr : &*it;
}

bool GridLayout::strip_is_free(int col, int row, int colspan, int rowspan) const {
    if (col < 0 || row < 0 || colspan <= 0 || rowspan <= 0)
        return false;
    if (col + colspan > cols() || row + rowspan > rows())
        return false;
    for (int c = col; c < col + colspan; ++c) {
        for (int r = row; r < row + rowspan; ++r) {
            if (is_occupied(c, r))
                return false;
        }
    }
    return true;
}

bool GridLayout::grow_once(const std::string& widget_id, int target_colspan, int target_rowspan) {
    GridPlacement* p = find_placement_mut(widget_id);
    if (!p)
        return false;

    // The grid is a hard ceiling on top of the widget's own target.
    const int target_cols = std::min(std::max(1, target_colspan), cols());
    const int target_rows = std::min(std::max(1, target_rowspan), rows());

    // RIGHT then DOWN keep the origin fixed; LEFT then UP are the fallback for a
    // widget already against an edge. See grow_once() in grid_layout.h.
    if (p->colspan < target_cols && strip_is_free(p->col + p->colspan, p->row, 1, p->rowspan)) {
        ++p->colspan;
        return true;
    }
    if (p->rowspan < target_rows && strip_is_free(p->col, p->row + p->rowspan, p->colspan, 1)) {
        ++p->rowspan;
        return true;
    }
    if (p->colspan < target_cols && strip_is_free(p->col - 1, p->row, 1, p->rowspan)) {
        --p->col;
        ++p->colspan;
        return true;
    }
    if (p->rowspan < target_rows && strip_is_free(p->col, p->row - 1, p->colspan, 1)) {
        --p->row;
        ++p->rowspan;
        return true;
    }
    return false;
}

int GridLayout::grow_to_targets(const std::vector<GrowthTarget>& targets) {
    int steps = 0;
    // Terminates: every step consumes at least one previously-free cell, and the
    // grid holds finitely many.
    for (bool progress = true; progress;) {
        progress = false;
        for (const auto& t : targets) {
            if (grow_once(t.widget_id, t.colspan, t.rowspan)) {
                progress = true;
                ++steps;
            }
        }
    }
    return steps;
}

const char* GridLayout::failure_text(PlacementFailure reason) {
    switch (reason) {
    case PlacementFailure::GridFull:
        return "grid full";
    case PlacementFailure::TooLargeForGrid:
        return "too big for this screen";
    case PlacementFailure::None:
        break;
    }
    return "placed";
}

std::pair<std::vector<GridPlacement>, std::vector<GridPlacement>>
GridLayout::filter_for_breakpoint(UiBreakpoint bp, const std::vector<GridPlacement>& placements) {
    auto dims = get_dimensions(bp);
    std::vector<GridPlacement> fits;
    std::vector<GridPlacement> does_not_fit;

    for (const auto& p : placements) {
        if (p.col >= 0 && p.row >= 0 && p.colspan > 0 && p.rowspan > 0 &&
            p.col + p.colspan <= dims.cols && p.row + p.rowspan <= dims.rows) {
            fits.push_back(p);
        } else {
            does_not_fit.push_back(p);
        }
    }
    return {std::move(fits), std::move(does_not_fit)};
}

void GridLayout::clear() {
    placements_.clear();
}

int GridLayout::gutter_px() {
    return theme_manager_get_spacing("space_xs");
}

int grid_count_tracks(const int32_t* dsc) {
    if (dsc == nullptr) {
        return 0;
    }
    for (int i = 0; i < GRID_TRACK_SCAN_MAX; ++i) {
        if (dsc[i] == LV_GRID_TEMPLATE_LAST) {
            return i;
        }
    }
    return 0;
}

CellMetrics grid_cell_metrics(int content_w, int content_h, int cols, int rows, int gutter) {
    CellMetrics m{};
    m.cols = cols;
    m.rows = rows;
    m.gutter = std::max(0, gutter);

    if (cols > 0 && content_w > 0) {
        float usable = static_cast<float>(content_w) - static_cast<float>(cols - 1) * m.gutter;
        m.cell_w = (usable > 0.0f) ? usable / static_cast<float>(cols) : 0.0f;
    }
    if (rows > 0 && content_h > 0) {
        float usable = static_cast<float>(content_h) - static_cast<float>(rows - 1) * m.gutter;
        m.cell_h = (usable > 0.0f) ? usable / static_cast<float>(rows) : 0.0f;
    }
    return m;
}

} // namespace helix
