# SPDX-License-Identifier: GPL-3.0-or-later
idf_component_register(SRCS "app_main.c" "board_display.c" "lvgl_glue.c" "touch_input.c" "ota_health.c"
                             "storage_mount.c" "net_hil.cpp"
                       PRIV_REQUIRES esp_timer app_update esp_lcd driver helixcore helixnet pthread
                                     esp_wifi esp_netif esp_event nvs_flash joltwallet__littlefs
                       INCLUDE_DIRS "." "../boards")

# LittleFS "storage" partition image: built from the staging tree the desktop
# script assembles (minified ui_xml, config JSON, printer images). Configure
# time only checks that the staging dir exists — running the python script is
# a separate, explicit step (see scripts/esp32_stage_assets.py), not something
# CMake shells out to, so a stale/missing staging dir fails with a clear
# message instead of silently packing whatever is (or isn't) on disk.
set(LITTLEFS_STAGING_DIR ${CMAKE_BINARY_DIR}/littlefs_staging)
if(NOT EXISTS ${LITTLEFS_STAGING_DIR})
    message(FATAL_ERROR
        "LittleFS staging dir missing: ${LITTLEFS_STAGING_DIR}\n"
        "Run 'python3 <repo_root>/scripts/esp32_stage_assets.py' first (writes "
        "here by default), then re-run idf.py build.")
endif()
littlefs_create_partition_image(storage ${LITTLEFS_STAGING_DIR} FLASH_IN_PROJECT)

# net_hil.cpp (Task 10, gated behind CONFIG_HELIX_NET_HIL) calls into
# EspMoonrakerClient's IMoonrakerClient contract directly, which needs the same
# repo-header + shim include paths helixnet's own sources use — those are
# PRIVATE to the helixnet target and don't propagate to a sibling component.
# Mirrors components/helixnet/CMakeLists.txt; keep the two in lockstep.
set(REPO_ROOT ${CMAKE_CURRENT_LIST_DIR}/../../..)
cmake_path(NORMAL_PATH REPO_ROOT)
set(HELIXNET_SHIM ${CMAKE_CURRENT_LIST_DIR}/../components/helixnet/shim)

target_include_directories(${COMPONENT_LIB} PRIVATE
    ${REPO_ROOT}/include        # i_moonraker_client.h + moonraker_*/printer_* headers
    ${HELIXNET_SHIM}/include    # <spdlog/spdlog.h>, <hv/json.hpp> aliases
    ${HELIXNET_SHIM})           # spdlog_shim.h, hv_json_shim.h, platform_stubs.h
target_include_directories(${COMPONENT_LIB} SYSTEM PRIVATE
    ${REPO_ROOT}/lib)           # libhv/include/hv/json.hpp + spdlog bundled fmt

target_compile_options(${COMPONENT_LIB} PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-std=gnu++17>
    $<$<COMPILE_LANGUAGE:CXX>:-Wno-format>)
