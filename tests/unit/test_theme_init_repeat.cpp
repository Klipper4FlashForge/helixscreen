// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * theme_manager_init() repeat guard. The registration pass re-reads and
 * expat-parses the whole ui_xml/ tree, and the test suite paid that cost once
 * per fixture instance (once per SECTION leaf) for a theme that never changed
 * between them — ~26% of main-thread time. An unchanged target (same display
 * pointer, resolution and mode, theme still initialized) now skips the pass.
 */

#include "../lvgl_ui_test_fixture.h"
#include "theme_manager.h"

#include "../catch_amalgamated.hpp"

TEST_CASE_METHOD(LVGLUITestFixture, "theme_manager_init skips repeats for an unchanged target",
                 "[1526][theme]") {
    const int before = theme_manager_full_init_count();

    // The fixture's own init_theme() ran as part of construction; a manual
    // repeat for the same display/mode must not rebuild.
    theme_manager_init(lv_display_get_default(), false);
    REQUIRE(theme_manager_full_init_count() == before);

    // A second identical repeat is also a no-op.
    theme_manager_init(lv_display_get_default(), false);
    REQUIRE(theme_manager_full_init_count() == before);

    // A mode change is a real change.
    theme_manager_init(lv_display_get_default(), true);
    REQUIRE(theme_manager_full_init_count() == before + 1);

    // And so is flipping back — this is also what restores the light mode the
    // rest of the fixture expects.
    theme_manager_init(lv_display_get_default(), false);
    REQUIRE(theme_manager_full_init_count() == before + 2);
}

TEST_CASE_METHOD(LVGLUITestFixture, "a deinitialized theme runs the full init again",
                 "[1526][theme]") {
    const int before = theme_manager_full_init_count();

    theme_manager_deinit();
    theme_manager_init(lv_display_get_default(), false);
    REQUIRE(theme_manager_full_init_count() == before + 1);

    // Deinit cleared the subject-initialized flag, so the NEXT repeat skips
    // again — the recovery is one rebuild, not a permanently disabled guard.
    theme_manager_init(lv_display_get_default(), false);
    REQUIRE(theme_manager_full_init_count() == before + 1);
}
