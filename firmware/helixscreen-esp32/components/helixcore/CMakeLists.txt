# SPDX-License-Identifier: GPL-3.0-or-later
#
# Compiles the repo's LVGL submodule (lib/lvgl, pinned 9.5.0 + patches) and
# lib/helix-xml UNMODIFIED, via relative paths into the main checkout.
#
# Why not the ESP-IDF managed lvgl/lvgl component: helix-xml includes
# <lvgl_private.h> (private internals) — it must be compiled against the exact
# LVGL tree it was extracted from. Version drift is guaranteed breakage, so the
# audit goes straight to the repo submodule (the plan's documented fallback).
#
# Source list mirrors Makefile:283+300 — LVGL's internal xml/expat are excluded
# because lib/helix-xml replaces them.

set(REPO_ROOT ${CMAKE_CURRENT_LIST_DIR}/../../../..)
cmake_path(NORMAL_PATH REPO_ROOT)

file(GLOB_RECURSE LVGL_SRCS ${REPO_ROOT}/lib/lvgl/src/*.c)
file(GLOB LVGL_INTERNAL_XML
     ${REPO_ROOT}/lib/lvgl/src/xml/*.c
     ${REPO_ROOT}/lib/lvgl/src/xml/parsers/*.c
     ${REPO_ROOT}/lib/lvgl/src/libs/expat/*.c)
if(LVGL_INTERNAL_XML)
    list(REMOVE_ITEM LVGL_SRCS ${LVGL_INTERNAL_XML})
endif()

file(GLOB HELIX_XML_SRCS
     ${REPO_ROOT}/lib/helix-xml/src/xml/*.c
     ${REPO_ROOT}/lib/helix-xml/src/xml/parsers/*.c
     ${REPO_ROOT}/lib/helix-xml/src/libs/expat/*.c)

# Medium-tier (800x480 K-Touch) compiled font faces. Explicit list, not a glob
# over assets/fonts/*.c — that directory holds ~40 faces across every
# breakpoint tier plus CJK; only the faces the _medium tier's globals.xml
# tokens reference ship here (see LV_FONT_CUSTOM_DECLARE in lv_conf.h, which
# must stay in lockstep with this list). CJK companions are Plan 5.
set(HELIX_FONT_SRCS
    ${REPO_ROOT}/assets/fonts/noto_sans_26.c
    ${REPO_ROOT}/assets/fonts/noto_sans_bold_28.c
    ${REPO_ROOT}/assets/fonts/noto_sans_18.c
    ${REPO_ROOT}/assets/fonts/noto_sans_light_16.c
    ${REPO_ROOT}/assets/fonts/noto_sans_light_12.c
    ${REPO_ROOT}/assets/fonts/source_code_pro_14.c
    ${REPO_ROOT}/assets/fonts/mdi_icons_16.c
    ${REPO_ROOT}/assets/fonts/mdi_icons_24.c
    ${REPO_ROOT}/assets/fonts/mdi_icons_32.c
    ${REPO_ROOT}/assets/fonts/mdi_icons_48.c
    ${REPO_ROOT}/assets/fonts/mdi_icons_64.c)

idf_component_register(
    SRCS ${LVGL_SRCS} ${HELIX_XML_SRCS} ${HELIX_FONT_SRCS} anomaly_stub.c
    INCLUDE_DIRS
        ${CMAKE_CURRENT_LIST_DIR}      # adapted lv_conf.h (RGB565)
        ${REPO_ROOT}/lib/lvgl          # lvgl.h, lvgl_private.h
        ${REPO_ROOT}/lib/helix-xml     # helix_xml.h
    PRIV_REQUIRES pthread esp_timer)

# LV_KCONFIG_IGNORE: helix-xml ships lv_conf_internal.h without the sibling
# lv_conf_kconfig.h; we configure via lv_conf.h, not ESP-IDF Kconfig.
# LV_LVGL_H_INCLUDE_SIMPLE: the font .c sources under assets/fonts/ guard
# their include with this macro (desktop resolves the #else branch,
# "lvgl/lvgl.h", via its own -isystem lib path instead) — firmware only
# exposes lib/lvgl as an include dir, so take the #include "lvgl.h" branch.
target_compile_definitions(${COMPONENT_LIB} PUBLIC LV_CONF_INCLUDE_SIMPLE LV_KCONFIG_IGNORE
                                                     LV_LVGL_H_INCLUDE_SIMPLE)
# LVGL 9.5 sources trip -Wformat and old-style warnings on Xtensa GCC; the
# audit measures compile/link viability, not warning hygiene.
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-format -Wno-unused-variable)
