// Copyright (C) 2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <glm/glm.hpp>

namespace helix::gcode {

/**
 * @brief Clip the segment p1-p2 to the rectangle [0, max_x] x [0, max_y]
 *
 * Liang-Barsky: the visible parametric range of the segment is narrowed by
 * each of the four edges, so a partially visible line keeps its slope and a
 * line that crosses the rectangle's corner region without entering it is
 * rejected outright. Clamping endpoints does neither.
 *
 * @return false when nothing of the segment lies inside; p1/p2 are then
 *         unspecified. true leaves p1/p2 on the visible sub-segment.
 */
inline bool clip_line_to_rect(glm::vec2& p1, glm::vec2& p2, float max_x, float max_y) {
    float t0 = 0.0f;
    float t1 = 1.0f;
    const float dx = p2.x - p1.x;
    const float dy = p2.y - p1.y;

    // One edge: p is the direction's component against that edge, q the
    // signed distance inside it. p == 0 means parallel: visible iff inside.
    auto narrow = [&](float p, float q) {
        if (p == 0.0f) {
            return q >= 0.0f;
        }
        const float r = q / p;
        if (p < 0.0f) {
            if (r > t1) {
                return false;
            }
            if (r > t0) {
                t0 = r;
            }
        } else {
            if (r < t0) {
                return false;
            }
            if (r < t1) {
                t1 = r;
            }
        }
        return true;
    };

    if (!narrow(-dx, p1.x) || !narrow(dx, max_x - p1.x) || !narrow(-dy, p1.y) ||
        !narrow(dy, max_y - p1.y)) {
        return false;
    }

    const glm::vec2 start = p1;
    p2 = start + glm::vec2(dx, dy) * t1;
    p1 = start + glm::vec2(dx, dy) * t0;
    return true;
}

} // namespace helix::gcode
