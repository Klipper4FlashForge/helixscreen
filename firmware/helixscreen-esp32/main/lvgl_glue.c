// SPDX-License-Identifier: GPL-3.0-or-later
#include "lvgl_glue.h"
#include "board_display.h"
#include "ktouch.h"
#include "ota_health.h"
#include "touch_input.h"

#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_log.h"
#include "esp_pthread.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "src/xml/lv_xml.h"

#include <pthread.h>

static const char *TAG = "lvgl_glue";
static esp_lcd_panel_handle_t s_panel;
static void (*s_ui_build)(void);
static void (*s_ui_tick)(void);

// D2 tear fix — LVGL double-buffered DIRECT mode over the two RGB framebuffers.
// The panel has num_fbs=2 (board_display.c). LVGL renders each frame's dirty
// areas straight into the BACK framebuffer; flush_cb hands the completed frame
// to esp_lcd, which flips it to scan-out at the next frame boundary. Nothing
// ever writes the framebuffer currently being scanned, so the tearing/glitch
// (and the "persistent" navbar lines, which are the same race on a static
// region) is structurally impossible. LVGL keeps both framebuffers consistent
// itself (direct mode re-renders the last two refreshes' dirty areas into the
// alternating buffer), so no manual front->back copy is needed.
//
// on_vsync gives this binary semaphore at each frame boundary; flush_cb waits
// on it after submitting the frame so LVGL can't start rendering into a buffer
// that's still on screen. The old 12-line internal draw-buffer pair is gone —
// in direct mode the framebuffers ARE the draw buffers — which returns ~38KB of
// internal DRAM (steady-state back over the >=100KB budget).
static SemaphoreHandle_t s_flush_sem;

static bool on_frame_boundary(esp_lcd_panel_handle_t panel,
                              const esp_lcd_rgb_panel_event_data_t *edata, void *user_ctx) {
    (void)panel;
    (void)edata;
    (void)user_ctx;
    BaseType_t high_task_woken = pdFALSE;
    if (s_flush_sem) {
        xSemaphoreGiveFromISR(s_flush_sem, &high_task_woken);
    }
    return high_task_woken == pdTRUE;
}

// XML/expat parsing recurses deeply during component registration and layout;
// the audit ran the full app slice on a 32KB pthread stack. 48KB gives margin
// for the real bring-up + panel construction. Kept INTERNAL (not PSRAM) — the
// UI thread does settings→flash writes, which cannot run from a PSRAM stack.
#define UI_THREAD_STACK_BYTES (48 * 1024)

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
    (void)area;
    // Direct mode: LVGL renders the dirty areas straight into the back
    // framebuffer (px_map). A refresh can flush several dirty areas — only the
    // LAST one submits the frame. For the earlier areas there's nothing to do
    // but mark them ready (they're already in the buffer).
    if (!lv_display_flush_is_last(disp)) {
        lv_display_flush_ready(disp);
        return;
    }
    // Hand the whole completed framebuffer to esp_lcd, which flips it to
    // scan-out at the next frame boundary. Then wait for that boundary so LVGL
    // can't begin rendering into a buffer that's still on screen. Drain any
    // stale give first; bounded wait so a stalled panel can't wedge the loop.
    esp_lcd_panel_draw_bitmap(s_panel, 0, 0, BOARD_LCD_H_RES, BOARD_LCD_V_RES, px_map);
    if (s_flush_sem) {
        xSemaphoreTake(s_flush_sem, 0);
        xSemaphoreTake(s_flush_sem, pdMS_TO_TICKS(100));
    }
    lv_display_flush_ready(disp);
}

static uint32_t tick_cb(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

// The UI thread body. Runs on the pthread created in lvgl_glue_start. Owns the
// ENTIRE display + LVGL bring-up (panel, lv_init, buffers, touch) plus the app
// shell and render loop. board_display_init() allocates the 32KB internal RGB
// bounce DMA buffer here — it's the SECOND internal-DRAM allocation gate (the
// first is the pthread stack), so its pre-alloc heap is logged inside
// board_display_init. Keeping it all on one thread also keeps LVGL access
// sequential (LV_OS_NONE, no locking).
static void *ui_thread_main(void *arg) {
    (void)arg;

    // Panel first (RGB init + bounce buffers). Thread-agnostic hardware setup.
    s_panel = board_display_init();

    // Frame-boundary signal for the double-FB flush (see s_flush_sem/flush_cb).
    // Register before any flush can run. Not IRAM-safe here (the bounce ISR runs
    // with cache on — LCD_RGB_ISR_IRAM_SAFE is off), so no IRAM_ATTR.
    s_flush_sem = xSemaphoreCreateBinary();
    esp_lcd_rgb_panel_event_callbacks_t lcd_cbs = {.on_vsync = on_frame_boundary};
    esp_lcd_rgb_panel_register_event_callbacks(s_panel, &lcd_cbs, NULL);

    lv_init();
    lv_tick_set_cb(tick_cb);
    lv_xml_init();

    ESP_LOGI(TAG, "internal heap at display setup: free=%u largest=%u",
             heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));

    // Direct mode over the panel's two framebuffers (double buffering). LVGL
    // renders into whichever FB is the back buffer; flush_cb flips it.
    void *fb0 = NULL, *fb1 = NULL;
    ESP_ERROR_CHECK(esp_lcd_rgb_panel_get_frame_buffer(s_panel, 2, &fb0, &fb1));
    lv_display_t *disp = lv_display_create(BOARD_LCD_H_RES, BOARD_LCD_V_RES);
    lv_display_set_buffers(disp, fb0, fb1,
                           BOARD_LCD_H_RES * BOARD_LCD_V_RES * sizeof(lv_color16_t),
                           LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(disp, flush_cb);

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

void lvgl_glue_start(void (*ui_build)(void), void (*ui_tick)(void)) {
    s_ui_build = ui_build;
    s_ui_tick = ui_tick;

    // Create the UI pthread as the FIRST sizeable heap allocation of the boot —
    // before the thread body's board_display_init/lv_init and before the net
    // task app_main spawns next. esp_pthread_set_cfg applies to the next
    // pthread_create only. Detached: runs the render loop for the process
    // lifetime, never joined.
    esp_pthread_cfg_t cfg = esp_pthread_get_default_config();
    cfg.stack_size = UI_THREAD_STACK_BYTES;
    cfg.prio = 5;
    cfg.thread_name = "ui";
    esp_err_t cfg_err = esp_pthread_set_cfg(&cfg);
    if (cfg_err != ESP_OK) {
        ESP_LOGE(TAG, "esp_pthread_set_cfg failed: %s", esp_err_to_name(cfg_err));
    }

    // Allocation gate #1 (one-shot, every boot): the 48KB UI stack must fit in
    // `largest`. Below ~48KB, pthread_create fails with ENOMEM (errno 12). The
    // matching gate #2 (RGB bounce DMA) logs inside board_display_init.
    ESP_LOGI(TAG, "internal heap before ui pthread: free=%u largest=%u (need >=%u)",
             heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL), UI_THREAD_STACK_BYTES);

    pthread_t ui_thread;
    int rc = pthread_create(&ui_thread, NULL, ui_thread_main, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "ui pthread_create failed: %d — no UI", rc);
        return;
    }
    pthread_detach(ui_thread);
}
