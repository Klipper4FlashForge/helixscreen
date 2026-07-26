// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file ui_callback_helpers.h
 * @brief Helpers to reduce boilerplate in panel/overlay callback registration
 *
 * For widget lookup by name, use the FIND_WIDGET / FIND_WIDGET_REQUIRED /
 * FIND_WIDGET_OPTIONAL macros in include/ui/ui_widget_helpers.h.
 *
 * @pattern Batch registration replaces repetitive lv_xml_register_event_cb() calls
 * @threading Main thread only
 */

#pragma once

#include "lvgl/lvgl.h"

#include <spdlog/spdlog.h>

#include <initializer_list>

/**
 * @brief Entry for batch XML event callback registration
 *
 * Pairs a callback name (matching XML event_cb attribute) with
 * its C++ function pointer.
 */
struct XmlCallbackEntry {
    const char* name;
    lv_event_cb_t callback;
};

/**
 * @brief Register multiple XML event callbacks in a single call
 *
 * Replaces repetitive blocks of lv_xml_register_event_cb() calls with
 * a compact table format. All callbacks are registered in the global
 * scope (nullptr component scope).
 *
 * @param callbacks Initializer list of {name, callback} pairs
 *
 * Example:
 * @code
 * register_xml_callbacks({
 *     {"on_home_all",  on_home_all},
 *     {"on_home_x",    on_home_x},
 *     {"on_home_y",    on_home_y},
 * });
 * @endcode
 */
inline void register_xml_callbacks(std::initializer_list<XmlCallbackEntry> callbacks) {
    for (const auto& cb : callbacks) {
        lv_xml_register_event_cb(nullptr, cb.name, cb.callback);
    }
}
