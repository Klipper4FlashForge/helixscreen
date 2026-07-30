// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_xml_border_side.cpp
 * @brief border_side parsing, including OR'd combinations
 *
 * LV_BORDER_SIDE_* are bit flags, so "left|bottom" is a reasonable thing to
 * write. It used to fall through to the unknown-value branch and return 0 —
 * which IS LV_BORDER_SIDE_NONE, so the border silently disappeared rather than
 * the parse failing. That is the trap these tests pin.
 */

#include "../catch_amalgamated.hpp"

extern "C" {
#include "lvgl.h"
#include "helix-xml/src/xml/lv_xml_base_types.h"
}

TEST_CASE("border_side parses each single side", "[xml][style][border]") {
    CHECK(lv_xml_border_side_to_enum("none") == LV_BORDER_SIDE_NONE);
    CHECK(lv_xml_border_side_to_enum("top") == LV_BORDER_SIDE_TOP);
    CHECK(lv_xml_border_side_to_enum("bottom") == LV_BORDER_SIDE_BOTTOM);
    CHECK(lv_xml_border_side_to_enum("left") == LV_BORDER_SIDE_LEFT);
    CHECK(lv_xml_border_side_to_enum("right") == LV_BORDER_SIDE_RIGHT);
    CHECK(lv_xml_border_side_to_enum("full") == LV_BORDER_SIDE_FULL);
}

TEST_CASE("border_side ORs combined sides", "[xml][style][border]") {
    const auto left_bottom =
        static_cast<lv_border_side_t>(LV_BORDER_SIDE_LEFT | LV_BORDER_SIDE_BOTTOM);

    SECTION("pipe separator") {
        CHECK(lv_xml_border_side_to_enum("left|bottom") == left_bottom);
    }
    SECTION("comma separator") {
        CHECK(lv_xml_border_side_to_enum("left,bottom") == left_bottom);
    }
    SECTION("space separator") {
        CHECK(lv_xml_border_side_to_enum("left bottom") == left_bottom);
    }
    SECTION("spaces around separators") {
        CHECK(lv_xml_border_side_to_enum(" left | bottom ") == left_bottom);
    }
    SECTION("three sides") {
        CHECK(lv_xml_border_side_to_enum("top|left|bottom") ==
              static_cast<lv_border_side_t>(LV_BORDER_SIDE_TOP | LV_BORDER_SIDE_LEFT |
                                            LV_BORDER_SIDE_BOTTOM));
    }
    SECTION("order does not matter") {
        CHECK(lv_xml_border_side_to_enum("bottom|left") ==
              lv_xml_border_side_to_enum("left|bottom"));
    }
    SECTION("repeating a side is idempotent") {
        CHECK(lv_xml_border_side_to_enum("left|left") == LV_BORDER_SIDE_LEFT);
    }
}

TEST_CASE("border_side rejects unknown tokens", "[xml][style][border]") {
    // Returning 0 is LV_BORDER_SIDE_NONE — the border vanishes. That is the
    // documented fallback, but it must not be reached for a VALID combination,
    // which is what the tests above guard.
    CHECK(lv_xml_border_side_to_enum("sideways") == 0);
    CHECK(lv_xml_border_side_to_enum("left|sideways") == 0);
    CHECK(lv_xml_border_side_to_enum("") == 0);
}
