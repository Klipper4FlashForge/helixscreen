// SPDX-License-Identifier: GPL-3.0-or-later
#include "touch_input.h"
#include "ktouch.h"

#include "driver/i2c_master.h"
#include "esp_lcd_touch_gt911.h"
#include "esp_lcd_panel_io.h"
#include "esp_log.h"
#include "lvgl.h"

static const char *TAG = "touch";
static esp_lcd_touch_handle_t s_touch;

static void indev_read(lv_indev_t *indev, lv_indev_data_t *data) {
    (void)indev;
    esp_lcd_touch_read_data(s_touch);
    uint16_t x, y;
    uint8_t cnt = 0;
    if (esp_lcd_touch_get_coordinates(s_touch, &x, &y, NULL, &cnt, 1) && cnt) {
        data->point.x = x;
        data->point.y = y;
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

void touch_input_init(void) {
    i2c_master_bus_config_t bus_cfg = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = 0,
        .scl_io_num = BOARD_TOUCH_I2C_SCL,
        .sda_io_num = BOARD_TOUCH_I2C_SDA,
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t bus;
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &bus));

    esp_lcd_panel_io_handle_t io;
    esp_lcd_panel_io_i2c_config_t io_cfg = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(bus, &io_cfg, &io));

    esp_lcd_touch_config_t tp_cfg = {
        .x_max = BOARD_LCD_H_RES,
        .y_max = BOARD_LCD_V_RES,
        .rst_gpio_num = BOARD_TOUCH_PIN_RST,
        .int_gpio_num = BOARD_TOUCH_PIN_IRQ,
    };
    ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_gt911(io, &tp_cfg, &s_touch));

    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, indev_read);
    ESP_LOGI(TAG, "GT911 indev registered");
}
