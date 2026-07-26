// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_gcode_render_mode_policy.cpp
 * @brief Tests for the viewer's HELIX_GCODE_MODE override handling.
 *
 * These call the REAL helix::gcode_viewer::decide_render_mode() used by the
 * viewer constructor. The have_3d_renderer parameter is what makes this worth
 * testing: in the constructor that branch is an #ifdef, so the "asked for 3D on
 * a 2D-only build" path is unreachable from a 3D-enabled build and never got
 * exercised. Passing it as an argument covers both builds from one test binary.
 */

#include "gcode_render_mode_policy.h"

#include "../catch_amalgamated.hpp"

using helix::GcodeViewerRenderMode;
using helix::gcode_viewer::decide_render_mode;
using helix::gcode_viewer::RenderModeReason;

TEST_CASE("Render mode: unset env auto-detects", "[gcode_viewer][render_mode]") {
    for (bool have_3d : {true, false}) {
        CAPTURE(have_3d);
        auto d = decide_render_mode(nullptr, have_3d);
        CHECK(d.mode == GcodeViewerRenderMode::Auto);
        CHECK(d.reason == RenderModeReason::DefaultAuto);
    }
}

TEST_CASE("Render mode: HELIX_GCODE_MODE=3D forces the GLES renderer",
          "[gcode_viewer][render_mode]") {
    auto d = decide_render_mode("3D", /*have_3d_renderer*/ true);
    CHECK(d.mode == GcodeViewerRenderMode::Render3D);
    CHECK(d.reason == RenderModeReason::EnvForced3D);
}

TEST_CASE("Render mode: HELIX_GCODE_MODE=3D on a 2D-only build falls back",
          "[gcode_viewer][render_mode]") {
    // Built without ENABLE_3D_RENDERER: honoring the override would select a
    // renderer that does not exist, so it degrades to 2D and warns instead.
    auto d = decide_render_mode("3D", /*have_3d_renderer*/ false);
    CHECK(d.mode == GcodeViewerRenderMode::Layer2D);
    CHECK(d.reason == RenderModeReason::Env3DUnavailable);
}

TEST_CASE("Render mode: HELIX_GCODE_MODE=2D forces the layer renderer",
          "[gcode_viewer][render_mode]") {
    for (bool have_3d : {true, false}) {
        CAPTURE(have_3d);
        auto d = decide_render_mode("2D", have_3d);
        CHECK(d.mode == GcodeViewerRenderMode::Layer2D);
        CHECK(d.reason == RenderModeReason::EnvForced2D);
    }
}

TEST_CASE("Render mode: unrecognized values degrade to 2D, not Auto",
          "[gcode_viewer][render_mode]") {
    // A typo must not silently behave as if nothing was set — that would hand
    // back Auto and hide the mistake. Note "3d"/"2d": matching is case-sensitive.
    for (const char* v : {"", "3d", "2d", "auto", "Auto", "gles", "true", "1"}) {
        CAPTURE(v);
        for (bool have_3d : {true, false}) {
            CAPTURE(have_3d);
            auto d = decide_render_mode(v, have_3d);
            CHECK(d.mode == GcodeViewerRenderMode::Layer2D);
            CHECK(d.reason == RenderModeReason::EnvUnrecognized);
        }
    }
}

TEST_CASE("Render mode: unset and empty are different answers", "[gcode_viewer][render_mode]") {
    // Guards the asymmetry documented on decide_render_mode(): only an absent
    // variable reaches Auto. Exporting HELIX_GCODE_MODE= is an explicit (if
    // empty) override and lands on 2D.
    CHECK(decide_render_mode(nullptr, true).mode == GcodeViewerRenderMode::Auto);
    CHECK(decide_render_mode("", true).mode == GcodeViewerRenderMode::Layer2D);
}
