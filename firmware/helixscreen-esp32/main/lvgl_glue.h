// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "esp_lcd_panel_ops.h"

// Spawns the UI pthread and returns immediately. The pthread body does the
// ENTIRE UI bring-up — board_display_init(), lv_init + lv_xml_init (LVGL does
// NOT call the latter; skipping it is heap-corruption-shaped TLSF crashes), the
// display with static internal draw buffers, touch, ui_build() once, then the
// lv_timer_handler loop (ui_tick, if non-NULL, runs each iteration after the
// timer handler). Everything that touches the panel/LVGL/app code runs on that
// pthread — the app core calls std::this_thread::get_id() (spdlog, main-thread
// detectors), which asserts on a raw FreeRTOS task, so a pthread context is
// mandatory.
//
// CALL THIS FIRST in app_main — before board_display_init and before spawning
// any network task. pthread_create is the boot's first sizeable heap
// allocation: the ~48KB internal stack must land on a pristine heap. Deferring
// it behind display/LVGL init or WiFi startup makes it a boot lottery that
// fails with ENOMEM (errno 12) — the boot-reliability pattern. The panel and
// LVGL allocations then happen inside the thread body, after the stack is
// already reserved.
void lvgl_glue_start(void (*ui_build)(void), void (*ui_tick)(void));
