// SPDX-License-Identifier: GPL-3.0-or-later
//
// HelixScreen ESP32 target — entry point.
// Phase order: board display → LVGL → touch → OTA health → UI task.
// Plan 1 fills these in task by task; this skeleton proves the partition
// layout and toolchain.

#include "board_display.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "ktouch.h"
#include "lvgl.h"
#include "lvgl_glue.h"
#include "sdkconfig.h"
#include "storage_mount.h"
#include "touch_input.h"

#if CONFIG_HELIX_NET_HIL
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Defined in net_hil.cpp (Task 10). Test-only network HIL scenario, disabled
// by default — see main/Kconfig.projbuild.
extern void net_hil_start(void);

static void net_hil_task(void* arg) {
    (void)arg;
    net_hil_start();
    vTaskDelete(NULL);
}
#endif

static const char* TAG = "helixscreen";

static void tap_cb(lv_event_t* e) {
    static int taps = 0;
    lv_obj_t* btn_label = lv_event_get_user_data(e);
    lv_label_set_text_fmt(btn_label, "tap me: %d", ++taps);
    LV_LOG_USER("tap %d", taps);
}

static void ui_build_hello(void) {
    touch_input_init();
    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_hex(0x0d1117), 0);
    lv_obj_t* card = lv_obj_create(lv_screen_active());
    lv_obj_set_size(card, 400, 120);
    lv_obj_center(card);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x1a2332), 0);
    lv_obj_t* label = lv_label_create(card);
    lv_label_set_text(label, "helixscreen-esp32 foundation");
    lv_obj_set_style_text_color(label, lv_color_hex(0x4fc3f7), 0);
    lv_obj_center(label);

    lv_obj_t* btn = lv_button_create(lv_screen_active());
    lv_obj_set_size(btn, 200, 60);
    lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, 0, -40);
    lv_obj_t* btn_label = lv_label_create(btn);
    lv_label_set_text(btn_label, "tap me: 0");
    lv_obj_center(btn_label);
    // Raw event_cb is fine here: foundation bring-up predates the XML engine
    // (declarative-UI rules bind when it arrives in Plan 4).
    lv_obj_add_event_cb(btn, tap_cb, LV_EVENT_CLICKED, btn_label);
}

// Defined in the helixnet component. Referenced (never called) so the linker
// keeps the EspMoonrakerClient in the image and the size gate accounts for its
// real footprint before Task 10 wires the transport into the boot flow.
extern void helixnet_link_probe(void);
static void (*volatile s_helixnet_keep)(void) = helixnet_link_probe;

// Defined in the helixapp component. Referenced (never called) so the linker
// keeps the curated Core+AMS app core in the image and the size gate accounts
// for its footprint before Task 6 wires the real UI bring-up.
extern void helixapp_link_probe(void);
static void (*volatile s_helixapp_keep)(void) = helixapp_link_probe;

void app_main(void) {
    (void)s_helixnet_keep;
    (void)s_helixapp_keep;
    const esp_partition_t* running = esp_ota_get_running_partition();
    ESP_LOGI(TAG, "helixscreen-esp32 booting from partition '%s' @ 0x%08" PRIx32, running->label,
             running->address);

    // Assets/config must be mounted before anything reads them — the UI
    // boots off /assets (packed frogfs container) starting with a later
    // task. Non-fatal here: the placeholder UI below doesn't need it yet,
    // but a failure is loud in the log rather than silently missing later.
    esp_err_t storage_err = storage_mount();
    if (storage_err != ESP_OK) {
        ESP_LOGE(TAG, "storage_mount failed: %s (asset/config storage unavailable)",
                 esp_err_to_name(storage_err));
    }

#if CONFIG_HELIX_NET_HIL
    // Network bring-up runs in its own task: the UI must come up
    // unconditionally, never behind WiFi — association without DHCP (weak
    // RSSI, dead AP, wrong creds) otherwise leaves the device on a black
    // screen indefinitely. The earlier wifi-before-LCD serialization guarded
    // against a suspected RF-cal x RGB-DMA boot wedge that reset-cause
    // instrumentation disproved: that wedge was an internal-RAM ENOMEM abort
    // loop, fixed structurally by static allocation in lvgl_glue.c.
    xTaskCreate(net_hil_task, "net_hil_start", 8192, NULL, 5, NULL);
#endif

    esp_lcd_panel_handle_t panel = board_display_init();
    lvgl_glue_start(panel, ui_build_hello);
}
