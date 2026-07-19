// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_esp32_reconnect_backoff.cpp
 * @brief Host-side tests for the pure reconnect predicates the ESP32
 * EspMoonrakerClient uses (Plan 4 Task 9: F8 backoff + R3 generation guard).
 *
 * esp_moonraker_client.cpp/.h are IDF-coupled (esp_timer.h, esp_websocket_client.h)
 * and cannot be compiled or unit-tested on the desktop build. The two predicates
 * that drive the reconnect logic — exponential backoff doubling, and the
 * connection-generation staleness check the discovery chain re-checks at every
 * async continuation — are pure integer/atomic-free logic, so they were
 * extracted into firmware/helixscreen-esp32/components/helixnet/reconnect_backoff.h
 * (no ESP-IDF/FreeRTOS includes) and are exercised here, unmodified, via the
 * repo-root include path the desktop test build already has (`-I.` in
 * mk/tests.mk's $(INCLUDES)).
 */

#include "firmware/helixscreen-esp32/components/helixnet/reconnect_backoff.h"

#include "../catch_amalgamated.hpp"

using helix::is_stale_generation;
using helix::next_backoff_delay_ms;

TEST_CASE("next_backoff_delay_ms doubles up to the cap", "[esp32][reconnect]") {
    REQUIRE(next_backoff_delay_ms(200, 2000) == 400);
    REQUIRE(next_backoff_delay_ms(400, 2000) == 800);
    REQUIRE(next_backoff_delay_ms(800, 2000) == 1600);
    // Doubling would overshoot the cap — clamp, don't overshoot.
    REQUIRE(next_backoff_delay_ms(1600, 2000) == 2000);
    // Already at (or past) the cap — stays put, never grows unbounded.
    REQUIRE(next_backoff_delay_ms(2000, 2000) == 2000);
    REQUIRE(next_backoff_delay_ms(3000, 2000) == 2000);
}

TEST_CASE("next_backoff_delay_ms handles a zero floor without stalling forever",
          "[esp32][reconnect]") {
    // A misconfigured 0ms floor must still climb toward the cap, not stay
    // stuck at 0 forever (0 * 2 == 0 would never reach max_delay_ms).
    int delay = 0;
    for (int i = 0; i < 20 && delay < 2000; ++i) {
        delay = next_backoff_delay_ms(delay == 0 ? 1 : delay, 2000);
    }
    REQUIRE(delay == 2000);
}

TEST_CASE("is_stale_generation matches desktop connection_generation() semantics",
          "[esp32][reconnect]") {
    // Same generation the chain snapshotted at entry — not stale.
    REQUIRE_FALSE(is_stale_generation(/*chain_generation=*/5, /*current_generation=*/5));

    // A reconnect (connect()/force_reconnect()/auto-reconnect executor) has
    // bumped the generation since the chain snapshotted it — stale, the chain
    // must abandon in place.
    REQUIRE(is_stale_generation(/*chain_generation=*/5, /*current_generation=*/6));

    // Bumped more than once (e.g. two reconnects landed while a slow RPC was
    // still in flight) — still stale, not just "off by one".
    REQUIRE(is_stale_generation(/*chain_generation=*/5, /*current_generation=*/9));

    // Both at the initial value (never connected yet) — not stale.
    REQUIRE_FALSE(is_stale_generation(0, 0));
}
