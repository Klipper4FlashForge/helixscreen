// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/// @file test_objects_count_text.cpp
/// Characterization tests for the objects-count formatting logic used on the
/// print status panel. Pure logic — no LVGL dependency.

#include <algorithm>
#include <cstdio>
#include <string>

#include "../catch_amalgamated.hpp"

// ---------------------------------------------------------------------------
// Extracted formatting logic (mirrors PrintStatusPanel::update_objects_text)
// ---------------------------------------------------------------------------

/// Format the active/total objects string for the print status layer row.
/// Returns empty string when there are fewer than 2 defined objects (nothing
/// meaningful to show).
static std::string format_objects_count(int total_defined, int num_excluded) {
    if (total_defined < 2)
        return {};
    int active = std::max(0, total_defined - num_excluded);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%d of %d objects", active, total_defined);
    return buf;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

TEST_CASE("objects count text formatting", "[print_status][exclude_object]") {
    SECTION("0 objects defined — no exclude_object support") {
        REQUIRE(format_objects_count(0, 0).empty());
    }

    SECTION("1 object defined — single-object, nothing to exclude") {
        REQUIRE(format_objects_count(1, 0).empty());
    }

    SECTION("2 objects, 0 excluded") {
        REQUIRE(format_objects_count(2, 0) == "2 of 2 objects");
    }

    SECTION("5 objects, 0 excluded") {
        REQUIRE(format_objects_count(5, 0) == "5 of 5 objects");
    }

    SECTION("5 objects, 2 excluded") {
        REQUIRE(format_objects_count(5, 2) == "3 of 5 objects");
    }

    SECTION("5 objects, 5 excluded — all excluded (degenerate)") {
        REQUIRE(format_objects_count(5, 5) == "0 of 5 objects");
    }

    SECTION("excluded > defined — defensive clamp") {
        REQUIRE(format_objects_count(3, 7) == "0 of 3 objects");
    }
}
