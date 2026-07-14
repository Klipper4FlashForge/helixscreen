// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// I2C bus + GT911 + LVGL pointer indev. Call on the UI pthread after lv_init.
void touch_input_init(void);
