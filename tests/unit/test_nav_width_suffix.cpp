// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_nav_width_suffix.cpp
 * @brief nav_width breakpoint selection must not depend on which path ran
 *
 * Startup registration and the resize refresh each had their own copy of this
 * ladder, and only the startup copy grew the ultrawide branch. A 1920x480 panel
 * therefore booted with the 76px '_small' nav bar and, after any resize, jumped
 * to the 132px '_large' one — a 56px lurch on the axis ultrawide has least of.
 * One function now serves both, so the divergence cannot come back.
 */

#include "ui_breakpoint.h"

#include "theme_manager.h"

#include <cstring>

#include "../catch_amalgamated.hpp"

using namespace helix;

namespace {
/// Catch2 prints const char* as a pointer; compare as strings.
std::string suffix(int32_t w, int32_t h) {
    return std::string(nav_width_suffix(w, h));
}
} // namespace

TEST_CASE("nav_width_suffix: ultrawide keeps the strip slim", "[theme][nav-width]") {
    // The regression: these all exceed the 1100px '_large' threshold on width,
    // and would land on "_large" without the aspect-ratio branch.
    CHECK(suffix(1920, 480) == "_small");
    CHECK(suffix(2560, 600) == "_small");
    // Very short ultrawide panels go one rung narrower still.
    CHECK(suffix(1920, 390) == "_tiny");
    CHECK(suffix(1280, 320) == "_tiny");
}

TEST_CASE("nav_width_suffix: micro is decided on the vertical axis", "[theme][nav-width]") {
    // 480x272 and 480x320 share a width; only height separates them.
    CHECK(suffix(480, 272) == "_micro");
    CHECK(suffix(480, 320) == "_tiny");
}

TEST_CASE("nav_width_suffix: ordinary landscape follows the width ladder", "[theme][nav-width]") {
    CHECK(suffix(480, 800) == "_tiny");    // <= 520
    CHECK(suffix(800, 480) == "_small");   // <= 900
    CHECK(suffix(1024, 600) == "_medium"); // <= 1100
    CHECK(suffix(1280, 800) == "_large");  // > 1100
}

TEST_CASE("nav_width_suffix: portrait picks from the narrow axis", "[theme][nav-width]") {
    // Portrait lays the bar along the bottom, so nav_width no longer feeds
    // overlay sizing there — but the token still resolves, and it must not
    // explode on a very tall aspect ratio.
    CHECK(suffix(320, 1480) == "_tiny");
    // 272x480 is 272 WIDE by 480 tall, so the micro test (which reads ver_res)
    // does not fire — 480 is above MICRO_MAX. It falls through to the width
    // ladder and lands on _tiny. The micro rung is reachable only by a short
    // display, which in practice means landscape 480x272.
    CHECK(suffix(272, 480) == "_tiny");
}

TEST_CASE("nav_width_suffix: the ultrawide threshold is exactly 2.5:1", "[theme][nav-width]") {
    // Just under 2.5:1 is not ultrawide and follows the width ladder; just over
    // is capped. 1200x480 = 2.5 exactly, which is NOT > 2.5.
    CHECK(suffix(1200, 480) == "_large"); // 2.50:1 — not ultrawide
    CHECK(suffix(1201, 480) == "_small"); // 2.502:1 — ultrawide, capped
}
