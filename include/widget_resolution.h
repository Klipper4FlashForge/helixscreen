// Copyright (C) 2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/**
 * @file widget_resolution.h
 * @brief Deciding which widget a `helix-screen ctl` click/set_value acts on.
 *
 * Split out of remote_control_server.cpp so the rule can be unit-tested: the
 * server object is excluded from the test link (mk/tests.mk) because it drags
 * in the transports and the toast manager, but the resolution rule is where
 * the interesting mistakes live and it needs nothing but LVGL.
 */

#include <string>
#include <vector>

struct _lv_obj_t;
typedef struct _lv_obj_t lv_obj_t;

namespace helix {

/**
 * @brief Split an indexed name token like "toggle[3]" into name and ordinal.
 *
 * The ordinal disambiguates same-named siblings, which LVGL permits: name
 * resolution only auto-indexes names ending in '#' (lv_obj_tree.c), so two
 * children both named "toggle" both resolve to "toggle".
 *
 * @param token     The token as typed.
 * @param out_name  Receives the name with the bracket suffix stripped.
 * @param out_index Receives the 0-based ordinal.
 * @return false when @p token carries no well-formed "[<digits>]" suffix, in
 *         which case the outputs are untouched and the token is a plain name.
 *         A malformed suffix ("t[", "t[]", "t[x]", "t[-1]") is not an error
 *         here — no widget name in the tree contains a bracket, so the caller
 *         simply fails to find it, which is the same outcome as any typo.
 */
bool parse_indexed_name(const std::string& token, std::string& out_name, int& out_index);

/**
 * @brief Emit a locator for @p o that a human can read, retype, and reuse.
 *
 * Each segment is, in order of preference: the widget's resolved name when it
 * is unique among its siblings; "name[k]" when it is not; the bare child index
 * when the widget has no name of its own. So a locator reads
 * "s/main_content/settings_list/toggle[1]" rather than "s/15/1/1/2".
 *
 * Name segments also survive layout churn that index segments do not: inserting
 * a row above the target shifts every index after it, but not a name.
 *
 * @param o    The widget to describe.
 * @param base Emit relative to this ancestor, with no root prefix. nullptr
 *             emits an absolute locator rooted at "s" (active screen) or "t"
 *             (top layer). When @p base is not an ancestor of @p o, the result
 *             falls back to absolute.
 */
std::string path_of(lv_obj_t* o, lv_obj_t* base = nullptr);

/**
 * @brief How one widget is addressed within its parent — one path segment.
 *
 * The building block path_of() concatenates. Exposed because describe_screen
 * walks the tree top-down and appends a segment per level, rather than paying
 * path_of()'s walk-to-the-root for every widget it lists.
 */
std::string path_segment_for(lv_obj_t* o);

/**
 * @brief Resolve a locator emitted by path_of() back to a live widget.
 *
 * Segment dispatch mirrors what path_of() emits: all-digits addresses a child
 * index, "name[k]" the k-th same-named child, anything else the uniquely-named
 * one. An all-numeric locator therefore still resolves exactly as it did before
 * names were introduced, so anything holding an older path keeps working.
 *
 * @param path      "s/..." or "t/..." for an absolute locator; otherwise
 *                  resolved relative to @p base.
 * @param base      Starting point for a relative locator; nullptr means the
 *                  active screen. Ignored for an absolute locator.
 * @param ambiguous Optional; filled with every candidate when a name segment
 *                  matches more than one sibling and no ordinal was given, so
 *                  the caller can report instead of silently taking the first.
 * @return The addressed widget, or nullptr if any segment fails to resolve.
 */
lv_obj_t* resolve_path(const std::string& path, lv_obj_t* base = nullptr,
                       std::vector<lv_obj_t*>* ambiguous = nullptr);

/**
 * @brief A widget carrying a value the user manipulates directly.
 *
 * As opposed to a container that merely happens to be clickable. Composite
 * rows (a settings toggle row, a dropdown row) are shells wrapping one of
 * these.
 */
bool is_value_control(lv_obj_t* o);

/**
 * @brief Whether @p o carries a click handler somebody explicitly attached.
 *
 * Class-level handlers are deliberately not counted: every lv_obj has one, so
 * counting them would make the predicate universally true. Only an added
 * callback whose filter admits LV_EVENT_CLICKED means "clicking this does
 * something".
 */
bool has_own_click_handler(lv_obj_t* o);

/**
 * @brief Resolve the widget a `click`/`set_value` should actually land on.
 *
 * A composite settings row is a shell wrapping the control the caller meant,
 * so addressing the row descends to that control. The descent is bounded: a
 * target that is already a click target in its own right is acted on
 * literally, and the search never tunnels past a descendant that is itself a
 * click target. Without those bounds a full-screen context-menu backdrop
 * resolves into whatever control the menu happens to contain and dispatches
 * its event — real G-code from a click whose only intent was to dismiss the
 * menu (#1179).
 *
 * @param target       The addressed widget.
 * @param descended_to Set to the resolved child when it differs from target,
 *                     nullptr otherwise. Required.
 * @param ambiguous    Optional; filled with every candidate when more than one
 *                     exists, so the caller can report instead of guessing.
 * @return The widget to act on — null only when @p target is null.
 */
lv_obj_t* resolve_actionable(lv_obj_t* target, lv_obj_t** descended_to,
                             std::vector<lv_obj_t*>* ambiguous);

} // namespace helix
