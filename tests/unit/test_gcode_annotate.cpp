// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_gcode_annotate.cpp
 * @brief Tests for the outgoing G-code source-comment annotation (M117/M118 exclusion).
 *
 * HelixScreen appends " ; from helixscreen" to every outgoing G-code line for
 * traceability in Klipper logs (annotate_gcode(), duplicated identically in
 * moonraker_client.cpp, moonraker_api_controls.cpp, and moonraker_motion_api.cpp).
 * For most commands Klipper ignores the trailing comment. M117 (display message)
 * and M118 (console echo) are different: Klipper hands the ENTIRE remainder of
 * the line to the command as a literal text payload, without stripping a
 * trailing ";" comment first. Confirmed live: typing "M117 Hello World" in the
 * console made the printer's display message become "Hello World ; from
 * helixscreen", which then rendered corrupted on the HelixScreen UI itself.
 *
 * annotate_gcode() is a private helper in each .cpp's anonymous namespace, so
 * it can't be called directly from a test binary. These tests instead exercise
 * each of the three call sites through their real production entry points:
 *
 *   - moonraker_client.cpp:      MoonrakerClient::gcode_script() — the lowest
 *     level call site, exercised directly against a real (non-mock)
 *     MoonrakerClient with only the outgoing JSON-RPC transport captured, since
 *     MoonrakerClientMock overrides gcode_script() itself and would bypass the
 *     annotation logic under test entirely.
 *   - moonraker_api_controls.cpp: MoonrakerAPI::execute_gcode() — the exact
 *     path the console G-code entry field uses (ui_panel_console.cpp
 *     send_gcode_command() -> MoonrakerAPI::execute_gcode()), reproducing the
 *     live repro. Exercised via MoonrakerClientMock + gcode_script_history().
 *   - moonraker_motion_api.cpp: execute_gcode() is `protected`, only ever
 *     invoked internally with gcode the API itself generates (G28, G1 ...) —
 *     never user-typed text — so no production caller can route M117/M118
 *     through it today. The full discriminating suite lives in the two
 *     reachable copies above; a single smoke test here confirms this copy's
 *     ordinary-command annotation still works post-fix (regression guard for
 *     the identical logic block, without adding test-only access surface for
 *     a path nothing can trigger).
 */

#include "../../include/moonraker_api.h"
#include "../../include/moonraker_client.h"
#include "../../include/moonraker_client_mock.h"
#include "../../include/printer_detector.h"
#include "../../include/printer_state.h"
#include "../../src/api/moonraker_gcode_annotate.h"
#include "../lvgl_test_fixture.h"

#include "../catch_amalgamated.hpp"

using namespace helix;
using json = nlohmann::json;

// Printer type NAME (matched case-insensitively against the printer database
// "name" field) whose firmware re-echoes received G-code — the provenance
// comment must be dropped for it. Kept as a constant so the gate tests and the
// database entry stay in lockstep.
static constexpr const char* kEchoingPrinterName = "FlashForge Adventurer 5X";
static constexpr const char* kNonEchoingPrinterName = "FlashForge Adventurer 5M Pro";

// ============================================================================
// Site 0: the shared pure helper — helix::api::annotate_gcode(gcode, add_comment)
// ============================================================================

TEST_CASE("annotate_gcode: add_comment=false returns the input verbatim",
          "[moonraker][gcode_annotate][gate]") {
    CHECK(helix::api::annotate_gcode("G28", false) == "G28");
    CHECK(helix::api::annotate_gcode("G1 X10 F3000", false) == "G1 X10 F3000");
    // Even a multi-line ordinary script is untouched when the tag is disabled.
    CHECK(helix::api::annotate_gcode("G28\nG1 X10", false) == "G28\nG1 X10");
}

TEST_CASE("annotate_gcode: add_comment=true tags ordinary commands",
          "[moonraker][gcode_annotate][gate]") {
    CHECK(helix::api::annotate_gcode("G28", true) == "G28 ; from helixscreen");
    CHECK(helix::api::annotate_gcode("G1 X10\nG1 Y5", true) ==
          "G1 X10 ; from helixscreen\nG1 Y5 ; from helixscreen");
}

