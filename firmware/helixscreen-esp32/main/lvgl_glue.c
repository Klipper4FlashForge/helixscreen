// SPDX-License-Identifier: GPL-3.0-or-later
#include "lvgl_glue.h"
#include "ktouch.h"

#include <pthread.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "src/xml/lv_xml.h"

static const char *TAG = "lvgl_glue";
static esp_lcd_panel_handle_t s_panel;
static void (*s_ui_build)(void);

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
    esp_lcd_panel_draw_bitmap(s_panel, area->x1, area->y1, area->x2 + 1,
                              area->y2 + 1, px_map);
    lv_display_flush_ready(disp);
}

static uint32_t tick_cb(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

static void *ui_thread(void *arg) {
    (void)arg;
    s_ui_build();
    ESP_LOGI(TAG, "ui: built, entering render loop");
    while (true) {
        uint32_t delay = lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(delay < 5 ? 5 : delay > 50 ? 50 : delay));
    }
    return NULL;
}

void lvgl_glue_start(esp_lcd_panel_handle_t panel, void (*ui_build)(void)) {
    s_panel = panel;
    s_ui_build = ui_build;

    lv_init();
    lv_tick_set_cb(tick_cb);
    lv_xml_init();

    lv_display_t *disp = lv_display_create(BOARD_LCD_H_RES, BOARD_LCD_V_RES);
    size_t buf_px = BOARD_LCD_H_RES * 80;
    // LV_DRAW_BUF_ALIGN is 16; heap_caps_malloc is only 8-aligned.
    lv_color_t *buf1 = heap_caps_aligned_alloc(16, buf_px * sizeof(lv_color16_t),
                                               MALLOC_CAP_SPIRAM);
    lv_color_t *buf2 = heap_caps_aligned_alloc(16, buf_px * sizeof(lv_color16_t),
                                               MALLOC_CAP_SPIRAM);
    assert(buf1 && buf2);
    lv_display_set_buffers(disp, buf1, buf2, buf_px * sizeof(lv_color16_t),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(disp, flush_cb);

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 32768);
    pthread_t t;
    int err = pthread_create(&t, &attr, ui_thread, NULL);
    if (err != 0) {
        ESP_LOGE(TAG, "ui pthread_create failed: %d", err);
        abort();
    }
    pthread_join(t, NULL);
}
