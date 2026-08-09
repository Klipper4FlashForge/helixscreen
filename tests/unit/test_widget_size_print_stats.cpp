// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_widget_size_print_stats.cpp
 * @brief print_stats picks its 4-way mode from physical pixels on two
 * independent axes (width, height), not colspan/rowspan.
 *
 * `PrintStatsWidget` has no `widget_obj_` guard at all, and its
 * `on_size_changed` writes only a subject (`print_stats_size_mode`) — no
 * object lookups to assert. The XML (`panel_widget_print_stats.xml`) binds
 * all four layout branches off that subject via `bind_flag_if_not_eq`, so
 * asserting the subject alone covers everything the predicate drives.
 *
 * Four cases isolate the 4-way, 2-axis predicate, each pairing pixels for
 * one target mode with a colspan/rowspan that the *old* span-based predicate
 * would have resolved to a *different* mode — so an implementation that
 * still reads spans fails here instead of passing by coincidence.
 */

#include "../helix_test_fixture.h"
#include "panel_widget_manager.h"
#include "panel_widget_size.h"
#include "src/ui/panel_widgets/print_stats_widget.h"

#include "../catch_amalgamated.hpp"

using namespace helix;
using namespace helix::widget_size;

namespace {
int print_stats_size_mode() {
    auto* subject = lv_xml_get_subject(nullptr, "print_stats_size_mode");
    REQUIRE(subject != nullptr);
    return lv_subject_get_int(subject);
}
} // namespace

TEST_CASE_METHOD(HelixTestFixture, "print_stats mode follows pixels on both axes, not spans",
                 "[widget_size][print_stats]") {
    // Widget-owned subjects (print_stats_size_mode, ...) are registered
    // lazily; construct the widget only after they exist.
    PanelWidgetManager::instance().init_widget_subjects();

    PrintStatsWidget w;

    // Mode 0 (narrow compact): both axes below threshold. Contradicting
    // span: 1x3 (old predicate: rowspan<=1(false) -> rowspan<=1(false) ->
    // colspan<=2(true) -> mode 1).
    w.on_size_changed(1, 3, W_WIDE - 1, H_TALL - 1);
    CHECK(print_stats_size_mode() == 0);

    // Mode 3 (wide compact): height below threshold regardless of width —
    // the wide-but-short case that must NOT land on mode 2. Contradicting
    // span: 1x1 (old predicate: rowspan<=1 && colspan<=2 -> mode 0).
    w.on_size_changed(1, 1, W_WIDE, H_TALL - 1);
    CHECK(print_stats_size_mode() == 3);

    // Mode 1 (2x2 grid): width below threshold, height at/over threshold.
    // Contradicting span: 1x1 (old predicate -> mode 0).
    w.on_size_changed(1, 1, W_WIDE - 1, H_TALL);
    CHECK(print_stats_size_mode() == 1);

    // Mode 2 (3x2 full): both axes at/over threshold. Contradicting span:
    // 1x1 (old predicate -> mode 0).
    w.on_size_changed(1, 1, W_WIDE, H_TALL);
    CHECK(print_stats_size_mode() == 2);
}

TEST_CASE_METHOD(HelixTestFixture,
                 "print_stats mode1 (2x2 grid) never fires at print_stats' own minimum "
                 "reachable width on Large/XLarge",
                 "[widget_size][print_stats]") {
    // print_stats' registry entry (panel_widget_registry.cpp) sets
    // min_colspan=2 — grid_edit_mode.cpp enforces that as a hard floor on
    // resize, so unlike clock/camera/fan_stack/favorite_macro this widget
    // can never actually reach a 1-column width. Its true minimum reachable
    // size is colspan=2, rowspan=1 (min_rowspan=1). On Large/XLarge a
    // 2-column width alone already clears W_WIDE (span2 = 221px/276px, vs.
    // W_WIDE = 205), so mode1 ("2x2 grid", which needs width < W_WIDE) is
    // structurally unreachable there — the Large/XLarge single-row height
    // inflation that breaks other widgets' H_TALL check cannot manifest as
    // print_stats' suspected "2x2 grid crammed into a narrow cell" defect,
    // because print_stats is never narrow enough on those two tiers to
    // reach mode1 in the first place.
    PanelWidgetManager::instance().init_widget_subjects();
    PrintStatsWidget w;

    // Large tier, min reachable size (colspan=2, rowspan=1): width=221
    // (>= W_WIDE), height=141 (>= H_TALL) -> falls straight to mode 2 (full),
    // never mode 1.
    w.on_size_changed(2, 1, 221, 141);
    CHECK(print_stats_size_mode() == 2);

    // XLarge tier, same story: width=276, height=169.
    w.on_size_changed(2, 1, 276, 169);
    CHECK(print_stats_size_mode() == 2);
}
