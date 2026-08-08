// tests/test_helpers/grid_edit_mode_test_access.h
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "grid_edit_mode.h"

namespace helix {

/// Reads the snap target GridEditMode computed for the current drag.
///
/// snap_preview_col_/row_ are the direct output of handle_drag_move()'s cell
/// computation and the values handle_drag_end() commits to config, so they are
/// what a drag test needs to see. Both reset to -1 on drag start and on exit.
struct GridEditModeTestAccess {
    static int snap_col(const GridEditMode& em) {
        return em.snap_preview_col_;
    }
    static int snap_row(const GridEditMode& em) {
        return em.snap_preview_row_;
    }

    /// The lattice overlay, so a test can confirm it is actually rebuilt (a
    /// new lv_obj_t*, not the one from before the selection changed) and that
    /// its child count matches the dot lattice the current selection should draw.
    static lv_obj_t* dots_overlay(const GridEditMode& em) {
        return em.dots_overlay_;
    }
};

} // namespace helix
