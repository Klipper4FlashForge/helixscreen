// Copyright (C) 2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_gcode_line_clip.cpp
 * @brief Viewport clipping keeps a partially visible line on its own slope.
 *
 * Run with: ./build/bin/helix-tests "[gcode][clip]"
 *
 * Clamping both endpoints into the rectangle draws a different line: a segment
 * that crosses one corner region without entering the rectangle became a
 * diagonal across it, and a segment leaving through the top edge bent to hug
 * the edge (prestonbrown/helixscreen#1373).
 */

#include "gcode_line_clip.h"

#include "../catch_amalgamated.hpp"

using Catch::Approx;
using helix::gcode::clip_line_to_rect;

namespace {
constexpr float W = 100.0f;
constexpr float H = 50.0f;
} // namespace

TEST_CASE("clip: a fully visible segment is untouched", "[gcode][clip]") {
    glm::vec2 a{10.0f, 10.0f};
    glm::vec2 b{90.0f, 40.0f};
    REQUIRE(clip_line_to_rect(a, b, W, H));
    CHECK(a == glm::vec2{10.0f, 10.0f});
    CHECK(b == glm::vec2{90.0f, 40.0f});
}

TEST_CASE("clip: a segment entirely past one edge is rejected", "[gcode][clip]") {
    glm::vec2 a{-30.0f, 10.0f};
    glm::vec2 b{-5.0f, 40.0f};
    CHECK_FALSE(clip_line_to_rect(a, b, W, H));
}

TEST_CASE("clip: a segment that misses the corner is rejected, not bent across the view",
          "[gcode][clip]") {
    // Endpoints are outside on DIFFERENT sides (left, then above), so the
    // same-side test passes it; the line itself never enters the rectangle.
    glm::vec2 a{-10.0f, 45.0f};
    glm::vec2 b{5.0f, 60.0f};
    CHECK_FALSE(clip_line_to_rect(a, b, W, H));
}

TEST_CASE("clip: a segment leaving through an edge is cut on that edge, slope preserved",
          "[gcode][clip]") {
    // Slope 1: leaves the top edge (y = H) at x = 60.
    glm::vec2 a{20.0f, 10.0f};
    glm::vec2 b{80.0f, 70.0f};
    REQUIRE(clip_line_to_rect(a, b, W, H));
    CHECK(a == glm::vec2{20.0f, 10.0f});
    CHECK(b.x == Approx(60.0f));
    CHECK(b.y == Approx(H));
}

TEST_CASE("clip: a segment crossing the whole view is cut on both edges", "[gcode][clip]") {
    // Horizontal line at y = 25 from far left to far right.
    glm::vec2 a{-50.0f, 25.0f};
    glm::vec2 b{150.0f, 25.0f};
    REQUIRE(clip_line_to_rect(a, b, W, H));
    CHECK(a.x == Approx(0.0f));
    CHECK(b.x == Approx(W));
    CHECK(a.y == Approx(25.0f));
    CHECK(b.y == Approx(25.0f));
}

TEST_CASE("clip: a segment parallel to an edge and outside it is rejected", "[gcode][clip]") {
    glm::vec2 a{10.0f, -1.0f};
    glm::vec2 b{90.0f, -1.0f};
    CHECK_FALSE(clip_line_to_rect(a, b, W, H));
}

// ---------------------------------------------------------------------------
// The renderer has to route through the same clip, or the header above is a
// well-tested function nothing draws with.
// ---------------------------------------------------------------------------

#include "gcode_renderer.h"

namespace helix::gcode {
struct GCodeRendererTestAccess {
    static bool clip(const GCodeRenderer& r, glm::vec2& a, glm::vec2& b) {
        return r.clip_line_to_viewport(a, b);
    }
};
} // namespace helix::gcode
using helix::gcode::GCodeRenderer;
using helix::gcode::GCodeRendererTestAccess;

TEST_CASE("GCodeRenderer rejects a corner-missing segment instead of bending it", "[gcode][clip]") {
    GCodeRenderer renderer;
    renderer.set_viewport_size(100, 50);
    glm::vec2 a{-10.0f, 45.0f};
    glm::vec2 b{5.0f, 60.0f};
    CHECK_FALSE(GCodeRendererTestAccess::clip(renderer, a, b));

    glm::vec2 c{20.0f, 10.0f};
    glm::vec2 d{80.0f, 70.0f};
    REQUIRE(GCodeRendererTestAccess::clip(renderer, c, d));
    CHECK(d.x == Approx(60.0f));
    CHECK(d.y == Approx(50.0f));
}
