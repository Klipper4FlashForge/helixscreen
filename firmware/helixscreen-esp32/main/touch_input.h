// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <stdbool.h>

// I2C bus + GT911 + LVGL pointer indev. Call on the UI pthread after lv_init.
// Returns false if the controller could not be brought up (typically a
// degraded touch ribbon failing the GT911 probe); in that case no indev is
// registered and the board runs display-only rather than resetting.
bool touch_input_init(void);

// Whether touch_input_init() registered a working pointer indev.
bool touch_input_available(void);