TEST_CASE("annotate_gcode: add_comment=true never tags M117/M118 payloads",
          "[moonraker][gcode_annotate][gate]") {
    // Klipper hands the whole line to M117/M118 as literal text, so a trailing
    // "; from helixscreen" would be stored/echoed verbatim — must be skipped.
    CHECK(helix::api::annotate_gcode("M117 hi", true) == "M117 hi");
    CHECK(helix::api::annotate_gcode("M118 x", true) == "M118 x");
    CHECK(helix::api::annotate_gcode("m117 lower", true) == "m117 lower");
    // But a command that merely starts with the token still gets tagged.
    CHECK(helix::api::annotate_gcode("M1170", true) == "M1170 ; from helixscreen");
}

// ============================================================================
// Site 1: moonraker_client.cpp — MoonrakerClient::gcode_script()
// ============================================================================

namespace {

/// Minimal MoonrakerClient subclass that captures the `script` param of the
/// outgoing printer.gcode.script RPC instead of sending it over a real
/// WebSocket. Overriding only the 2-arg send_jsonrpc() (what gcode_script()
/// calls) leaves the real MoonrakerClient::gcode_script() -> annotate_gcode()
/// path completely intact, unlike MoonrakerClientMock which overrides
/// gcode_script() itself.
class GcodeCaptureClient : public helix::MoonrakerClient {
  public:
    std::string last_script;

    int send_jsonrpc(const std::string& method, const json& params) override {
        if (method == "printer.gcode.script" && params.contains("script") &&
            params["script"].is_string()) {
            last_script = params["script"].get<std::string>();
        }
        return 0;
    }
};

} // namespace

TEST_CASE("moonraker_client.cpp annotate_gcode leaves M117 payload unchanged",
          "[moonraker][gcode_annotate]") {
    GcodeCaptureClient client;

    SECTION("uppercase M117") {
        client.gcode_script("M117 Hello World");
        REQUIRE(client.last_script == "M117 Hello World");
    }

    SECTION("lowercase m117 (Klipper is case-insensitive)") {
        client.gcode_script("m117 hello world");
        REQUIRE(client.last_script == "m117 hello world");
    }

    SECTION("leading whitespace before M117") {
        client.gcode_script("  M117 Hi");
        REQUIRE(client.last_script == "  M117 Hi");
    }

    SECTION("M117 with empty message") {
        client.gcode_script("M117");
        REQUIRE(client.last_script == "M117");
    }
}

TEST_CASE("moonraker_client.cpp annotate_gcode leaves M118 payload unchanged",
          "[moonraker][gcode_annotate]") {
    GcodeCaptureClient client;

    client.gcode_script("M118 status update");
    REQUIRE(client.last_script == "M118 status update");
}

TEST_CASE("moonraker_client.cpp annotate_gcode still annotates ordinary commands",
          "[moonraker][gcode_annotate]") {
    GcodeCaptureClient client;

    SECTION("G28 (guards against over-broad M117/M118 matching)") {
        client.gcode_script("G28");
        REQUIRE(client.last_script == "G28 ; from helixscreen");
    }

    SECTION("a command that merely starts with the M117 token is a different command") {
        // M1170 is not a real Klipper command, but must not be mistaken for M117
        // by a substring/prefix match — it should still get annotated like any
        // other unrecognized command.
        client.gcode_script("M1170");
        REQUIRE(client.last_script == "M1170 ; from helixscreen");
    }
}

TEST_CASE("moonraker_client.cpp annotate_gcode skips only the M117 line in a "
          "multi-line payload",
          "[moonraker][gcode_annotate]") {
    GcodeCaptureClient client;

    client.gcode_script("M117 Status\nG28");
    REQUIRE(client.last_script == "M117 Status\nG28 ; from helixscreen");
}

