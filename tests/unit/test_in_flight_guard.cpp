// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_in_flight_guard.cpp
 * @brief Unit tests for helix::InFlightGuard (#910).
 *
 * Pure logic — no LVGL/fixture. Drives the injectable @c now TimePoint so
 * stuck-detection is deterministic. This file subsumes the coverage previously
 * held by test_print_select_refresh_self_heal.cpp's refresh_should_skip cases:
 * the guard now owns that decision, so the four skip/self-heal scenarios are
 * re-expressed here in terms of AcquireResult (Acquired/RecoveredStuck/Skipped).
 */

#include "in_flight_guard.h"

#include <chrono>

#include "../catch_amalgamated.hpp"

using helix::InFlightGuard;
using Result = helix::InFlightGuard::AcquireResult;
using Clock = helix::InFlightGuard::Clock;
using namespace std::chrono_literals;

// ============================================================================
// Basic acquire / release lifecycle
// ============================================================================

TEST_CASE("InFlightGuard: fresh guard acquires and becomes active", "[inflight]") {
    InFlightGuard g(30000ms);
    REQUIRE(g.active() == false);

    auto t0 = Clock::now();
    REQUIRE(g.try_acquire(/*force=*/false, t0) == Result::Acquired);
    REQUIRE(g.active() == true);
}

TEST_CASE("InFlightGuard: acquire while active & under threshold is skipped", "[inflight]") {
    InFlightGuard g(30000ms);
    auto t0 = Clock::now();
    REQUIRE(g.try_acquire(false, t0) == Result::Acquired);

    // 5s in — well under the 30s threshold: a healthy request is still in flight.
    REQUIRE(g.try_acquire(false, t0 + 5s) == Result::Skipped);
    REQUIRE(g.active() == true); // state unchanged
}

TEST_CASE("InFlightGuard: release clears active and re-enables acquire", "[inflight]") {
    InFlightGuard g(30000ms);
    auto t0 = Clock::now();
    REQUIRE(g.try_acquire(false, t0) == Result::Acquired);

    g.release();
    REQUIRE(g.active() == false);

    REQUIRE(g.try_acquire(false, t0 + 1s) == Result::Acquired);
    REQUIRE(g.active() == true);
}

// ============================================================================
// Stuck self-heal (mirrors PrintSelectPanel #911 behavior)
// ============================================================================

TEST_CASE("InFlightGuard: stuck request self-heals and re-bases started_at", "[inflight]") {
    InFlightGuard g(30000ms);
    auto t0 = Clock::now();
    REQUIRE(g.try_acquire(false, t0) == Result::Acquired);

    // 31s later the prior response is presumed lost — recover.
    auto t_stuck = t0 + 31s;
    REQUIRE(g.is_stuck(t_stuck) == true);
    REQUIRE(g.try_acquire(false, t_stuck) == Result::RecoveredStuck);
    REQUIRE(g.active() == true);

    // started_at was re-based to t_stuck: not stuck again until another 30s pass.
    REQUIRE(g.is_stuck(t_stuck + 5s) == false);
    REQUIRE(g.try_acquire(false, t_stuck + 5s) == Result::Skipped);
    REQUIRE(g.is_stuck(t_stuck + 30s) == true);
}

TEST_CASE("InFlightGuard: force acquires while active (not stuck) and bumps started_at",
          "[inflight]") {
    InFlightGuard g(30000ms);
    auto t0 = Clock::now();
    REQUIRE(g.try_acquire(false, t0) == Result::Acquired);

    // Under threshold, but force must proceed regardless.
    auto t1 = t0 + 5s;
    REQUIRE(g.try_acquire(/*force=*/true, t1) == Result::Acquired);
    REQUIRE(g.active() == true);

    // started_at bumped to t1: a non-forced acquire 5s later still skips (not yet stuck),
    // and staleness is measured from t1, not t0.
    REQUIRE(g.is_stuck(t1 + 5s) == false);
    REQUIRE(g.is_stuck(t0 + 31s) == false); // 26s past t1 — under threshold
    REQUIRE(g.is_stuck(t1 + 30s) == true);
}

// ============================================================================
// is_stuck boundary — the guard uses >=, so exactly-threshold IS stuck.
// (Matches refresh_should_skip's "exactly threshold -> self-heal" case.)
// ============================================================================

TEST_CASE("InFlightGuard: is_stuck boundary at exactly the threshold", "[inflight]") {
    InFlightGuard g(30000ms);
    auto t0 = Clock::now();
    REQUIRE(g.try_acquire(false, t0) == Result::Acquired);

    // Exactly 30s: >= threshold -> stuck -> a non-forced acquire recovers.
    REQUIRE(g.is_stuck(t0 + 30s) == true);
    REQUIRE(g.try_acquire(false, t0 + 30s) == Result::RecoveredStuck);
}

TEST_CASE("InFlightGuard: is_stuck just under the threshold is not stuck", "[inflight]") {
    InFlightGuard g(30000ms);
    auto t0 = Clock::now();
    REQUIRE(g.try_acquire(false, t0) == Result::Acquired);

    // 1ms shy of the threshold: not stuck -> a non-forced acquire skips.
    auto almost = t0 + 30s - 1ms;
    REQUIRE(g.is_stuck(almost) == false);
    REQUIRE(g.try_acquire(false, almost) == Result::Skipped);
}

TEST_CASE("InFlightGuard: idle guard is never stuck", "[inflight]") {
    InFlightGuard g(30000ms);
    REQUIRE(g.is_stuck(Clock::now()) == false);
    REQUIRE(g.is_stuck(Clock::now() + 1h) == false);
}

// ============================================================================
// Coverage parity with the retired refresh_should_skip tests (#911):
//   not-in-flight -> never skip; force -> never skip; under threshold -> skip;
//   over/at threshold -> self-heal. Also short + zero thresholds.
// ============================================================================

TEST_CASE("InFlightGuard: not-in-flight never skips (any elapsed time)", "[inflight]") {
    InFlightGuard g(30000ms);
    auto now = Clock::now();
    // Fresh guard, huge notional gap — still Acquired, never Skipped.
    REQUIRE(g.try_acquire(false, now) == Result::Acquired);
    g.release();
    REQUIRE(g.try_acquire(false, now + 1h) == Result::Acquired);
}

TEST_CASE("InFlightGuard: force never skips even when a healthy request is in flight",
          "[inflight]") {
    InFlightGuard g(30000ms);
    auto t0 = Clock::now();
    REQUIRE(g.try_acquire(false, t0) == Result::Acquired);
    REQUIRE(g.try_acquire(/*force=*/true, t0 + 50ms) == Result::Acquired);
}

TEST_CASE("InFlightGuard: short (50ms) threshold is honoured", "[inflight]") {
    InFlightGuard g(50ms);
    auto t0 = Clock::now();
    REQUIRE(g.try_acquire(false, t0) == Result::Acquired);

    SECTION("under 50ms -> skip") {
        REQUIRE(g.try_acquire(false, t0 + 10ms) == Result::Skipped);
    }
    SECTION("over 50ms -> self-heal") {
        REQUIRE(g.try_acquire(false, t0 + 100ms) == Result::RecoveredStuck);
    }
}

TEST_CASE("InFlightGuard: zero threshold always self-heals when in flight", "[inflight]") {
    InFlightGuard g(0ms);
    auto t = Clock::now();
    REQUIRE(g.try_acquire(false, t) == Result::Acquired);
    // Any elapsed >= 0 is stuck -> recover, never skip.
    REQUIRE(g.try_acquire(false, t) == Result::RecoveredStuck);
    REQUIRE(g.try_acquire(false, t + 1us) == Result::RecoveredStuck);
}

TEST_CASE("InFlightGuard: stuck_threshold() reports the configured value", "[inflight]") {
    InFlightGuard g(1234ms);
    REQUIRE(g.stuck_threshold() == 1234ms);
    InFlightGuard def;
    REQUIRE(def.stuck_threshold() == 30000ms);
}
