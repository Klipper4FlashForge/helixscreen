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

// Vsync gate for the flush (D2 tear fix). The single PSRAM framebuffer is
// scanned out continuously; esp_lcd_panel_draw_bitmap() memcpys a dirty region
// straight into it, so a copy landing under the active scan line tears the
// band (persistent on the static navbar, periodic on the 1Hz-updating panel).
// on_vsync gives this binary semaphore each frame; flush_cb waits for it so the
// copy starts at frame top, finishing well before scan-out reaches a
// mid-screen region. Single FB kept; small partial redraws stay cheap.
static SemaphoreHandle_t s_vsync_sem;

static bool flush_on_vsync(esp_lcd_panel_handle_t panel,
                           const esp_lcd_rgb_panel_event_data_t *edata, void *user_ctx) {
    (void)panel;
    (void)edata;
    (void)user_ctx;
    BaseType_t high_task_woken = pdFALSE;
    if (s_vsync_sem) {
        xSemaphoreGiveFromISR(s_vsync_sem, &high_task_woken);
    }
    return high_task_woken == pdTRUE;
}

// LVGL draw buffer — ONE full-frame buffer, driven in DIRECT mode.
//
// Why: in PARTIAL mode LVGL flushed ~40 small dirty chunks per refresh, each a
// separate memcpy into the single live framebuffer racing the RGB bounce ISR's
// scan-out read — so paint was visibly progressive (chunk-by-chunk) AND any
// ungated chunk could tear (the residual navbar/glitch artifacts). DIRECT mode
// renders the whole refresh into this persistent full-frame buffer; flush_cb
// then copies the refresh's dirty band to the panel framebuffer in ONE
// contiguous top-to-bottom draw_bitmap gated to a fresh vsync. Result: the frame
// lands atomically (the real perceptual speed win) with ONE beam race per cycle
// (full head start) instead of ~40 chances to lose to PSRAM bus contention.
//
// PSRAM: 800x480x2 = 768KB won't fit internal DRAM, so it lives in PSRAM. That
// reintroduces some PSRAM-bandwidth contention with the bounce scan-out (why the
// old partial buffers were internal), but num_fbs stays 1 — no driver
// flip/bounce conflict, which is what killed the double-FB attempt (see
// board_display.c). Worst case here is a recoverable RESTART_IN_VSYNC glitch,
// not permanent BIST desync. Allocated at runtime (heap_caps) so it doesn't grow
// .bss or the boot internal-DRAM gate.
#define UI_DRAW_BUF_BYTES ((size_t)BOARD_LCD_H_RES * BOARD_LCD_V_RES * sizeof(lv_color16_t))
static uint8_t *s_draw_buf;

// Accumulated dirty Y-range for the current refresh cycle (always full width).
// Copied as one contiguous full-width band on the cycle's last flush.
static int32_t s_dirty_y1;
static int32_t s_dirty_y2;
static bool s_dirty_valid = false;

// XML/expat parsing recurses deeply during component registration and layout;
// the audit ran the full app slice on a 32KB pthread stack. 48KB gives margin
// for the real bring-up + panel construction. Kept INTERNAL (not PSRAM) — the
// UI thread does settings→flash writes, which cannot run from a PSRAM stack.
#define UI_THREAD_STACK_BYTES (48 * 1024)

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
    (void)px_map; // DIRECT mode: LVGL rendered straight into s_draw_buf.
    // Accumulate this refresh's dirty Y-range (full width). Copy nothing until
    // the cycle's last flush — that keeps the whole frame's update as ONE copy.
    if (!s_dirty_valid) {
        s_dirty_y1 = area->y1;
        s_dirty_y2 = area->y2;
        s_dirty_valid = true;
    } else {
        if (area->y1 < s_dirty_y1)
            s_dirty_y1 = area->y1;
        if (area->y2 > s_dirty_y2)
            s_dirty_y2 = area->y2;
    }

    if (!lv_display_flush_is_last(disp)) {
        lv_display_flush_ready(disp);
        return;
    }

    // Last flush of the cycle. Gate on a FRESH vsync (drain any stale token from
    // idle vsyncs first — back-to-back cycles like the deferred-panel loading
    // scrim then the panel-swap repaint must not inherit one; bounded 100ms so a
    // stalled panel can't hang the render loop), then push the whole dirty band
    // in ONE contiguous top-to-bottom copy. Full-width rows are tightly packed in
    // the full-frame buffer, so draw_bitmap of [0, y1 .. W, y2+1] with the source
    // offset by y1 rows is the correct packed span — one beam race per frame with
    // a full head start, frame lands atomically. See s_draw_buf.
    if (s_vsync_sem) {
        xSemaphoreTake(s_vsync_sem, 0);
        xSemaphoreTake(s_vsync_sem, pdMS_TO_TICKS(100));
    }
    if (s_dirty_valid) {
        uint8_t *band = s_draw_buf + (size_t)s_dirty_y1 * BOARD_LCD_H_RES * sizeof(lv_color16_t);
        esp_lcd_panel_draw_bitmap(s_panel, 0, s_dirty_y1, BOARD_LCD_H_RES, s_dirty_y2 + 1, band);
        s_dirty_valid = false;
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

    // Vsync gate for tear-free flushing (see s_vsync_sem / flush_cb). Register
    // before any flush can run. on_vsync is not IRAM-safe here (the bounce ISR
    // runs with cache on — LCD_RGB_ISR_IRAM_SAFE is off), so no IRAM_ATTR.
    s_vsync_sem = xSemaphoreCreateBinary();
    esp_lcd_rgb_panel_event_callbacks_t lcd_cbs = {.on_vsync = flush_on_vsync};
    esp_lcd_rgb_panel_register_event_callbacks(s_panel, &lcd_cbs, NULL);

    lv_init();
    lv_tick_set_cb(tick_cb);
    lv_xml_init();

    ESP_LOGI(TAG, "internal heap at display setup: free=%u largest=%u",
             heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));

    // Full-frame PSRAM draw buffer for DIRECT mode (see s_draw_buf). 64-byte
    // aligned for PSRAM cache-line coherency when esp_lcd reads it.
    s_draw_buf = heap_caps_aligned_alloc(64, UI_DRAW_BUF_BYTES, MALLOC_CAP_SPIRAM);
    if (!s_draw_buf) {
        ESP_LOGE(TAG, "FATAL: no PSRAM for %u-byte draw buffer (free=%u largest=%u)",
                 (unsigned)UI_DRAW_BUF_BYTES, (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
        abort();
    }
    ESP_LOGI(TAG, "draw buffer: %u bytes PSRAM @ %p (psram free=%u)", (unsigned)UI_DRAW_BUF_BYTES,
             s_draw_buf, (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    lv_display_t *disp = lv_display_create(BOARD_LCD_H_RES, BOARD_LCD_V_RES);
    lv_display_set_buffers(disp, s_draw_buf, NULL, UI_DRAW_BUF_BYTES,
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
