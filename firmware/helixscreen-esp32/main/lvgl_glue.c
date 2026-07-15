// SPDX-License-Identifier: GPL-3.0-or-later
#include "lvgl_glue.h"
#include "ktouch.h"
#include "ota_health.h"

#include "esp_heap_caps.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "src/xml/lv_xml.h"

static const char *TAG = "lvgl_glue";
static esp_lcd_panel_handle_t s_panel;
static void (*s_ui_build)(void);

// Draw buffers and the UI task stack are static (.bss, internal SRAM,
// reserved at link time) — NOT heap-allocated. WiFi's internal allocations
// fragment the heap run-to-run; after wifi-up the largest contiguous internal
// block measured 31-63KB, so a runtime alloc of a 38KB draw buffer or the
// 32KB UI stack is a boot lottery that abort-looped the device (2026-07-15
// boot-reliability investigation). Link-time reservation cannot fragment.
// 24 lines x 800px x RGB565: sized so both buffers + stack fit alongside
// WiFi and the RGB bounce buffers in 512KB SRAM.
#define UI_DRAW_BUF_LINES 24
#define UI_DRAW_BUF_BYTES \
    (BOARD_LCD_H_RES * UI_DRAW_BUF_LINES * sizeof(lv_color16_t))
LV_ATTRIBUTE_MEM_ALIGN static uint8_t s_draw_buf1[UI_DRAW_BUF_BYTES];
LV_ATTRIBUTE_MEM_ALIGN static uint8_t s_draw_buf2[UI_DRAW_BUF_BYTES];
#define UI_TASK_STACK_BYTES 32768
static StaticTask_t s_ui_tcb;
static StackType_t s_ui_stack[UI_TASK_STACK_BYTES];

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
    esp_lcd_panel_draw_bitmap(s_panel, area->x1, area->y1, area->x2 + 1,
                              area->y2 + 1, px_map);
    lv_display_flush_ready(disp);
}

static uint32_t tick_cb(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

static void ui_task(void *arg) {
    (void)arg;
    s_ui_build();
    ota_health_confirm();
    ESP_LOGI(TAG, "ui: built, entering render loop");
    while (true) {
        uint32_t delay = lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(delay < 5 ? 5 : delay > 50 ? 50 : delay));
    }
}

void lvgl_glue_start(esp_lcd_panel_handle_t panel, void (*ui_build)(void)) {
    s_panel = panel;
    s_ui_build = ui_build;

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

    // Static creation cannot fail — no ENOMEM path, no abort loop.
    xTaskCreateStatic(ui_task, "ui", sizeof(s_ui_stack), NULL, 5, s_ui_stack,
                      &s_ui_tcb);
    // Preserve the never-returns contract app_main relies on, and return the
    // caller task's stack to the heap.
    vTaskDelete(NULL);
}
