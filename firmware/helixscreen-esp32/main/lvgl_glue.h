// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "esp_lcd_panel_ops.h"

// Initializes LVGL (lv_init + lv_xml_init — LVGL does NOT call the latter;
// skipping it is heap-corruption-shaped TLSF crashes), creates the display
// with static internal draw buffers, then spawns the UI pthread: ui_build()
// once, then the lv_timer_handler loop (ui_tick, if non-NULL, runs each
// iteration after the timer handler). Everything that touches LVGL or app code
// after this call happens on that pthread — the app core calls
// std::this_thread::get_id() (spdlog, main-thread detectors), which asserts on
// a raw FreeRTOS task, so a pthread context is mandatory.
//
// Returns after the pthread is created (does NOT delete the caller). Call this
// from app_main BEFORE spawning any network task: the pthread's stack is a
// large internal-heap allocation that must land while the heap is unfragmented
// — after WiFi start the largest contiguous internal block is too small and the
// allocation becomes a boot lottery (the boot-reliability pattern).
void lvgl_glue_start(esp_lcd_panel_handle_t panel, void (*ui_build)(void),
                     void (*ui_tick)(void));
