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

static const char *TAG = "helixscreen";

void app_main(void) {
    const esp_partition_t *running = esp_ota_get_running_partition();
    ESP_LOGI(TAG, "helixscreen-esp32 booting from partition '%s' @ 0x%08" PRIx32,
             running->label, running->address);

    esp_lcd_panel_handle_t panel = board_display_init();
    // Test pattern: three horizontal color bands drawn via draw_bitmap —
    // proves data-pin order and scanout before LVGL enters the picture.
    static uint16_t line[BOARD_LCD_H_RES];
    for (int y = 0; y < BOARD_LCD_V_RES; y++) {
        uint16_t c = (y < 160) ? 0xF800 : (y < 320) ? 0x07E0 : 0x001F;
        for (int x = 0; x < BOARD_LCD_H_RES; x++) line[x] = c;
        esp_lcd_panel_draw_bitmap(panel, 0, y, BOARD_LCD_H_RES, y + 1, line);
    }
    ESP_LOGI(TAG, "display: test pattern up");
}
