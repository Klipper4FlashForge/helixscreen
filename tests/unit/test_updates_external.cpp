// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_updates_external.cpp
 * @brief Tests for the firmware-managed-update gate (updates_externally_managed).
 *
 * Covers helix_parse_truthy_env() (the pure parse that feeds the cached
 * updates_externally_managed() helper) and confirms the cached predicate is
 * consistent with the process environment. The cache is deliberately NOT
 * exercised for both true/false in one process — the parse function is the
 * testable unit; the cache is a thin static wrapper around it.
 */

#include "app_globals.h"

#include <cstdlib>

#include "../catch_amalgamated.hpp"

TEST_CASE("helix_parse_truthy_env recognizes truthy values", "[update][external]") {
    // Truthy — case-insensitive
    CHECK(helix_parse_truthy_env("1"));
    CHECK(helix_parse_truthy_env("true"));
    CHECK(helix_parse_truthy_env("TRUE"));
    CHECK(helix_parse_truthy_env("True"));
    CHECK(helix_parse_truthy_env("yes"));
    CHECK(helix_parse_truthy_env("YES"));
    CHECK(helix_parse_truthy_env("on"));
    CHECK(helix_parse_truthy_env("ON"));
    // Surrounding whitespace tolerated (helixscreen.env may carry a stray space)
    CHECK(helix_parse_truthy_env("  1  "));
    CHECK(helix_parse_truthy_env("\ttrue\n"));
}

TEST_CASE("helix_parse_truthy_env rejects falsy and empty values", "[update][external]") {
    CHECK_FALSE(helix_parse_truthy_env(nullptr));
    CHECK_FALSE(helix_parse_truthy_env(""));
    CHECK_FALSE(helix_parse_truthy_env("0"));
    CHECK_FALSE(helix_parse_truthy_env("false"));
    CHECK_FALSE(helix_parse_truthy_env("no"));
    CHECK_FALSE(helix_parse_truthy_env("off"));
    CHECK_FALSE(helix_parse_truthy_env("2"));
    CHECK_FALSE(helix_parse_truthy_env("enabled"));
    CHECK_FALSE(helix_parse_truthy_env("   "));
}

TEST_CASE("compute_updates_externally_managed detects firmware-managed launch",
          "[update][external]") {
    // Args: (updates_external, disable_auto_updates, supervised, data_dir)

    // Explicit HELIX_UPDATES_EXTERNAL override wins regardless of the other vars.
    CHECK(compute_updates_externally_managed("1", nullptr, nullptr, nullptr));
    CHECK(compute_updates_externally_managed("yes", nullptr, "0", ""));

    // Explicit HELIX_DISABLE_AUTO_UPDATES (firmware-facing name) also wins alone.
    CHECK(compute_updates_externally_managed(nullptr, "1", nullptr, nullptr));
    CHECK(compute_updates_externally_managed(nullptr, "true", "0", ""));
    CHECK(compute_updates_externally_managed(nullptr, "on", nullptr, nullptr));

    // Firmware-managed: supervised AND asset root relocated.
    CHECK(compute_updates_externally_managed(nullptr, nullptr, "1", "/oem/apps/helixscreen/latest"));
    CHECK(compute_updates_externally_managed("", nullptr, "true", "/some/dir"));

    // Our own watchdog install: supervised but HELIX_DATA_DIR unset/empty —
    // self-update MUST stay enabled.
    CHECK_FALSE(compute_updates_externally_managed(nullptr, nullptr, "1", nullptr));
    CHECK_FALSE(compute_updates_externally_managed(nullptr, nullptr, "1", ""));

    // HELIX_DATA_DIR alone (e.g. a dev pointing at a custom asset dir) is not
    // enough — a supervisor must also own the lifecycle.
    CHECK_FALSE(compute_updates_externally_managed(nullptr, nullptr, nullptr, "/some/dir"));
    CHECK_FALSE(compute_updates_externally_managed(nullptr, nullptr, "0", "/some/dir"));

    // A falsy explicit opt-out does not force the managed state.
    CHECK_FALSE(compute_updates_externally_managed(nullptr, "0", nullptr, nullptr));
    CHECK_FALSE(compute_updates_externally_managed("false", "no", nullptr, nullptr));

    // Nothing set → normal self-managed install.
    CHECK_FALSE(compute_updates_externally_managed(nullptr, nullptr, nullptr, nullptr));
}

TEST_CASE("updates_externally_managed reflects the environment (cached)",
          "[update][external]") {
    // The value is cached process-wide, so we assert it agrees with the pure
    // predicate over the current env rather than trying to flip it mid-process.
    const bool expected = compute_updates_externally_managed(
        std::getenv("HELIX_UPDATES_EXTERNAL"), std::getenv("HELIX_DISABLE_AUTO_UPDATES"),
        std::getenv("HELIX_SUPERVISED"), std::getenv("HELIX_DATA_DIR"));
    CHECK(updates_externally_managed() == expected);
    // Stable across calls (proves the cache doesn't re-read differently).
    CHECK(updates_externally_managed() == updates_externally_managed());
}
