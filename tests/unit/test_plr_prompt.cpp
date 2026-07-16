// SPDX-License-Identifier: GPL-3.0-or-later
//
// Phase 2 tests for the power-loss-recovery UI layer. The connect-time offer
// DECISION is already covered by the pure tests in test_plr_offer.cpp; this file
// covers the new pure helper introduced in Phase 2 (plr_prompt_body) plus the
// null-api safety guard on show_plr_recovery_prompt.
//
// A subject-driven smoke test (drive pl_env_valid -> assert a modal appears) is
// intentionally NOT written here: show_plr_recovery_prompt() requires the XML
// modal_dialog component to be loaded AND the offer path requires a mocked AMS
// SNAPMAKER backend (AmsState::get_backend()->get_type() == SNAPMAKER). That is
// heavy XMLTestFixture + HELIX_MOCK_AMS scaffolding with no existing lightweight
// harness; a green test built on stubs would assert nothing real. The pure
// pieces below are the parts with branching logic worth pinning down.

#include "../../src/ui/ui_plr_prompt.h"

#include "../catch_amalgamated.hpp"

using helix::ui::plr_prompt_body;
using helix::ui::show_plr_recovery_prompt;

TEST_CASE("plr_prompt_body: filename is basename'd, extension stripped, substituted",
          "[plr][prompt]") {
    // Directory prefix stripped, .gcode removed -> "Benchy".
    REQUIRE(plr_prompt_body("gcodes/sub/Benchy.gcode", "Resume {}?", "generic") == "Resume Benchy?");
}

TEST_CASE("plr_prompt_body: bare filename with no directory", "[plr][prompt]") {
    REQUIRE(plr_prompt_body("3DBenchy.gco", "Resume {}?", "generic") == "Resume 3DBenchy?");
}

TEST_CASE("plr_prompt_body: empty path returns the generic body verbatim", "[plr][prompt]") {
    REQUIRE(plr_prompt_body("", "Resume {}?", "generic body") == "generic body");
}

TEST_CASE("plr_prompt_body: null with_file_fmt returns generic", "[plr][prompt]") {
    REQUIRE(plr_prompt_body("part.gcode", nullptr, "generic body") == "generic body");
}

TEST_CASE("plr_prompt_body: null generic body with empty path yields empty string",
          "[plr][prompt]") {
    REQUIRE(plr_prompt_body("", "Resume {}?", nullptr).empty());
}

TEST_CASE("plr_prompt_body: malformed format template falls back to generic (no throw)",
          "[plr][prompt]") {
    // An unmatched '{' makes fmt::format throw; plr_prompt_body must catch it
    // and return the generic body rather than letting it propagate through the
    // LVGL C dispatch frame.
    REQUIRE(plr_prompt_body("part.gcode", "broken {", "safe fallback") == "safe fallback");
}

TEST_CASE("plr_prompt_body: filename that is only an extension is not over-stripped",
          "[plr][prompt]") {
    // strip_gcode_extension only strips when size > ext size, so ".gcode" alone
    // stays intact and is substituted as-is.
    REQUIRE(plr_prompt_body(".gcode", "Resume {}?", "generic") == "Resume .gcode?");
}

TEST_CASE("show_plr_recovery_prompt: null api is a safe no-op", "[plr][prompt]") {
    // The controller calls show_plr_recovery_prompt(get_moonraker_api()), which
    // can be null in a headless context. The null-guard must return before any
    // LVGL / PrinterState access, so this must not crash.
    REQUIRE_NOTHROW(show_plr_recovery_prompt(nullptr));
}
