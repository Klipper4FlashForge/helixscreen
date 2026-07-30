// Copyright (C) 2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Target resolution for `helix-screen ctl`. Both behaviours here exist because
// driving the UI silently no-op'd: `click <name>` resolved to a same-named
// widget in an overlay stacked *behind* the visible one and reported success,
// and a path copied out of `ls` was rejected unless prefixed with '@'.

#include "../lvgl_test_fixture.h"
#include "remote_client.h"
#include "widget_resolution.h"

#include "../catch_amalgamated.hpp"

// --- bare path locators -------------------------------------------------

TEST_CASE("ctl target: bare describe_screen paths are recognised", "[remote][ctl]") {
    // The exact shape `ls` prints, usable without remembering '@'.
    REQUIRE(helix::is_bare_path("s/0"));
    REQUIRE(helix::is_bare_path("s/18/0/1"));
    REQUIRE(helix::is_bare_path("t/0"));
    REQUIRE(helix::is_bare_path("t/1/2/3/4/5"));
}

TEST_CASE("ctl target: widget names are not mistaken for paths", "[remote][ctl]") {
    // Real widget names from the codebase — none may be read as a locator,
    // or clicking them would resolve a child index instead.
    REQUIRE_FALSE(helix::is_bare_path("swatch_0"));
    REQUIRE_FALSE(helix::is_bare_path("action_button_2"));
    REQUIRE_FALSE(helix::is_bare_path("row_theme_settings"));
    REQUIRE_FALSE(helix::is_bare_path("s"));
    REQUIRE_FALSE(helix::is_bare_path("t"));
    REQUIRE_FALSE(helix::is_bare_path(""));
    // Names that merely start with s/t must not be swallowed.
    REQUIRE_FALSE(helix::is_bare_path("slot_grid"));
    REQUIRE_FALSE(helix::is_bare_path("theme_swatch_grid"));
}

TEST_CASE("ctl target: malformed locators are rejected", "[remote][ctl]") {
    REQUIRE_FALSE(helix::is_bare_path("s/"));     // no segment
    REQUIRE_FALSE(helix::is_bare_path("s//1"));   // empty segment
    REQUIRE_FALSE(helix::is_bare_path("s/1/"));   // trailing slash
    REQUIRE_FALSE(helix::is_bare_path("s/1/x"));  // non-numeric segment
    REQUIRE_FALSE(helix::is_bare_path("x/1"));    // unknown root
    REQUIRE_FALSE(helix::is_bare_path("s/1 /2")); // stray space
    REQUIRE_FALSE(helix::is_bare_path("row_*"));  // glob stays a glob
}

// --- topmost-visible name resolution ------------------------------------
//
// resolve_widget() is file-static in remote_control_server.cpp, so this pins
// the LVGL-level invariant it depends on: lv_obj_find_by_name() returns the
// FIRST depth-first match, which on a stacked screen is the bottom overlay.
// If LVGL ever changed that, the ctl fix would be silently redundant — and if
// someone "simplifies" resolve_widget back to lv_obj_find_by_name, this test
// documents exactly why that regresses.

TEST_CASE_METHOD(LVGLTestFixture, "lv_obj_find_by_name returns the bottom-most stacked match",
                 "[remote][ctl]") {
    lv_obj_t* screen = lv_screen_active();

    // Two overlays stacked on the screen, each containing a button with the
    // same name — the exact shape that broke `ctl click action_button_2`.
    lv_obj_t* lower = lv_obj_create(screen);
    lv_obj_t* lower_btn = lv_obj_create(lower);
    lv_obj_set_name(lower_btn, "action_button_2");

    lv_obj_t* upper = lv_obj_create(screen);
    lv_obj_t* upper_btn = lv_obj_create(upper);
    lv_obj_set_name(upper_btn, "action_button_2");

    REQUIRE(lv_obj_get_index(upper) > lv_obj_get_index(lower));

    // The naive lookup finds the one the user cannot see.
    lv_obj_t* naive = lv_obj_find_by_name(screen, "action_button_2");
    REQUIRE(naive == lower_btn);
    REQUIRE(naive != upper_btn);
}

TEST_CASE_METHOD(LVGLTestFixture, "hidden subtrees still contain findable names", "[remote][ctl]") {
    lv_obj_t* screen = lv_screen_active();

    // A hidden overlay's children are still reachable by name — which is why
    // resolution has to filter on LV_OBJ_FLAG_HIDDEN rather than trust the
    // lookup. Clicking one of these is always a no-op.
    lv_obj_t* hidden = lv_obj_create(screen);
    lv_obj_add_flag(hidden, LV_OBJ_FLAG_HIDDEN);
    lv_obj_t* buried = lv_obj_create(hidden);
    lv_obj_set_name(buried, "btn_only_in_hidden");

    REQUIRE(lv_obj_has_flag(hidden, LV_OBJ_FLAG_HIDDEN));
    REQUIRE(lv_obj_find_by_name(screen, "btn_only_in_hidden") == buried);
}

// --- bounded container-to-control descent (#1179) -----------------------
//
// resolve_actionable() descends a composite row to the control inside it, so
// `click <row>` behaves like tapping the switch. Unbounded, that same descent
// walks a context-menu backdrop's entire subtree and resolves onto whatever
// control the menu contains — dispatching its event and, on a live printer,
// sending G-code from a click whose only intent was to dismiss the menu.

