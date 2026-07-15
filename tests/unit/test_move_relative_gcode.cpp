// SPDX-License-Identifier: GPL-3.0-or-later
// tests/unit/test_move_relative_gcode.cpp
#include "../../include/moonraker_motion_api.h"

#include <limits>

#include "../catch_amalgamated.hpp"

TEST_CASE("generate_relative_move_gcode: XY combined on one G0 line", "[motion][gcode]") {
    CHECK(MoonrakerMotionAPI::generate_relative_move_gcode(4.0, -2.0, 0.0, 6000.0, 600.0) ==
          "G91\nG0 X4 Y-2 F6000\nG90");
}

TEST_CASE("generate_relative_move_gcode: Z gets its own feedrate line", "[motion][gcode]") {
    CHECK(MoonrakerMotionAPI::generate_relative_move_gcode(0.0, 0.0, 0.5, 6000.0, 600.0) ==
          "G91\nG0 Z0.5 F600\nG90");
}

TEST_CASE("generate_relative_move_gcode: XY and Z as two moves in one script", "[motion][gcode]") {
    CHECK(MoonrakerMotionAPI::generate_relative_move_gcode(1.0, 0.0, -0.5, 6000.0, 600.0) ==
          "G91\nG0 X1 F6000\nG0 Z-0.5 F600\nG90");
}

TEST_CASE("generate_relative_move_gcode: all-zero deltas produce empty script", "[motion][gcode]") {
    CHECK(MoonrakerMotionAPI::generate_relative_move_gcode(0.0, 0.0, 0.0, 6000.0, 600.0).empty());
}

TEST_CASE("generate_relative_move_gcode: NaN/Inf rejected", "[motion][gcode]") {
    CHECK(MoonrakerMotionAPI::generate_relative_move_gcode(std::numeric_limits<double>::quiet_NaN(),
                                                           0.0, 0.0, 6000.0, 600.0)
              .empty());
    CHECK(MoonrakerMotionAPI::generate_relative_move_gcode(
              0.0, std::numeric_limits<double>::infinity(), 0.0, 6000.0, 600.0)
              .empty());
}
