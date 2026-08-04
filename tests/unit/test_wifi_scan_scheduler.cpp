// Copyright (C) 2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../../include/wifi_scan_scheduler.h"

#include "../catch_amalgamated.hpp"

using helix::wifi::ScanScheduler;

// ScanScheduler is a pure state machine — no LVGL, no timers, no backend —
// so these tests exercise it directly with no fixture needed.

TEST_CASE("ScanScheduler starts ready to trigger", "[wifi][scan_scheduler]") {
    ScanScheduler sched;
    REQUIRE(sched.should_trigger());
    REQUIRE_FALSE(sched.suppressed());
    REQUIRE(sched.next_interval_ms() == ScanScheduler::kBaseIntervalMs);
}

TEST_CASE("ScanScheduler blocks a second trigger while a scan is outstanding",
          "[wifi][scan_scheduler]") {
    ScanScheduler sched;
    sched.on_scan_started();
    REQUIRE_FALSE(sched.should_trigger());

    // Still outstanding — a second check must also refuse.
    REQUIRE_FALSE(sched.should_trigger());

    sched.on_scan_complete(3, false);
    REQUIRE(sched.should_trigger());
}

TEST_CASE("ScanScheduler grows the interval 10s -> 20s -> 30s and caps", "[wifi][scan_scheduler]") {
    ScanScheduler sched;
    REQUIRE(sched.next_interval_ms() == 10000);

    // Baseline reading — nothing to compare against yet, stays at base.
    sched.on_scan_started();
    sched.on_scan_complete(5, false);
    REQUIRE(sched.next_interval_ms() == 10000);

    // First repeat of the same count.
    sched.on_scan_started();
    sched.on_scan_complete(5, false);
    REQUIRE(sched.next_interval_ms() == 20000);

    // Second repeat.
    sched.on_scan_started();
    sched.on_scan_complete(5, false);
    REQUIRE(sched.next_interval_ms() == 30000);

    // Third repeat — capped, does not exceed kMaxIntervalMs.
    sched.on_scan_started();
    sched.on_scan_complete(5, false);
    REQUIRE(sched.next_interval_ms() == ScanScheduler::kMaxIntervalMs);
    REQUIRE(sched.next_interval_ms() == 30000);
}

TEST_CASE("ScanScheduler suppresses after an unchanged count twice while connected",
          "[wifi][scan_scheduler]") {
    ScanScheduler sched;

    // Baseline.
    sched.on_scan_started();
    sched.on_scan_complete(4, true);
    REQUIRE_FALSE(sched.suppressed());

    // First unchanged repeat — not suppressed yet.
    sched.on_scan_started();
    sched.on_scan_complete(4, true);
    REQUIRE_FALSE(sched.suppressed());
    REQUIRE(sched.should_trigger());

    // Second unchanged repeat — now suppressed.
    sched.on_scan_started();
    sched.on_scan_complete(4, true);
    REQUIRE(sched.suppressed());
    REQUIRE_FALSE(sched.should_trigger());
}

TEST_CASE("ScanScheduler does not suppress an unchanged count while disconnected",
          "[wifi][scan_scheduler]") {
    ScanScheduler sched;

    sched.on_scan_started();
    sched.on_scan_complete(4, false);
    sched.on_scan_started();
    sched.on_scan_complete(4, false);
    sched.on_scan_started();
    sched.on_scan_complete(4, false);

    REQUIRE_FALSE(sched.suppressed());
    REQUIRE(sched.should_trigger());
}

TEST_CASE("ScanScheduler on_user_refresh clears suppression and resets the interval",
          "[wifi][scan_scheduler]") {
    ScanScheduler sched;

    // Drive it into suppression.
    sched.on_scan_started();
    sched.on_scan_complete(7, true);
    sched.on_scan_started();
    sched.on_scan_complete(7, true);
    sched.on_scan_started();
    sched.on_scan_complete(7, true);
    REQUIRE(sched.suppressed());
    REQUIRE(sched.next_interval_ms() == 30000);

    sched.on_user_refresh();
    REQUIRE_FALSE(sched.suppressed());
    REQUIRE(sched.next_interval_ms() == ScanScheduler::kBaseIntervalMs);
    REQUIRE(sched.should_trigger());
}

TEST_CASE("ScanScheduler on_disconnected clears suppression and resets the interval",
          "[wifi][scan_scheduler]") {
    ScanScheduler sched;

    sched.on_scan_started();
    sched.on_scan_complete(2, true);
    sched.on_scan_started();
    sched.on_scan_complete(2, true);
    sched.on_scan_started();
    sched.on_scan_complete(2, true);
    REQUIRE(sched.suppressed());

    sched.on_disconnected();
    REQUIRE_FALSE(sched.suppressed());
    REQUIRE(sched.next_interval_ms() == ScanScheduler::kBaseIntervalMs);
    REQUIRE(sched.should_trigger());
}

TEST_CASE("ScanScheduler resets the interval on a changed count without suppressing",
          "[wifi][scan_scheduler]") {
    ScanScheduler sched;

    // Baseline + one repeat to grow the interval, but not enough to suppress.
    sched.on_scan_started();
    sched.on_scan_complete(1, true);
    sched.on_scan_started();
    sched.on_scan_complete(1, true);
    REQUIRE(sched.next_interval_ms() == 20000);
    REQUIRE_FALSE(sched.suppressed());

    // A changed count resets the interval...
    sched.on_scan_started();
    sched.on_scan_complete(9, true);
    REQUIRE(sched.next_interval_ms() == ScanScheduler::kBaseIntervalMs);
    // ...and does not suppress.
    REQUIRE_FALSE(sched.suppressed());
    REQUIRE(sched.should_trigger());
}