namespace {

void noop_event_cb(lv_event_t*) {}

// The ams_context_menu shape: a full-screen backdrop with a dismiss handler,
// wrapping a card that holds one visible dropdown. The second dropdown row is
// hidden (no toolchanger), which is what leaves exactly one candidate and made
// the descent look unambiguous.
struct ContextMenuTree {
    lv_obj_t* backdrop;
    lv_obj_t* card;
    lv_obj_t* visible_dropdown;
    lv_obj_t* hidden_dropdown;
};

ContextMenuTree build_context_menu(lv_obj_t* parent) {
    ContextMenuTree t{};
    t.backdrop = lv_obj_create(parent);
    lv_obj_add_flag(t.backdrop, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(t.backdrop, noop_event_cb, LV_EVENT_CLICKED, nullptr);

    t.card = lv_obj_create(t.backdrop);
    lv_obj_add_flag(t.card, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t* hidden_row = lv_obj_create(t.card);
    lv_obj_add_flag(hidden_row, LV_OBJ_FLAG_HIDDEN);
    t.hidden_dropdown = lv_dropdown_create(hidden_row);

    lv_obj_t* visible_row = lv_obj_create(t.card);
    t.visible_dropdown = lv_dropdown_create(visible_row);
    return t;
}

} // namespace

TEST_CASE_METHOD(LVGLTestFixture, "ctl: a backdrop click never resolves into the menu it covers",
                 "[remote][ctl][1179]") {
    ContextMenuTree t = build_context_menu(lv_screen_active());

    lv_obj_t* descended = nullptr;
    std::vector<lv_obj_t*> ambiguous;
    lv_obj_t* resolved = helix::resolve_actionable(t.backdrop, &descended, &ambiguous);

    // The backdrop dismisses; that IS the action. Resolving anywhere else
    // fires an unrelated widget's handler.
    REQUIRE(resolved == t.backdrop);
    REQUIRE(descended == nullptr);
    REQUIRE(resolved != t.visible_dropdown);
    REQUIRE(ambiguous.empty());
}

TEST_CASE_METHOD(LVGLTestFixture, "ctl: descent stops at a nested click target",
                 "[remote][ctl][1179]") {
    // Same tree minus the backdrop's own handler: the card below it is still a
    // click target of its own, so the search must not tunnel through it even
    // though nothing above it claims the click.
    lv_obj_t* backdrop = lv_obj_create(lv_screen_active());
    lv_obj_add_flag(backdrop, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t* card = lv_obj_create(backdrop);
    lv_obj_add_event_cb(card, noop_event_cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* buried = lv_dropdown_create(card);

    lv_obj_t* descended = nullptr;
    lv_obj_t* resolved = helix::resolve_actionable(backdrop, &descended, nullptr);

    REQUIRE(resolved == backdrop);
    REQUIRE(descended == nullptr);
    REQUIRE(resolved != buried);
}

TEST_CASE_METHOD(LVGLTestFixture, "ctl: a composite row still descends to its control",
                 "[remote][ctl][1179]") {
    // The behaviour the descent exists for, and the reason the bound is a
    // handler test rather than a depth or geometry cap: a settings row that
    // does nothing on its own must still reach the switch nested inside it,
    // through as much non-interactive scaffolding as the layout uses.
    lv_obj_t* row = lv_obj_create(lv_screen_active());
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE); // clickable, but no handler
    lv_obj_t* inner = lv_obj_create(row);
    lv_obj_t* deeper = lv_obj_create(inner);
    lv_obj_t* sw = lv_switch_create(deeper);

    lv_obj_t* descended = nullptr;
    std::vector<lv_obj_t*> ambiguous;
    lv_obj_t* resolved = helix::resolve_actionable(row, &descended, &ambiguous);

    REQUIRE(resolved == sw);
    REQUIRE(descended == sw);
    REQUIRE(ambiguous.empty());
}

TEST_CASE_METHOD(LVGLTestFixture, "ctl: a value control is returned as itself",
                 "[remote][ctl][1179]") {
    // Addressing the control directly must be unaffected — including a
    // dropdown, whose own internals must never be mistaken for a nested
    // target now that the search stops at value controls.
    lv_obj_t* dd = lv_dropdown_create(lv_screen_active());

    lv_obj_t* descended = nullptr;
    lv_obj_t* resolved = helix::resolve_actionable(dd, &descended, nullptr);

    REQUIRE(resolved == dd);
    REQUIRE(descended == nullptr);
}

TEST_CASE_METHOD(LVGLTestFixture, "ctl: several candidates are reported, not guessed between",
                 "[remote][ctl][1179]") {
    // Pre-existing contract, pinned because the new bound changes what the
    // search reaches: two visible controls under a handler-less container are
    // still ambiguous rather than silently resolved to the first.
    lv_obj_t* box = lv_obj_create(lv_screen_active());
    lv_obj_t* a = lv_switch_create(box);
    lv_obj_t* b = lv_slider_create(box);

    lv_obj_t* descended = nullptr;
    std::vector<lv_obj_t*> ambiguous;
    lv_obj_t* resolved = helix::resolve_actionable(box, &descended, &ambiguous);

    REQUIRE(resolved == box);
    REQUIRE(descended == nullptr);
    REQUIRE(ambiguous.size() == 2);
    REQUIRE(ambiguous[0] == a);
    REQUIRE(ambiguous[1] == b);
}
