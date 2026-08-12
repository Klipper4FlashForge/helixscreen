// SPDX-License-Identifier: GPL-3.0-or-later

#include "../../include/bed_dimensions.h"

#include "../catch_amalgamated.hpp"

TEST_CASE("bed_dimensions falls back to 235x235 with no api and no state", "[bed_dimensions]") {
    auto d = helix::bed_dimensions(nullptr, nullptr);
    REQUIRE(d.w_mm == Catch::Approx(235.0f));
    REQUIRE(d.h_mm == Catch::Approx(235.0f));
    REQUIRE(d.origin_x == Catch::Approx(0.0f));
    REQUIRE(d.origin_y == Catch::Approx(0.0f));
}

TEST_CASE("bed_dimensions rejects a degenerate build volume", "[bed_dimensions]") {
    // A build volume that has not been populated yet reports x_max == x_min,
    // giving a zero-width bed. That must fall through to the default rather
    // than produce a zero scale in BedCoordMapper.
    helix::BedDimensions d = helix::bed_dimensions_from_volume(0.0f, 0.0f, 0.0f, 0.0f);
    REQUIRE(d.w_mm == Catch::Approx(235.0f));
    REQUIRE(d.h_mm == Catch::Approx(235.0f));
}

TEST_CASE("bed_dimensions uses a populated build volume", "[bed_dimensions]") {
    helix::BedDimensions d = helix::bed_dimensions_from_volume(0.0f, 350.0f, 0.0f, 350.0f);
    REQUIRE(d.w_mm == Catch::Approx(350.0f));
    REQUIRE(d.h_mm == Catch::Approx(350.0f));
    REQUIRE(d.origin_x == Catch::Approx(0.0f));
}

TEST_CASE("bed_dimensions carries a negative origin through for delta beds", "[bed_dimensions]") {
    helix::BedDimensions d = helix::bed_dimensions_from_volume(-100.0f, 100.0f, -100.0f, 100.0f);
    REQUIRE(d.w_mm == Catch::Approx(200.0f));
    REQUIRE(d.h_mm == Catch::Approx(200.0f));
    REQUIRE(d.origin_x == Catch::Approx(-100.0f));
    REQUIRE(d.origin_y == Catch::Approx(-100.0f));
}
