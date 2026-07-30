// Copyright (C) 2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "widget_resolution.h"

#include "lvgl.h"

// lv_event_dsc_t's filter is not exposed by the public API, and
// has_own_click_handler() needs it to tell a click target from scaffolding.
#include "lvgl/src/misc/lv_event_private.h"

namespace helix {

bool is_value_control(lv_obj_t* o) {
    return lv_obj_check_type(o, &lv_switch_class) || lv_obj_check_type(o, &lv_checkbox_class) ||
           lv_obj_check_type(o, &lv_slider_class) || lv_obj_check_type(o, &lv_arc_class) ||
           lv_obj_check_type(o, &lv_dropdown_class) || lv_obj_check_type(o, &lv_textarea_class);
}

bool has_own_click_handler(lv_obj_t* o) {
    if (!o) {
        return false;
    }
    // Mirrors the dispatch test in lv_event.c's event_send_core: mask off the
    // PREPROCESS flag, LV_EVENT_ALL matches everything, and entries already
    // marked for deletion will never fire.
    uint32_t count = lv_obj_get_event_count(o);
    for (uint32_t i = 0; i < count; ++i) {
        lv_event_dsc_t* dsc = lv_obj_get_event_dsc(o, i);
        if (!dsc || (dsc->filter & LV_EVENT_MARKED_DELETING) != 0) {
            continue;
        }
        uint32_t filter = dsc->filter & ~(LV_EVENT_PREPROCESS | LV_EVENT_MARKED_DELETING);
        if (filter == LV_EVENT_ALL || filter == LV_EVENT_CLICKED) {
            return true;
        }
    }
    return false;
}

namespace {

// Collect visible value-controls in a subtree (excluding the root itself).
// Hidden subtrees are skipped for the same reason describe_screen skips them:
// they are not on screen, so they are not targets.
//
// Recursion stops at any descendant that is itself a click target. Such a
// child is a destination in its own right, not scaffolding wrapping the
// control the caller meant, so tunnelling past it would resolve a click onto
// something the caller never addressed (#1179).
void collect_value_controls(lv_obj_t* parent, std::vector<lv_obj_t*>& out) {
    if (!parent) {
        return;
    }
    uint32_t count = lv_obj_get_child_count(parent);
    for (uint32_t i = 0; i < count; ++i) {
        lv_obj_t* child = lv_obj_get_child(parent, i);
        if (!child || lv_obj_has_flag(child, LV_OBJ_FLAG_HIDDEN)) {
            continue;
        }
        if (is_value_control(child)) {
            out.push_back(child);
            continue; // a control's own internals are never a separate target
        }
        if (has_own_click_handler(child)) {
            continue;
        }
        collect_value_controls(child, out);
    }
}

} // namespace

// `click row_foo` on a composite settings row used to send CLICKED to the row
// container — which does nothing, because the control the user meant is the
// switch nested inside it. Prefer a value-control descendant when the target
// itself isn't one; fall back to the literal target (a category row that opens
// an overlay is clickable and has no value-control, and must stay that way).
//
// Descent is only ever a repair for a target that does nothing on its own. A
// target with its own click handler is already the thing the caller addressed,
// so it is acted on literally — this is what keeps `click <backdrop>` a
// dismissal instead of resolving into whatever the overlay happens to contain
// (#1179).
lv_obj_t* resolve_actionable(lv_obj_t* target, lv_obj_t** descended_to,
                             std::vector<lv_obj_t*>* ambiguous) {
    *descended_to = nullptr;
    if (!target || is_value_control(target) || has_own_click_handler(target)) {
        return target;
    }
    std::vector<lv_obj_t*> found;
    collect_value_controls(target, found);
    if (found.size() == 1) {
        *descended_to = found[0];
        return found[0];
    }
    if (found.size() > 1 && ambiguous) {
        *ambiguous = found;
    }
    // Zero candidates, or too many to choose between: act on the target itself
    // if it is clickable at all, so buttons and overlay-opening rows are
    // unaffected by this resolution step.
    return target;
}

} // namespace helix
