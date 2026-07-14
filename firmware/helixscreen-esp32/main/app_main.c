// SPDX-License-Identifier: GPL-3.0-or-later
//
// HelixScreen ESP32 target — entry point.
// Phase order: board display → LVGL → touch → OTA health → UI task.
// Plan 1 fills these in task by task; this skeleton proves the partition
// layout and toolchain.

#include "esp_log.h"
#include "esp_ota_ops.h"

#include "board_display.h"
#include "ktouch.h"
#include "lvgl_glue.h"
#include "lvgl.h"

static const char *TAG = "helixscreen";

static void ui_build_hello(void) {
    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_hex(0x0d1117), 0);
    lv_obj_t *card = lv_obj_create(lv_screen_active());
    lv_obj_set_size(card, 400, 120);
    lv_obj_center(card);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x1a2332), 0);
    lv_obj_t *label = lv_label_create(card);
    lv_label_set_text(label, "helixscreen-esp32 foundation");
    lv_obj_set_style_text_color(label, lv_color_hex(0x4fc3f7), 0);
    lv_obj_center(label);
}

void app_main(void) {
    const esp_partition_t *running = esp_ota_get_running_partition();
    ESP_LOGI(TAG, "helixscreen-esp32 booting from partition '%s' @ 0x%08" PRIx32,
             running->label, running->address);

    esp_lcd_panel_handle_t panel = board_display_init();
    lvgl_glue_start(panel, ui_build_hello);
}
