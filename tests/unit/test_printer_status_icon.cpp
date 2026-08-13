// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Tests for PrinterStatusIcon::compute_state — the pure mapping from connection
// + klippy state to the displayed navbar icon. Kept free of LVGL subjects and
// the singleton so the branch logic (including the expected-restart suppression)
// is exercised directly.

#include "ui_printer_status_icon.h"

#include "moonraker_client.h" // ConnectionState
#include "printer_state.h"    // KlippyState

#include "../catch_amalgamated.hpp"

using namespace helix;

namespace {
constexpr int kConnected = static_cast<int>(ConnectionState::CONNECTED);
constexpr int kDisconnected = static_cast<int>(ConnectionState::DISCONNECTED);
constexpr int kFailed = static_cast<int>(ConnectionState::FAILED);

constexpr int kReady = static_cast<int>(KlippyState::READY);
constexpr int kStartup = static_cast<int>(KlippyState::STARTUP);
constexpr int kShutdown = static_cast<int>(KlippyState::SHUTDOWN);
constexpr int kKlippyError = static_cast<int>(KlippyState::ERROR);

PrinterIconState state(int conn, int klippy, bool ever_connected, bool expected_restart) {
    return PrinterStatusIcon::compute_state(conn, klippy, ever_connected, expected_restart);
}
} // namespace

TEST_CASE("PrinterStatusIcon::compute_state - connected klippy states", "[status_icon]") {
    SECTION("READY -> READY") {
        REQUIRE(state(kConnected, kReady, true, false) == PrinterIconState::READY);
    }
    SECTION("STARTUP -> WARNING") {
        REQUIRE(state(kConnected, kStartup, true, false) == PrinterIconState::WARNING);
    }
    SECTION("SHUTDOWN with no restart pending -> ERROR") {
        REQUIRE(state(kConnected, kShutdown, true, false) == PrinterIconState::ERROR);
    }
    SECTION("klippy ERROR -> ERROR") {
        REQUIRE(state(kConnected, kKlippyError, true, false) == PrinterIconState::ERROR);
    }
}

TEST_CASE("PrinterStatusIcon::compute_state - expected restart suppresses SHUTDOWN error",
          "[status_icon][suppress]") {
    SECTION("transient SHUTDOWN during expected restart shows WARNING, not ERROR") {
        REQUIRE(state(kConnected, kShutdown, true, /*expected_restart=*/true) ==
                PrinterIconState::WARNING);
    }
    SECTION("expected_restart does NOT mask a genuine klippy ERROR state") {
        // Only the transient SHUTDOWN is softened; a real ERROR still shows red.
        REQUIRE(state(kConnected, kKlippyError, true, /*expected_restart=*/true) ==
                PrinterIconState::ERROR);
    }
    SECTION("expected_restart is irrelevant when klippy is READY") {
        REQUIRE(state(kConnected, kReady, true, /*expected_restart=*/true) ==
                PrinterIconState::READY);
    }
}

TEST_CASE("PrinterStatusIcon::compute_state - disconnected states", "[status_icon]") {
    SECTION("connection FAILED -> ERROR") {
        REQUIRE(state(kFailed, kReady, true, false) == PrinterIconState::ERROR);
    }
    SECTION("disconnected but was connected -> WARNING") {
        REQUIRE(state(kDisconnected, kReady, /*ever_connected=*/true, false) ==
                PrinterIconState::WARNING);
    }
    SECTION("disconnected and never connected -> DISCONNECTED") {
        REQUIRE(state(kDisconnected, kReady, /*ever_connected=*/false, false) ==
                PrinterIconState::DISCONNECTED);
    }
}
