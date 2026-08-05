// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace helix {

/**
 * @brief Floor share of the stacked portrait column reserved for the canvas.
 *
 * The canvas and the profiles list are the two elastic blocks in the portrait
 * column. Without a floor, a long profile list plus a five-row info card can
 * squeeze the plot until it is unreadable — and an unreadable plot is the one
 * thing this panel exists to show. A cramped profiles list is merely a scroll.
 */
inline constexpr int32_t kBedMeshPortraitCanvasMinPct = 35;

/**
 * @brief Height for the portrait bed mesh canvas, or 0 for "cannot decide".
 *
 * Square is the target and the ceiling: a bed is a square-ish object and a
 * taller-than-wide plot buys no legibility while starving the profiles list.
 * When the column is too short for square, the canvas flattens rather than
 * pushing everything else off screen — a mesh squashed horizontally still
 * reads, one squashed vertically does not.
 *
 * @param band_w  Measured width of the canvas band.
 * @param avail_h Height the stacked column has left for the canvas.
 * @return Height in px, or 0 when either input is non-positive (the caller has
 *         not been laid out yet and should leave the XML default alone).
 */
constexpr int32_t bed_mesh_portrait_canvas_height(int32_t band_w, int32_t avail_h) {
    if (band_w <= 0 || avail_h <= 0) {
        return 0;
    }
    const int32_t square = band_w;
    // Square unless the column cannot afford it, then flatten.
    const int32_t fitted = (square <= avail_h) ? square : avail_h;
    // The floor is a share of the whole column, so on a very tall column it can
    // exceed the square itself (e.g. avail_h=2000 at 35% floors at 700, past a
    // 480-wide square). Capping the floor at `square` keeps square the ceiling
    // in every case, not just when the column is short — otherwise a roomy
    // column would inflate an already-fitted canvas past its own width, which
    // is the exact taller-than-square shape rule 2 exists to forbid.
    const int32_t floor_h = avail_h * kBedMeshPortraitCanvasMinPct / 100;
    const int32_t effective_floor = (floor_h < square) ? floor_h : square;
    return (fitted < effective_floor) ? effective_floor : fitted;
}

} // namespace helix
