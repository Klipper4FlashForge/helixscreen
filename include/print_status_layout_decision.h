// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>

namespace helix::ui {

/**
 * @brief Share of the stacked column portrait reserves for the preview.
 *
 * The gcode viewer / thumbnail is why this screen exists, so in a stacked
 * portrait layout it gets a floor rather than whatever the controls leave over.
 * One number drives both consumers below, so "how much of a portrait
 * print-status screen is preview" is a single decision with a single place to
 * change it.
 */
inline constexpr int32_t kPortraitPreviewReservePct = 40;

/**
 * @brief Vertical space the fan row may occupy, in px. Negative means no room.
 *
 * The landscape and portrait layouts are not two tunings of one rule; they are
 * different rules, because the container means different things.
 *
 * LANDSCAPE — `overlay_content` is a flex ROW and `controls_section` is
 * `height="100%"`. The column is a fixed budget and its children compete inside
 * it, so the budget is the column's own height minus what the children take.
 *
 * PORTRAIT — `overlay_content` is a flex COLUMN and `controls_section` is
 * `height="content"`. It has *no slack by construction*: its height is by
 * definition the sum of its children, so `controls_h - used` is approximately
 * zero (in fact slightly negative once the prospective fan row's gap is counted)
 * no matter how much screen is free. Feeding the landscape formula a portrait
 * layout therefore latches the fan row off permanently — the bug this exists to
 * prevent. The real competitor in portrait is the *preview*, which sits above
 * the controls with `flex_grow="1"` and absorbs whatever is left, so the budget
 * is the whole stacked column minus the preview's reserved floor.
 *
 * @param portrait   True when overlay_content is stacked (any portrait class).
 * @param controls_h Measured height of `controls_section`. Landscape only.
 * @param content_h  Measured height of `overlay_content`. Portrait only.
 * @param used       Summed heights of the visible controls children, including
 *                   inter-child gaps and one extra gap for the prospective fan
 *                   row.
 */
inline constexpr int32_t fan_row_budget(bool portrait, int32_t controls_h, int32_t content_h,
                                        int32_t used) {
    if (!portrait) {
        return controls_h - used;
    }
    const int32_t preview_floor = content_h * kPortraitPreviewReservePct / 100;
    return content_h - preview_floor - used;
}

/**
 * @brief Where the exclude-object side list sits over the print-status content.
 *
 * The list is a FLOATING child of `overlay_content`, so it overlays rather than
 * displaces. Its job is to cover the controls while the object map takes the
 * preview — which is a different edge in each layout.
 *
 * LANDSCAPE — controls are the right-hand column, historically 4/9 of the row,
 * so a 44%-wide list anchored right covers them almost exactly.
 *
 * PORTRAIT — controls are the bottom of a stack, so a 44%-wide list anchored
 * right covers the right 44% of *everything* and none of the controls fully.
 * The list goes full width, anchored to the bottom, taking the complement of the
 * preview reserve so it lands on the controls and leaves the map visible.
 */
struct SideListGeometry {
    int32_t width_pct;
    int32_t height_pct;
    bool anchor_bottom; ///< false = right edge (landscape), true = bottom (portrait)
};

inline constexpr SideListGeometry exclude_side_list_geometry(bool portrait) {
    // 44% ~= 4/9, the controls column's share of the landscape row.
    return portrait ? SideListGeometry{100, 100 - kPortraitPreviewReservePct, true}
                    : SideListGeometry{44, 100, false};
}

} // namespace helix::ui
