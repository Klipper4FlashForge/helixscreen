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

#include <vector>

struct _lv_obj_t;
typedef struct _lv_obj_t lv_obj_t;

namespace helix {

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
