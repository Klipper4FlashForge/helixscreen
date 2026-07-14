// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "esp_lcd_panel_ops.h"

// Initializes LVGL (lv_init + lv_xml_init — LVGL does NOT call the latter;
// skipping it is heap-corruption-shaped TLSF crashes), creates the display
// with 2x80-line PSRAM draw buffers, then spawns the UI pthread: ui_build()
// once, then the lv_timer_handler loop. Everything that touches LVGL or app
// code after this call happens on that pthread (ESP-IDF pthread_self()
// asserts from raw FreeRTOS tasks).
void lvgl_glue_start(esp_lcd_panel_handle_t panel, void (*ui_build)(void));
