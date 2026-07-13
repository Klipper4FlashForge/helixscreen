// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_filename_utils.h"

#include <string>

#include "../catch_amalgamated.hpp"

using helix::gcode::is_native_3mf_shadow;

// =============================================================================
// is_native_3mf_shadow() - QIDI native-3MF shadow G-code detection
// =============================================================================

TEST_CASE("is_native_3mf_shadow() accepts valid shadow names", "[filename_utils][qidi]") {
    REQUIRE(is_native_3mf_shadow("shadow_native_plate_1.gcode"));
    REQUIRE(is_native_3mf_shadow("shadow_native_plate_12.gcode"));
    REQUIRE(is_native_3mf_shadow("shadow_native_plate_007.gcode"));
    // Plate id need not be numeric - any non-empty middle is accepted.
    REQUIRE(is_native_3mf_shadow("shadow_native_plate_A.gcode"));
}

TEST_CASE("is_native_3mf_shadow() requires a non-empty plate id", "[filename_utils][qidi]") {
    // Prefix directly followed by suffix leaves no plate id in between.
    REQUIRE_FALSE(is_native_3mf_shadow("shadow_native_plate_.gcode"));
}

TEST_CASE("is_native_3mf_shadow() rejects wrong prefix", "[filename_utils][qidi]") {
    REQUIRE_FALSE(is_native_3mf_shadow("native_plate_1.gcode"));
    // Prefix must be at position 0, not embedded.
    REQUIRE_FALSE(is_native_3mf_shadow("foo_shadow_native_plate_1.gcode"));
    REQUIRE_FALSE(is_native_3mf_shadow("shadow_plate_1.gcode"));
}

TEST_CASE("is_native_3mf_shadow() rejects wrong suffix", "[filename_utils][qidi]") {
    REQUIRE_FALSE(is_native_3mf_shadow("shadow_native_plate_1.gco"));
    REQUIRE_FALSE(is_native_3mf_shadow("shadow_native_plate_1.txt"));
    REQUIRE_FALSE(is_native_3mf_shadow("shadow_native_plate_1"));
    // The active .3mf name itself must not match.
    REQUIRE_FALSE(is_native_3mf_shadow("MyModel.3mf"));
}

TEST_CASE("is_native_3mf_shadow() is case-sensitive", "[filename_utils][qidi]") {
    REQUIRE_FALSE(is_native_3mf_shadow("SHADOW_NATIVE_PLATE_1.GCODE"));
    REQUIRE_FALSE(is_native_3mf_shadow("shadow_native_plate_1.GCODE"));
}

TEST_CASE("is_native_3mf_shadow() handles empty and short input", "[filename_utils][qidi]") {
    REQUIRE_FALSE(is_native_3mf_shadow(""));
    REQUIRE_FALSE(is_native_3mf_shadow(".gcode"));
    REQUIRE_FALSE(is_native_3mf_shadow("shadow_native_plate_"));
}
