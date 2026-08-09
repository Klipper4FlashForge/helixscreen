// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <lvgl/lvgl.h>

#include <functional>
#include <string>

namespace helix {
namespace ui {

/**
 * @brief Create a selectable LED chip widget
 *
 * Creates a pill-shaped chip with optional check icon for multi-select LED interfaces.
 * Uses theme tokens for consistent styling.
 *
 * @param parent Parent container for the chip
 * @param led_name Technical LED name (e.g., "neopixel chamber_light")
 * @param display_name Friendly display name (e.g., "Chamber Light")
 * @param selected Whether the chip starts selected
 * @param on_click Callback when chip is clicked (receives technical led_name)
 * @return Pointer to created chip widget
 */
lv_obj_t* create_led_chip(lv_obj_t* parent, const std::string& led_name,
                          const std::string& display_name, bool selected,
                          std::function<void(const std::string&)> on_click);

} // namespace ui
} // namespace helix
