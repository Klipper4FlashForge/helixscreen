# SPDX-License-Identifier: GPL-3.0-or-later
idf_component_register(SRCS "app_main.c" "board_display.c" "lvgl_glue.c" "touch_input.c" "ota_health.c"
                             "storage_mount.c" "net_hil.cpp"
                       PRIV_REQUIRES esp_timer app_update esp_lcd driver helixcore helixnet pthread
                                     esp_wifi esp_netif esp_event nvs_flash joltwallet__littlefs
                                     jkent__frogfs
                       INCLUDE_DIRS "." "../boards")

# `storage` partition image: a packed frogfs (jkent/frogfs) compressed
# container, built from the staging tree scripts/esp32_stage_assets.py
# assembles (minified ui_xml in every shipped language, config JSON, printer
# image renditions) by scripts/esp32_pack_assets.py. Both are explicit,
# inspectable pre-steps — like the staging script itself, packing is
# deliberately NOT something CMake shells out to, so configure fails loudly
# with next-step instructions instead of silently flashing a stale or
# zero-length image. `cfg` (settings) is unaffected — this partition swap
# only touches `storage`.
set(STORAGE_IMAGE ${CMAKE_BINARY_DIR}/storage_frogfs.bin)
if(NOT EXISTS ${STORAGE_IMAGE})
    message(FATAL_ERROR
        "Packed asset container missing: ${STORAGE_IMAGE}\n"
        "Run 'python3 <repo_root>/scripts/esp32_stage_assets.py' then "
        "'python3 <repo_root>/scripts/esp32_pack_assets.py' first (writes here by "
        "default), then re-run idf.py build.")
endif()
partition_table_get_partition_info(storage_offset "--partition-name storage" "offset")
if(NOT storage_offset)
    message(FATAL_ERROR "Could not find offset of 'storage' partition in partitions.csv")
endif()
idf_component_get_property(main_args esptool_py FLASH_ARGS)
idf_component_get_property(sub_args esptool_py FLASH_SUB_ARGS)
esptool_py_flash_target(storage-flash "${main_args}" "${sub_args}")
esptool_py_flash_target_image(storage-flash "storage" "${storage_offset}" "${STORAGE_IMAGE}")
esptool_py_flash_target_image(flash "storage" "${storage_offset}" "${STORAGE_IMAGE}")

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
