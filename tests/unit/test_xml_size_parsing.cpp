// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_xml_size_parsing.cpp
 * @brief lv_xml_to_size() must not read outside the string it was handed.
 *
 * Run with: ./build/bin/helix-tests "[xml][size_parse]"
 *
 * Background — this is the root cause of the long-standing test_xml_expr_compose
 * flake (#1121), which failed only in full-suite runs and never in isolation.
 *
 * A `${expr}` that fails to compile splices an EMPTY string into the attribute,
 * so `width=''` reaches lv_xml_to_size(). The percent suffix check was written
 * as `txt[lv_strlen(txt) - 1]`, which for an empty string indexes txt[-1] — one
 * byte before the buffer. Whatever happened to sit there decided the result: a
 * stray '%' turned a plain 0 into lv_pct(0) (0x20000000), and a percentage width
 * lays out to something other than 0. Heap layout differs between a solo run and
 * a full-suite run, which is exactly why it only ever failed in the latter.
 *
 * The first test reproduces that deterministically by controlling the byte
 * before the string, rather than waiting for the heap to line up by chance.
 *
 * Mutation check: remove the empty-string guard in lv_xml_to_size() and
 * "empty string does not depend on the preceding byte" fails (536870912 vs 0).
 */

#include "../catch_amalgamated.hpp"
#include "../lvgl_test_fixture.h"

extern "C" {
#include "helix-xml/src/xml/lv_xml_base_types.h"
}

TEST_CASE("lv_xml_to_size: empty string does not depend on the preceding byte",
          "[xml][size_parse]") {
    // Two empty strings whose out-of-bounds predecessor differs. A conforming
    // parser cannot tell them apart; the buggy one returned lv_pct(0) for the
    // first and 0 for the second.
    char poisoned[4] = {'%', '\0', '\0', '\0'};
    char benign[4] = {'x', '\0', '\0', '\0'};

    int32_t after_pct = lv_xml_to_size(poisoned + 1);
    int32_t after_x = lv_xml_to_size(benign + 1);

    CHECK(after_pct == after_x);
    CHECK(after_pct == 0); // an absent size is zero, not a percentage
}

TEST_CASE("lv_xml_to_size: normal forms still parse", "[xml][size_parse]") {
    // The guard must not disturb any real value.
    CHECK(lv_xml_to_size("0") == 0);
    CHECK(lv_xml_to_size("10") == 10);
    CHECK(lv_xml_to_size("-4") == -4);
    CHECK(lv_xml_to_size("content") == LV_SIZE_CONTENT);
    CHECK(lv_xml_to_size("50%") == lv_pct(50));
    CHECK(lv_xml_to_size("100%") == lv_pct(100));
    // A bare "%" is degenerate but must stay in-bounds and self-consistent.
    CHECK(lv_xml_to_size("%") == lv_pct(0));
}

TEST_CASE("lv_xml_to_size: null is treated as absent", "[xml][size_parse]") {
    CHECK(lv_xml_to_size(nullptr) == 0);
}
