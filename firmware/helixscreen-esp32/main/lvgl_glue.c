// SPDX-License-Identifier: GPL-3.0-or-later
#include "lvgl_glue.h"
#include "ktouch.h"
#include "ota_health.h"
#include "touch_input.h"

#include "esp_heap_caps.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_log.h"
#include "esp_pthread.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "src/xml/lv_xml.h"

#include <pthread.h>

static const char *TAG = "lvgl_glue";
static esp_lcd_panel_handle_t s_panel;
static void (*s_ui_build)(void);
static void (*s_ui_tick)(void);

// Draw buffers are static (.bss, internal SRAM, reserved at link time) — NOT
// heap-allocated. WiFi's internal allocations fragment the heap run-to-run;
// after wifi-up the largest contiguous internal block measured 31-63KB, so a
// runtime alloc of a 38KB draw buffer is a boot lottery that abort-looped the
// device (2026-07-15 boot-reliability investigation). Link-time reservation
// cannot fragment. 24 lines x 800px x RGB565: sized so both buffers fit
// alongside WiFi and the RGB bounce buffers in 512KB SRAM.
//
// The UI thread is now a pthread (the app core needs pthread_self() /
// std::this_thread::get_id()), not a static FreeRTOS task. Its ~48KB stack is a
// heap allocation — but app_main creates it BEFORE any network task, while the
// internal heap is still unfragmented, so it lands reliably (the same pattern
// the static reservation used to guarantee).
#define UI_DRAW_BUF_LINES 24
#define UI_DRAW_BUF_BYTES \
    (BOARD_LCD_H_RES * UI_DRAW_BUF_LINES * sizeof(lv_color16_t))
LV_ATTRIBUTE_MEM_ALIGN static uint8_t s_draw_buf1[UI_DRAW_BUF_BYTES];
LV_ATTRIBUTE_MEM_ALIGN static uint8_t s_draw_buf2[UI_DRAW_BUF_BYTES];

// XML/expat parsing recurses deeply during component registration and layout;
// the audit ran the full app slice on a 32KB pthread stack. 48KB gives margin
// for the real bring-up + panel construction.
#define UI_THREAD_STACK_BYTES (48 * 1024)

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
    esp_lcd_panel_draw_bitmap(s_panel, area->x1, area->y1, area->x2 + 1,
                              area->y2 + 1, px_map);
    lv_display_flush_ready(disp);
}

static uint32_t tick_cb(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

static void *ui_thread_main(void *arg) {
    (void)arg;
    // Touch indev registration must run on the UI thread after lv_init.
    touch_input_init();
    s_ui_build();
    ota_health_confirm();
    ESP_LOGI(TAG, "ui: built, entering render loop");
    while (true) {
        uint32_t delay = lv_timer_handler();
        if (s_ui_tick) {
            s_ui_tick();
        }
        vTaskDelay(pdMS_TO_TICKS(delay < 5 ? 5 : delay > 50 ? 50 : delay));
    }
    return NULL;
}

void lvgl_glue_start(esp_lcd_panel_handle_t panel, void (*ui_build)(void),
                     void (*ui_tick)(void)) {
    s_panel = panel;
    s_ui_build = ui_build;
    s_ui_tick = ui_tick;

    lv_init();
    lv_tick_set_cb(tick_cb);
    lv_xml_init();

    ESP_LOGI(TAG, "internal heap at display setup: free=%u largest=%u",
             heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));

    lv_display_t *disp = lv_display_create(BOARD_LCD_H_RES, BOARD_LCD_V_RES);
    lv_display_set_buffers(disp, s_draw_buf1, s_draw_buf2, UI_DRAW_BUF_BYTES,
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(disp, flush_cb);

    // Create the UI pthread. esp_pthread_set_cfg applies to the next
    // pthread_create only. Detached: it runs the render loop for the process
    // lifetime and is never joined.
    esp_pthread_cfg_t cfg = esp_pthread_get_default_config();
    cfg.stack_size = UI_THREAD_STACK_BYTES;
    cfg.prio = 5;
    cfg.thread_name = "ui";
    esp_err_t cfg_err = esp_pthread_set_cfg(&cfg);
    if (cfg_err != ESP_OK) {
        ESP_LOGE(TAG, "esp_pthread_set_cfg failed: %s", esp_err_to_name(cfg_err));
    }

    pthread_t ui_thread;
    int rc = pthread_create(&ui_thread, NULL, ui_thread_main, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "ui pthread_create failed: %d — no UI", rc);
        return;
    }
    pthread_detach(ui_thread);
}
