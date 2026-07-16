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

// Draw buffers are static (reserved at link time — NOT runtime heap-allocated,
// so they can't lose the fragmentation lottery a runtime alloc would).
//
// LOCATION — INTERNAL DRAM, not PSRAM. Rendering to/from PSRAM draw buffers
// contends with the RGB framebuffer scan-out on the shared octal-PSRAM bus:
// every partial redraw (e.g. the 1Hz temperature widget updates) streams pixels
// through PSRAM while the RGB bounce ISR needs deterministic framebuffer reads,
// so refills miss and the flush bands tear (the "lines through navbar icons"
// Preston saw are scan-out tearing in the bands the navbar shares with updating
// widgets, recovered by RESTART_IN_VSYNC → visible glitch), and every blend is
// bus-throttled (slow paint). Internal draw buffers remove that contention.
//
// SIZE — the arithmetic tradeoff against boot-time internal-DRAM pressure. The
// full 24-line pair (76.8KB) is why these were pushed to PSRAM originally: the
// 48KB UI-pthread stack and 32KB RGB bounce DMA must still find contiguous
// internal blocks (see the "internal heap before ..." gate logs; measured
// steady-state largest-free-block is ~31KB — the heap is fragmented, so the
// bounce buffer is the tight constraint). The 12-line pair (2x19.2KB = 38.4KB)
// keeps double-buffering (render N+1 while flushing N) at half the internal
// cost. To trade RAM for fewer flush passes, raise UI_DRAW_BUF_LINES to 24 IF
// the gate logs show the bounce buffer still fits; UI_DRAW_BUF_PSRAM 1 reverts
// to the old PSRAM placement if the internal pressure proves too high.
#define UI_DRAW_BUF_PSRAM 0 // 1 = PSRAM (old placement); 0 = internal DRAM
#define UI_DRAW_BUF_LINES 12
#define UI_DRAW_BUF_BYTES \
    (BOARD_LCD_H_RES * UI_DRAW_BUF_LINES * sizeof(lv_color16_t))
#if UI_DRAW_BUF_PSRAM
// PSRAM requires CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY=y; without it
// EXT_RAM_BSS_ATTR is a silent no-op and the buffers fall back to internal.
#define UI_DRAW_BUF_ATTR EXT_RAM_BSS_ATTR LV_ATTRIBUTE_MEM_ALIGN
#else
#define UI_DRAW_BUF_ATTR LV_ATTRIBUTE_MEM_ALIGN
#endif
UI_DRAW_BUF_ATTR static uint8_t s_draw_buf1[UI_DRAW_BUF_BYTES];
UI_DRAW_BUF_ATTR static uint8_t s_draw_buf2[UI_DRAW_BUF_BYTES];

// XML/expat parsing recurses deeply during component registration and layout;
// the audit ran the full app slice on a 32KB pthread stack. 48KB gives margin
// for the real bring-up + panel construction. Kept INTERNAL (not PSRAM) — the
// UI thread does settings→flash writes, which cannot run from a PSRAM stack.
#define UI_THREAD_STACK_BYTES (48 * 1024)

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
    // Gate on the next vsync so the copy into the live framebuffer starts during
    // blanking (see s_vsync_sem). Drain any STALE give first (vsyncs fire during
    // idle and leave the binary sem set — without the drain, the first flush
    // after an idle gap, e.g. the 1Hz temp update, would take the stale token
    // and copy immediately, which is the exact periodic tear we're fixing).
    // Then wait for a FRESH vsync. Bounded — a stalled vsync must never hang the
    // render loop; fall through and copy if it doesn't arrive.
    if (s_vsync_sem) {
        xSemaphoreTake(s_vsync_sem, 0);
        xSemaphoreTake(s_vsync_sem, pdMS_TO_TICKS(100));
    }
    esp_lcd_panel_draw_bitmap(s_panel, area->x1, area->y1, area->x2 + 1,
                              area->y2 + 1, px_map);
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

    lv_display_t *disp = lv_display_create(BOARD_LCD_H_RES, BOARD_LCD_V_RES);
    lv_display_set_buffers(disp, s_draw_buf1, s_draw_buf2, UI_DRAW_BUF_BYTES,
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
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
