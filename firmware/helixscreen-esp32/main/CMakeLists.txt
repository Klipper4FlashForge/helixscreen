# SPDX-License-Identifier: GPL-3.0-or-later
idf_component_register(SRCS "app_main.c" "board_display.c" "lvgl_glue.c" "touch_input.c" "ota_health.c"
                       PRIV_REQUIRES esp_timer app_update esp_lcd driver helixcore helixnet pthread
                       INCLUDE_DIRS "." "../boards")
