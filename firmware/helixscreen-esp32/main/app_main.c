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
#include "touch_input.h"

static const char *TAG = "helixscreen";

static void tap_cb(lv_event_t *e) {
    static int taps = 0;
    lv_obj_t *btn_label = lv_event_get_user_data(e);
    lv_label_set_text_fmt(btn_label, "tap me: %d", ++taps);
    LV_LOG_USER("tap %d", taps);
}

static void ui_build_hello(void) {
    touch_input_init();
    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_hex(0x0d1117), 0);
    lv_obj_t *card = lv_obj_create(lv_screen_active());
    lv_obj_set_size(card, 400, 120);
    lv_obj_center(card);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x1a2332), 0);
    lv_obj_t *label = lv_label_create(card);
    lv_label_set_text(label, "helixscreen-esp32 foundation");
    lv_obj_set_style_text_color(label, lv_color_hex(0x4fc3f7), 0);
    lv_obj_center(label);

    lv_obj_t *btn = lv_button_create(lv_screen_active());
    lv_obj_set_size(btn, 200, 60);
    lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, 0, -40);
    lv_obj_t *btn_label = lv_label_create(btn);
    lv_label_set_text(btn_label, "tap me: 0");
    lv_obj_center(btn_label);
    // Raw event_cb is fine here: foundation bring-up predates the XML engine
    // (declarative-UI rules bind when it arrives in Plan 4).
    lv_obj_add_event_cb(btn, tap_cb, LV_EVENT_CLICKED, btn_label);
}

void app_main(void) {
    const esp_partition_t *running = esp_ota_get_running_partition();
    ESP_LOGI(TAG, "helixscreen-esp32 booting from partition '%s' @ 0x%08" PRIx32,
             running->label, running->address);

    esp_lcd_panel_handle_t panel = board_display_init();
    lvgl_glue_start(panel, ui_build_hello);
}
