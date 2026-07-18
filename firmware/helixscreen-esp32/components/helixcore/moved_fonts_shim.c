// SPDX-License-Identifier: GPL-3.0-or-later
//
// Shim definitions for the 5 "cold" font faces moved out of the compiled app
// image into runtime .bin files in the frogfs storage partition (Stage B
// fonts->frogfs enabler). Each symbol is a zero-init, writable lv_font_t that
// main/font_registration.c populates at boot via lv_binfont_create + struct
// copy (fallback: a copy of noto_sans_18). Every existing &symbol reference
// (ui_icon.cpp, theme_manager.cpp, AssetManager::register_fonts, etc.) and
// token registration keeps working against these populated shims — so no call
// site changes. These MUST be writable (.bss/.data, never .rodata): the
// runtime struct-copy writes into them. mdi_icons_48/64 were de-const'd in
// ui_fonts.h + lv_conf.h for exactly this reason.
#include "lvgl.h"

lv_font_t mdi_icons_48;
lv_font_t mdi_icons_64;
lv_font_t noto_sans_bold_28;
lv_font_t noto_sans_light_16;
lv_font_t noto_sans_light_12;