TEST_CASE("moonraker_client.cpp annotate_gcode skips M118 alongside an annotated "
          "neighbour",
          "[moonraker][gcode_annotate]") {
    GcodeCaptureClient client;

    client.gcode_script("G28\nM118 done homing");
    REQUIRE(client.last_script == "G28 ; from helixscreen\nM118 done homing");
}

// ============================================================================
// Site 2: moonraker_api_controls.cpp — MoonrakerAPI::execute_gcode()
// ============================================================================
// This is the exact path the console's G-code entry field uses, reproducing
// the live repro (typing "M117 Hello World" in the console).

namespace {

class ExecuteGcodeFixture : public LVGLTestFixture {
  public:
    ExecuteGcodeFixture() : mock_client(MoonrakerClientMock::PrinterType::VORON_24) {
        state.init_subjects(false);
        state.set_klippy_state_sync(KlippyState::READY);
        mock_client.connect("ws://mock/websocket", []() {}, []() {});
        api = std::make_unique<MoonrakerAPI>(mock_client, state);
    }

    ~ExecuteGcodeFixture() override {
        mock_client.stop_temperature_simulation();
        mock_client.disconnect();
        api.reset();
    }

    MoonrakerClientMock mock_client;
    PrinterState state;
    std::unique_ptr<MoonrakerAPI> api;
};

} // namespace

TEST_CASE_METHOD(ExecuteGcodeFixture,
                 "console G-code entry (MoonrakerAPI::execute_gcode) leaves M117 unchanged",
                 "[moonraker][gcode_annotate][mock]") {
    api->execute_gcode("M117 Hello World", nullptr, nullptr);

    REQUIRE_FALSE(mock_client.gcode_script_history().empty());
    REQUIRE(mock_client.gcode_script_history().back() == "M117 Hello World");
}

TEST_CASE_METHOD(ExecuteGcodeFixture,
                 "console G-code entry (MoonrakerAPI::execute_gcode) still annotates "
                 "an ordinary command",
                 "[moonraker][gcode_annotate][mock]") {
    api->execute_gcode("G28", nullptr, nullptr);

    REQUIRE_FALSE(mock_client.gcode_script_history().empty());
    REQUIRE(mock_client.gcode_script_history().back() == "G28 ; from helixscreen");
}

// ============================================================================
// Site 3: moonraker_motion_api.cpp — smoke test only (see file header)
// ============================================================================

TEST_CASE_METHOD(ExecuteGcodeFixture,
                 "motion API (MoonrakerMotionAPI::execute_gcode) still annotates a jog move",
                 "[moonraker][gcode_annotate][mock][motion]") {
    bool error_called = false;
    api->motion().move_axis('X', 10.0, 6000.0, nullptr,
                            [&](const MoonrakerError&) { error_called = true; });

    CHECK_FALSE(error_called);
    REQUIRE_FALSE(mock_client.gcode_script_history().empty());
    const std::string& sent = mock_client.gcode_script_history().back();
    REQUIRE(sent.find(" ; from helixscreen") != std::string::npos);
}

// ============================================================================
// Provenance gate: source (console) + firmware echo, both routed through
// MoonrakerAPI::execute_gcode. This is the core behavioral fix.
// ============================================================================

TEST_CASE_METHOD(ExecuteGcodeFixture,
                 "execute_gcode(UserConsole) drops the provenance comment",
                 "[moonraker][gcode_annotate][gate][mock]") {
    // User-typed console G-code must never carry the tag, even on an ordinary
    // (non-echoing) printer that would otherwise be tagged.
    api->execute_gcode("G28", nullptr, nullptr, 0, false,
                       MoonrakerAPI::GcodeSource::UserConsole);

    REQUIRE_FALSE(mock_client.gcode_script_history().empty());
    CHECK(mock_client.gcode_script_history().back() == "G28");
}

