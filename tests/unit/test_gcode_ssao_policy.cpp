// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_gcode_ssao_policy.cpp
 * @brief Tests for the viewer's enhanced-shading (SSAO) tiering decision.
 *
 * These call the REAL helix::gcode_viewer::decide_ssao_enabled() used by the
 * viewer constructor. The SSAO cache is a third full-canvas ARGB8888 buffer
 * held for the life of the viewer (~592KB at 368x402, more at larger sizes) —
 * on a 47MB AD5M that is the difference between rendering and OOM, so the
 * constrained tier must stay off unless explicitly forced.
 */

#include "gcode_ssao_policy.h"

#include "../catch_amalgamated.hpp"

using helix::gcode_viewer::decide_ssao_enabled;
using helix::gcode_viewer::SsaoReason;

TEST_CASE("SSAO: on by default when memory is not constrained", "[gcode_viewer][ssao]") {
    auto d = decide_ssao_enabled(/*constrained*/ false, /*env*/ nullptr);
    CHECK(d.enabled);
    CHECK(d.reason == SsaoReason::DefaultOn);
}

TEST_CASE("SSAO: off on a constrained device", "[gcode_viewer][ssao]") {
    // AD5M / K1C tier (< 256MB total RAM).
    auto d = decide_ssao_enabled(/*constrained*/ true, /*env*/ nullptr);
    CHECK_FALSE(d.enabled);
    CHECK(d.reason == SsaoReason::ConstrainedOff);
}

TEST_CASE("SSAO: HELIX_SSAO=1 forces it on over the constrained tier", "[gcode_viewer][ssao]") {
    // The override exists so the effect can be compared on the small devices.
    auto d = decide_ssao_enabled(/*constrained*/ true, "1");
    CHECK(d.enabled);
    CHECK(d.reason == SsaoReason::EnvForcedOn);
}

TEST_CASE("SSAO: HELIX_SSAO=0 forces it off on a roomy device", "[gcode_viewer][ssao]") {
    auto d = decide_ssao_enabled(/*constrained*/ false, "0");
    CHECK_FALSE(d.enabled);
    CHECK(d.reason == SsaoReason::EnvForcedOff);
}

TEST_CASE("SSAO: env overrides are redundant when they agree with the tier",
          "[gcode_viewer][ssao]") {
    // Same result, but the reason must report the override so the startup log
    // says why rather than implying the tier decided.
    auto on = decide_ssao_enabled(/*constrained*/ false, "1");
    CHECK(on.enabled);
    CHECK(on.reason == SsaoReason::EnvForcedOn);

    auto off = decide_ssao_enabled(/*constrained*/ true, "0");
    CHECK_FALSE(off.enabled);
    CHECK(off.reason == SsaoReason::EnvForcedOff);
}

TEST_CASE("SSAO: unrecognized env values fall through to the tier", "[gcode_viewer][ssao]") {
    // Only the exact strings "0" and "1" are honored — anything else (including
    // empty, "true", "yes") must not silently turn shading on for a 47MB board.
    for (const char* v : {"", "true", "yes", "on", "2", "01"}) {
        CAPTURE(v);
        auto constrained = decide_ssao_enabled(true, v);
        CHECK_FALSE(constrained.enabled);
        CHECK(constrained.reason == SsaoReason::ConstrainedOff);

        auto roomy = decide_ssao_enabled(false, v);
        CHECK(roomy.enabled);
        CHECK(roomy.reason == SsaoReason::DefaultOn);
    }
}
