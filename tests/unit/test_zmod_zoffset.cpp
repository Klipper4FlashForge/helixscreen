// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_zmod_zoffset.cpp
 * @brief Tests for the ZMOD persistent-z-offset send predicate.
 *
 * helix::zmod::should_enable_persistent_zoffset gates whether HelixScreen sends
 * `SAVE_ZMOD_DATA LOAD_ZOFFSET=1`. It must be true ONLY when the ZMOD macro is
 * present, the printer is idle, and we have not already sent it this session.
 */

#include "zmod_zoffset.h"

#include "../catch_amalgamated.hpp"

using helix::zmod::should_enable_persistent_zoffset;

TEST_CASE("ZMOD z-offset: no macro means never send", "[zmod]") {
    // Macro absent -> false regardless of the other two args.
    CHECK_FALSE(should_enable_persistent_zoffset(false, false, false));
    CHECK_FALSE(should_enable_persistent_zoffset(false, true, false));
    CHECK_FALSE(should_enable_persistent_zoffset(false, false, true));
    CHECK_FALSE(should_enable_persistent_zoffset(false, true, true));
}

TEST_CASE("ZMOD z-offset: macro present, idle, not yet sent -> send", "[zmod]") {
    CHECK(should_enable_persistent_zoffset(true, false, false));
}

TEST_CASE("ZMOD z-offset: never send during an active print", "[zmod]") {
    CHECK_FALSE(should_enable_persistent_zoffset(true, true, false));
}

TEST_CASE("ZMOD z-offset: never send twice in a session", "[zmod]") {
    CHECK_FALSE(should_enable_persistent_zoffset(true, false, true));
}
