// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_vertical_breakpoint_subject.cpp
 * @brief ui_breakpoint_v carries the vertical tier; ui_breakpoint keeps the cramped one.
 *
 * #1209 put a second ladder behind an allow-list of px *tokens*, but nothing
 * published it as a subject, so every `bind_flag_if_eq` / `bind_style_if` in
 * ui_xml/ still saw min(width, height) only. A 320x1480 panel therefore reported
 * TINY and no declarative binding could reach the 1480px of height.
 *
 * The load-bearing assertion here is that the two subjects DISAGREE on an
 * ultratall panel. A test that only checked they agree would pass with this
 * feature reverted, because they agree by construction everywhere else:
 * landscape and square displays have min(w,h) == h.
 *
 * ui_breakpoint itself must NOT move — every existing ref_value in ui_xml/ is
 * written against the cramped tier. See test_vertical_breakpoint_tokens.cpp.
 */

#include "ui_breakpoint.h"

#include "../test_fixtures.h"
#include "helix-xml/src/xml/lv_xml.h"
#include "lvgl/lvgl.h"
#include "theme_manager.h"

#include "../catch_amalgamated.hpp"

namespace {

/// Bufferless display: the axis accessors only read the resolution.
lv_display_t* make_test_display(int32_t w, int32_t h) {
    return lv_display_create(w, h);
}

int subject_value(const char* name) {
    lv_subject_t* s = lv_xml_get_subject(nullptr, name);
    REQUIRE(s != nullptr);
    return lv_subject_get_int(s);
}

/// Puts the fixture's display geometry back on the way out.
///
/// theme_manager writes the responsive px consts into a SHARED XML scope, and
/// the fixture does not re-register them per test, so anything here that points
/// the theme manager at another display leaks into every later test in the
/// binary. Observed: button_height latched at the xxlarge 96 while
/// test_vertical_breakpoint_tokens.cpp asked for the medium 52 — a failure in a
/// file this one never touches. RAII so no section can forget it.
struct RestoreDisplayConsts {
    lv_display_t* prev = lv_display_get_default();

    ~RestoreDisplayConsts() {
        if (prev != nullptr) {
            lv_display_set_default(prev);
            theme_manager_refresh_layout_constants(prev);
        }
    }
};

} // namespace

TEST_CASE_METHOD(XMLTestFixture, "ui_breakpoint_v reads the vertical axis",
                 "[theme][breakpoints][1209][vertical-subject]") {
    RestoreDisplayConsts restore;

    SECTION("ultratall portrait — the two ladders diverge, which is the whole point") {
        lv_display_t* d = make_test_display(320, 1480);
        theme_manager_refresh_layout_constants(d);

        CHECK(subject_value("ui_breakpoint") == to_int(UiBreakpoint::Tiny));
        CHECK(subject_value("ui_breakpoint_v") == to_int(UiBreakpoint::XXLarge));

        lv_display_delete(d);
    }

    SECTION("landscape — both ladders agree, zero regression") {
        for (auto wh :
             {std::pair<int32_t, int32_t>{800, 480}, std::pair<int32_t, int32_t>{1024, 600},
              std::pair<int32_t, int32_t>{480, 272}}) {
            INFO(wh.first << "x" << wh.second);
            lv_display_t* d = make_test_display(wh.first, wh.second);
            theme_manager_refresh_layout_constants(d);

            CHECK(subject_value("ui_breakpoint_v") == subject_value("ui_breakpoint"));

            lv_display_delete(d);
        }
    }

    SECTION("the STARTUP publish reads the vertical axis, not only the rotation path") {
        // Regression guard for a coverage gap found by mutation: every other
        // section here drives theme_manager_refresh_layout_constants(), so
        // breaking the axis in theme_manager_init() left all of them green. A
        // display that is never rotated would have carried the cramped tier for
        // its whole session.
        //
        // theme_manager_init() writes the responsive px consts into the SHARED
        // XML scope and the fixture does not re-init an already-initialised
        // theme manager, so calling it here leaks this section's tier into every
        // later test in the binary (seen once: button_height stuck at the
        // xxlarge 96 while test_vertical_breakpoint_tokens.cpp asked for the
        // medium 52). Restore the fixture's own geometry before leaving.
        lv_display_t* d = make_test_display(320, 1480);
        theme_manager_init(d, theme_manager_is_dark_mode());

        CHECK(subject_value("ui_breakpoint_v") == to_int(UiBreakpoint::XXLarge));
        CHECK(subject_value("ui_breakpoint") == to_int(UiBreakpoint::Tiny));

        lv_display_delete(d);
    }

    SECTION("rotation republishes it — a stale vertical tier survives an axis swap") {
        // The failure this pins: updating ui_breakpoint on rotation but forgetting
        // ui_breakpoint_v leaves the pre-rotation tier latched, so a panel rotated
        // from 320x1480 to 1480x320 keeps claiming it has 1480px to stack into.
        lv_display_t* tall = make_test_display(320, 1480);
        theme_manager_refresh_layout_constants(tall);
        REQUIRE(subject_value("ui_breakpoint_v") == to_int(UiBreakpoint::XXLarge));
        lv_display_delete(tall);

        lv_display_t* wide = make_test_display(1480, 320);
        theme_manager_refresh_layout_constants(wide);
        CHECK(subject_value("ui_breakpoint_v") == to_int(UiBreakpoint::Tiny));
        lv_display_delete(wide);
    }
}

TEST_CASE_METHOD(XMLTestFixture, "the preparing-overlay reserve threshold matches the measurements",
                 "[theme][breakpoints][1209][vertical-subject]") {
    RestoreDisplayConsts restore;

    // Measured (ctl geom, preparing_visible == 1, strip's extra line present —
    // the conservative case): reserving #metadata_clip_height leaves 50px at
    // 240x320 and 114px at 480x272 against a 112px stack, but 166px or more from
    // 272x480 up. `ui_breakpoint_v ge 3` is the cut that separates them, and
    // nothing in the sample sits on the boundary.
    struct Case {
        int32_t w;
        int32_t h;
        bool reserves;
    };
    const Case cases[] = {
        {240, 320, false}, {480, 272, false}, {272, 480, true}, {480, 800, true}, {320, 1480, true},
    };

    for (const auto& c : cases) {
        INFO(c.w << "x" << c.h);
        lv_display_t* d = make_test_display(c.w, c.h);
        theme_manager_refresh_layout_constants(d);

        CHECK((subject_value("ui_breakpoint_v") >= to_int(UiBreakpoint::Medium)) == c.reserves);

        lv_display_delete(d);
    }
}
