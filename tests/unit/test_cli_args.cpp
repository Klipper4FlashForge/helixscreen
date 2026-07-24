// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_cli_args.cpp
 * @brief Unit tests for CLI argument struct helpers
 *
 * Tests the CliArgs struct defaults and parse_cli_args() for flags that do
 * not require graphics or printer state.
 */

#include "cli_args.h"

#include "../catch_amalgamated.hpp"

using namespace helix;

// ============================================================================
// CliArgs Tests
// ============================================================================

TEST_CASE("parse_cli_args: --detect-printer with host/port", "[cli_args][detect]") {
    const char* argv[] = {"helix-screen", "--detect-printer", "--host",
                          "127.0.0.1",    "--port",           "7125"};
    CliArgs args;
    int w = 0, h = 0;
    REQUIRE(parse_cli_args(6, const_cast<char**>(argv), args, w, h));
    REQUIRE(args.detect_printer);
    REQUIRE(args.detect_host == "127.0.0.1");
    REQUIRE(args.detect_port == 7125);
}

TEST_CASE("parse_cli_args: --detect-printer defaults", "[cli_args][detect]") {
    const char* argv[] = {"helix-screen", "--detect-printer"};
    CliArgs args;
    int w = 0, h = 0;
    REQUIRE(parse_cli_args(2, const_cast<char**>(argv), args, w, h));
    REQUIRE(args.detect_printer);
    REQUIRE(args.detect_host == "127.0.0.1");
    REQUIRE(args.detect_port == 7125);
}

TEST_CASE("CliArgs: default values", "[cli_args]") {
    CliArgs args;

    SECTION("screen settings default to auto") {
        REQUIRE(args.screen_size == ScreenSize::MEDIUM);
        REQUIRE(args.dpi == -1);
        REQUIRE(args.display_num == -1);
        REQUIRE(args.x_pos == -1);
        REQUIRE(args.y_pos == -1);
    }

    SECTION("wizard defaults off") {
        REQUIRE_FALSE(args.force_wizard);
        REQUIRE(args.wizard_step == -1);
    }

    SECTION("automation defaults off") {
        REQUIRE_FALSE(args.screenshot_enabled);
        REQUIRE(args.screenshot_delay_sec == 2);
        REQUIRE(args.timeout_sec == 0);
    }

    SECTION("theme defaults to not set") {
        REQUIRE(args.dark_mode_cli == -1);
    }

    SECTION("logging defaults to warning level") {
        REQUIRE(args.verbosity == 0);
    }

    SECTION("memory profiling defaults off") {
        REQUIRE_FALSE(args.memory_report);
        REQUIRE_FALSE(args.show_memory);
    }

    SECTION("moonraker URL default empty") {
        REQUIRE(args.moonraker_url.empty());
    }

    SECTION("layout default empty") {
        REQUIRE(args.layout.empty());
    }
}

// ============================================================================
// ScreenSize Enum Tests
// ============================================================================

TEST_CASE("ScreenSize enum values", "[cli_args]") {
    SECTION("All enum values are distinct") {
        REQUIRE(ScreenSize::TINY != ScreenSize::SMALL);
        REQUIRE(ScreenSize::TINY != ScreenSize::MEDIUM);
        REQUIRE(ScreenSize::TINY != ScreenSize::LARGE);
        REQUIRE(ScreenSize::TINY != ScreenSize::XLARGE);
        REQUIRE(ScreenSize::SMALL != ScreenSize::MEDIUM);
        REQUIRE(ScreenSize::SMALL != ScreenSize::LARGE);
        REQUIRE(ScreenSize::SMALL != ScreenSize::XLARGE);
        REQUIRE(ScreenSize::MEDIUM != ScreenSize::LARGE);
        REQUIRE(ScreenSize::MEDIUM != ScreenSize::XLARGE);
        REQUIRE(ScreenSize::LARGE != ScreenSize::XLARGE);
    }

    SECTION("ScreenSize ordering matches expected breakpoint order") {
        // Verify enum values are ordered TINY < SMALL < MEDIUM < LARGE < XLARGE
        REQUIRE(static_cast<int>(ScreenSize::TINY) < static_cast<int>(ScreenSize::SMALL));
        REQUIRE(static_cast<int>(ScreenSize::SMALL) < static_cast<int>(ScreenSize::MEDIUM));
        REQUIRE(static_cast<int>(ScreenSize::MEDIUM) < static_cast<int>(ScreenSize::LARGE));
        REQUIRE(static_cast<int>(ScreenSize::LARGE) < static_cast<int>(ScreenSize::XLARGE));
    }

    SECTION("Default CliArgs screen_size is MEDIUM") {
        CliArgs args;
        REQUIRE(args.screen_size == ScreenSize::MEDIUM);
    }
}
