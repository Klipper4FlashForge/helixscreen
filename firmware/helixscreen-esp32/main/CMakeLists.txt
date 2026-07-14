# SPDX-License-Identifier: GPL-3.0-or-later
idf_component_register(SRCS "app_main.c" "board_display.c"
                       PRIV_REQUIRES esp_timer app_update esp_lcd driver
                       INCLUDE_DIRS "." "../boards")