TEST_CASE_METHOD(ExecuteGcodeFixture,
                 "execute_gcode(Internal) tags G-code on a non-echoing printer",
                 "[moonraker][gcode_annotate][gate][mock]") {
    // Default source is Internal; a normal printer keeps the tag.
    api->execute_gcode("G28", nullptr, nullptr);

    REQUIRE_FALSE(mock_client.gcode_script_history().empty());
    CHECK(mock_client.gcode_script_history().back() == "G28 ; from helixscreen");
}

TEST_CASE_METHOD(ExecuteGcodeFixture,
                 "execute_gcode(Internal) drops the tag when firmware echoes G-code",
                 "[moonraker][gcode_annotate][gate][mock]") {
    // AD5X re-echoes received G-code into a quoted RESPOND MSG="..."; the
    // trailing "; from helixscreen" would leave an unterminated quote for
    // Klipper. Setting the printer type resolves the DB flag into the atomic.
    state.set_printer_type_sync(kEchoingPrinterName);
    REQUIRE(state.firmware_echoes_gcode());

    api->execute_gcode("G28", nullptr, nullptr);

    REQUIRE_FALSE(mock_client.gcode_script_history().empty());
    CHECK(mock_client.gcode_script_history().back() == "G28");
}

TEST_CASE_METHOD(ExecuteGcodeFixture,
                 "motion execute_gcode drops the tag when firmware echoes G-code",
                 "[moonraker][gcode_annotate][gate][mock][motion]") {
    state.set_printer_type_sync(kEchoingPrinterName);
    REQUIRE(state.firmware_echoes_gcode());

    bool error_called = false;
    api->motion().move_axis('X', 10.0, 6000.0, nullptr,
                            [&](const MoonrakerError&) { error_called = true; });

    CHECK_FALSE(error_called);
    REQUIRE_FALSE(mock_client.gcode_script_history().empty());
    CHECK(mock_client.gcode_script_history().back().find(" ; from helixscreen") ==
          std::string::npos);
}

// ============================================================================
// Phase 1: PrinterDetector DB reader + PrinterState atomic cache
// ============================================================================

TEST_CASE("PrinterDetector::firmware_echoes_gcode reads the database flag",
          "[printer_detector][gate]") {
    CHECK(PrinterDetector::firmware_echoes_gcode(kEchoingPrinterName));
    // AD5M (and its Pro variant) must stay OFF — only the AD5X echoes.
    CHECK_FALSE(PrinterDetector::firmware_echoes_gcode(kNonEchoingPrinterName));
    CHECK_FALSE(PrinterDetector::firmware_echoes_gcode("FlashForge Adventurer 5M"));
    // Unknown / generic printer defaults to false.
    CHECK_FALSE(PrinterDetector::firmware_echoes_gcode("Some Generic Printer"));
    CHECK_FALSE(PrinterDetector::firmware_echoes_gcode(""));
}

TEST_CASE_METHOD(ExecuteGcodeFixture,
                 "PrinterState::firmware_echoes_gcode() reflects the printer type",
                 "[printer_state][gate]") {
    // Default (no type set) is false.
    CHECK_FALSE(state.firmware_echoes_gcode());

    state.set_printer_type_sync(kEchoingPrinterName);
    CHECK(state.firmware_echoes_gcode());

    // Switching to a non-echoing printer clears it.
    state.set_printer_type_sync(kNonEchoingPrinterName);
    CHECK_FALSE(state.firmware_echoes_gcode());
}

// ============================================================================
// Phase 4: MoonrakerError factories
// ============================================================================

TEST_CASE("MoonrakerError::not_ready / validation_error factories set fields",
          "[moonraker][error]") {
    MoonrakerError nr = MoonrakerError::not_ready("printer.gcode.script", "Klipper is halted");
    CHECK(nr.type == MoonrakerErrorType::NOT_READY);
    CHECK(nr.method == "printer.gcode.script");
    CHECK(nr.message == "Klipper is halted");

    MoonrakerError ve = MoonrakerError::validation_error("motion.move", "NaN or Inf value");
    CHECK(ve.type == MoonrakerErrorType::VALIDATION_ERROR);
    CHECK(ve.method == "motion.move");
    CHECK(ve.message == "NaN or Inf value");
}
