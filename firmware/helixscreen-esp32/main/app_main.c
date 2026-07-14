// SPDX-License-Identifier: GPL-3.0-or-later
//
// HelixScreen ESP32 target — entry point.
// Phase order: board display → LVGL → touch → OTA health → UI task.
// Plan 1 fills these in task by task; this skeleton proves the partition
// layout and toolchain.

#include "esp_log.h"
#include "esp_ota_ops.h"

static const char *TAG = "helixscreen";

void app_main(void) {
    const esp_partition_t *running = esp_ota_get_running_partition();
    ESP_LOGI(TAG, "helixscreen-esp32 booting from partition '%s' @ 0x%08" PRIx32,
             running->label, running->address);
}
