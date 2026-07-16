// SPDX-License-Identifier: GPL-3.0-or-later
//
// HelixScreen ESP32 target — entry point.
// Boot order: storage mount → board display → LVGL + UI pthread (the real
// HelixScreen shell) → network task. The UI pthread is created BEFORE any
// network task so its ~48KB stack lands while the internal heap is still
// unfragmented (the boot-reliability pattern — WiFi startup fragments the heap
// and a late large internal alloc becomes a boot lottery).

#include "app_boot.h"
#include "board_display.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "ktouch.h"
#include "lvgl_glue.h"
#include "sdkconfig.h"
#include "storage_mount.h"

#if CONFIG_HELIX_NET_HIL
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Defined in net_hil.cpp (Plan 3 Task 10). Test-only network HIL scenario,
// disabled by default — see main/Kconfig.projbuild.
extern void net_hil_start(void);

static void net_hil_task(void* arg) {
    (void)arg;
    net_hil_start();
    vTaskDelete(NULL);
}
#endif

static const char* TAG = "helixscreen";

// Defined in the helixnet component. Referenced (never called) so the linker
// keeps the EspMoonrakerClient in the image; the real transport is wired into
// MoonrakerManager's ESP arm from the app core (app_boot.cpp).
extern void helixnet_link_probe(void);
static void (*volatile s_helixnet_keep)(void) = helixnet_link_probe;

void app_main(void) {
    (void)s_helixnet_keep;
    const esp_partition_t* running = esp_ota_get_running_partition();
    ESP_LOGI(TAG, "helixscreen-esp32 booting from partition '%s' @ 0x%08" PRIx32, running->label,
             running->address);

    // Assets (/assets, read-only frogfs) and config (/config, writable
    // LittleFS) must be mounted before the UI reads them. Fatal-ish: the shell
    // boots off /assets, so a failure here is loud rather than a silent missing
    // file later.
    esp_err_t storage_err = storage_mount();
    if (storage_err != ESP_OK) {
        ESP_LOGE(TAG, "storage_mount failed: %s (asset/config storage unavailable)",
                 esp_err_to_name(storage_err));
    }

    // Bring up the panel, then LVGL + the UI pthread that runs the real shell.
    // This happens BEFORE the network task spawn below so the pthread stack
    // allocation cannot lose the WiFi heap-fragmentation lottery.
    esp_lcd_panel_handle_t panel = board_display_init();
    lvgl_glue_start(panel, app_boot_ui, app_boot_tick);

#if CONFIG_HELIX_NET_HIL
    // Network bring-up runs in its own task: the UI must come up
    // unconditionally, never behind WiFi — association without DHCP (weak RSSI,
    // dead AP, wrong creds) otherwise leaves the device on a black screen
    // indefinitely.
    xTaskCreate(net_hil_task, "net_hil_start", 8192, NULL, 5, NULL);
#endif
}
