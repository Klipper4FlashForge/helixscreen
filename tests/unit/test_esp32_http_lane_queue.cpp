// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_esp32_http_lane_queue.cpp
 * @brief Host-side tests for the pure bounded-queue + cap logic EspHttpLane
 * uses (Plan 4 Task 10: R2 bounded queue, R3 size cap).
 *
 * esp_http_lane.cpp/.h are IDF-coupled (pthread, esp_http_client) and cannot
 * be compiled or unit-tested on the desktop build. The two pieces of pure
 * logic that drive its accept/reject and cap-clamping decisions —
 * clamp_fetch_cap() and BoundedSlotCounter — were extracted into
 * firmware/helixscreen-esp32/components/helixnet/http_lane_queue.h (no
 * ESP-IDF/pthread/esp_http_client includes), mirroring Task 9's
 * reconnect_backoff.h extraction, and are exercised here, unmodified, via the
 * repo-root include path the desktop test build already has (`-I.` in
 * mk/tests.mk's $(INCLUDES)).
 */

#include "firmware/helixscreen-esp32/components/helixnet/http_lane_queue.h"

#include "../catch_amalgamated.hpp"

using helix::http::BoundedSlotCounter;
using helix::http::clamp_fetch_cap;
using helix::http::kHardCapBytes;

TEST_CASE("clamp_fetch_cap enforces the hard ceiling regardless of the request", "[esp32][http]") {
    // Well under the cap — passed through unchanged.
    REQUIRE(clamp_fetch_cap(1024) == 1024);
    REQUIRE(clamp_fetch_cap(100 * 1024) == 100 * 1024);

    // Exactly at the cap — unchanged.
    REQUIRE(clamp_fetch_cap(kHardCapBytes) == kHardCapBytes);

    // Over the cap — clamped down, never grows past the ceiling.
    REQUIRE(clamp_fetch_cap(kHardCapBytes + 1) == kHardCapBytes);
    REQUIRE(clamp_fetch_cap(10 * 1024 * 1024) == kHardCapBytes);
}

TEST_CASE("clamp_fetch_cap(0, ...) means \"no explicit cap\", not \"zero bytes\"",
          "[esp32][http]") {
    // A caller-requested 0 resolves to the hard ceiling — it must NOT mean
    // "fetch nothing" (that would silently break any caller that forgets to
    // set an explicit max_bytes).
    REQUIRE(clamp_fetch_cap(0) == kHardCapBytes);
}

TEST_CASE("BoundedSlotCounter accepts up to max_depth then rejects", "[esp32][http]") {
    BoundedSlotCounter slots(3);
    REQUIRE(slots.in_flight() == 0);

    REQUIRE(slots.try_acquire());
    REQUIRE(slots.try_acquire());
    REQUIRE(slots.try_acquire());
    REQUIRE(slots.in_flight() == 3);

    // Queue full — the 4th submission must be rejected, not queued.
    REQUIRE_FALSE(slots.try_acquire());
    REQUIRE(slots.in_flight() == 3); // rejection doesn't mutate state

    // Completing one job frees a slot for the next submission.
    slots.release();
    REQUIRE(slots.in_flight() == 2);
    REQUIRE(slots.try_acquire());
    REQUIRE(slots.in_flight() == 3);
}

TEST_CASE("BoundedSlotCounter::release() never underflows below zero", "[esp32][http]") {
    // Defensive: release() with nothing in flight must not wrap a size_t
    // counter to SIZE_MAX (which would then never report "full" again).
    BoundedSlotCounter slots(2);
    slots.release();
    slots.release();
    REQUIRE(slots.in_flight() == 0);

    // Still behaves correctly afterward — no corrupted state from the
    // defensive releases above.
    REQUIRE(slots.try_acquire());
    REQUIRE(slots.try_acquire());
    REQUIRE_FALSE(slots.try_acquire());
}

TEST_CASE("BoundedSlotCounter::max_depth() reports the configured depth", "[esp32][http]") {
    // 8 matches EspHttpLane::kQueueDepth (esp_http_lane.h) — kept as a literal
    // here since that header pulls no ESP-IDF includes but is still the
    // IDF-coupled class's home, not this pure-logic test's.
    BoundedSlotCounter slots(8);
    REQUIRE(slots.max_depth() == 8);
    REQUIRE(slots.in_flight() == 0);
}
