// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "asset_manager.h"

#include "data_root_resolver.h"
#include "ui_fonts.h"

#include "theme_manager.h"

#include <spdlog/spdlog.h>

#include <lvgl.h>

// Static member definitions
bool AssetManager::s_fonts_registered = false;
bool AssetManager::s_images_registered = false;

void AssetManager::register_fonts() {
    if (s_fonts_registered) {
        spdlog::debug("[AssetManager] Fonts already registered, skipping");
        return;
    }

    // Determine breakpoint from the current display's vertical resolution.
    // Fonts only used at larger breakpoints are skipped to save memory
    // (~500-800KB of .rodata pages that won't be faulted in).
    int32_t ver_res = 0;
    lv_display_t* disp = lv_display_get_default();
    if (disp) {
        ver_res = lv_display_get_vertical_resolution(disp);
    }
    const bool is_medium_plus = (ver_res > UI_BREAKPOINT_SMALL_MAX);
    const bool is_large_plus = (ver_res > UI_BREAKPOINT_MEDIUM_MAX);
    const bool is_xlarge_plus = (ver_res > UI_BREAKPOINT_LARGE_MAX);
    const bool is_xxlarge_plus = (ver_res > UI_BREAKPOINT_XLARGE_MAX);
    const bool is_micro = (ver_res <= UI_BREAKPOINT_MICRO_MAX);

    int skipped = 0;

    spdlog::trace("[AssetManager] Registering fonts (ver_res={}, micro={}, medium+={}, large+={})",
                  ver_res, is_micro, is_medium_plus, is_large_plus);

    // Material Design Icons (various sizes for different UI elements)
    // Source: https://pictogrammers.com/library/mdi/
    // All icon sizes needed at all breakpoints (used in watchdog, XML, etc.)
    lv_xml_register_font(nullptr, "mdi_icons_64", &mdi_icons_64);
    lv_xml_register_font(nullptr, "mdi_icons_48", &mdi_icons_48);
    lv_xml_register_font(nullptr, "mdi_icons_32", &mdi_icons_32);
    lv_xml_register_font(nullptr, "mdi_icons_24", &mdi_icons_24);
    lv_xml_register_font(nullptr, "mdi_icons_16", &mdi_icons_16);
    lv_xml_register_font(nullptr, "mdi_icons_14", &mdi_icons_14);

    // Micro fonts (480x272 screens only) — skip on larger screens to save memory
    if (is_micro) {
        lv_xml_register_font(nullptr, "noto_sans_8", &noto_sans_8);
        lv_xml_register_font(nullptr, "source_code_pro_8", &source_code_pro_8);
    } else {
        skipped += 2;
    }

    // Montserrat text fonts - used by semantic text components. Sizes per breakpoint
    // (micro/tiny/small/medium/large/xlarge/xxlarge):
    // - text_heading uses font_heading  (14 / 14 / 20 / 26 / 28 / 32 / 40)
    // - text_body    uses font_body     (10 / 11 / 14 / 18 / 20 / 24 / 32)
    // - text_small   uses font_small    (10 / 11 / 12 / 16 / 18 / 20 / 26)
    // NOTE: Registering as "montserrat_*" for XML compatibility but using noto_sans_* fonts
    lv_xml_register_font(nullptr, "montserrat_10", &noto_sans_10);
    lv_xml_register_font(nullptr, "montserrat_12", &noto_sans_12);
    lv_xml_register_font(nullptr, "montserrat_14", &noto_sans_14);
    lv_xml_register_font(nullptr, "montserrat_16", &noto_sans_16);
    lv_xml_register_font(nullptr, "montserrat_18", &noto_sans_18);
    lv_xml_register_font(nullptr, "montserrat_20", &noto_sans_20);
    lv_xml_register_font(nullptr, "montserrat_24", &noto_sans_24);
    // montserrat_26: only font_heading_medium
    if (is_medium_plus) {
        lv_xml_register_font(nullptr, "montserrat_26", &noto_sans_26);
    } else {
        skipped++;
    }
    // montserrat_28: only font_heading_large
    if (is_large_plus) {
        lv_xml_register_font(nullptr, "montserrat_28", &noto_sans_28);
    } else {
        skipped++;
    }

    // Noto Sans fonts - same sizes as Montserrat, with extended Unicode support
    // (includes ©®™€£¥°±•… and other symbols)
    lv_xml_register_font(nullptr, "noto_sans_10", &noto_sans_10);
    lv_xml_register_font(nullptr, "noto_sans_11", &noto_sans_11);
    lv_xml_register_font(nullptr, "noto_sans_12", &noto_sans_12);
    lv_xml_register_font(nullptr, "noto_sans_14", &noto_sans_14);
    lv_xml_register_font(nullptr, "noto_sans_16", &noto_sans_16);
    lv_xml_register_font(nullptr, "noto_sans_18", &noto_sans_18);
    lv_xml_register_font(nullptr, "noto_sans_20", &noto_sans_20);
    lv_xml_register_font(nullptr, "noto_sans_24", &noto_sans_24);
    // noto_sans_26: only font_heading_medium
    if (is_medium_plus) {
        lv_xml_register_font(nullptr, "noto_sans_26", &noto_sans_26);
    } else {
        skipped++;
    }
    // noto_sans_28: only font_heading_large
    if (is_large_plus) {
        lv_xml_register_font(nullptr, "noto_sans_28", &noto_sans_28);
    } else {
        skipped++;
    }

    // Noto Sans Light fonts (for text_small and text_xs)
    lv_xml_register_font(nullptr, "noto_sans_light_10", &noto_sans_light_10);
    lv_xml_register_font(nullptr, "noto_sans_light_11", &noto_sans_light_11);
    lv_xml_register_font(nullptr, "noto_sans_light_12", &noto_sans_light_12);
    // noto_sans_light_14: only font_xs_large
    if (is_large_plus) {
        lv_xml_register_font(nullptr, "noto_sans_light_14", &noto_sans_light_14);
    } else {
        skipped++;
    }
    // noto_sans_light_16: only font_small_medium
    if (is_medium_plus) {
        lv_xml_register_font(nullptr, "noto_sans_light_16", &noto_sans_light_16);
    } else {
        skipped++;
    }
    // noto_sans_light_18: only font_small_large
    if (is_large_plus) {
        lv_xml_register_font(nullptr, "noto_sans_light_18", &noto_sans_light_18);
    } else {
        skipped++;
    }

    // Noto Sans Bold fonts — all registered unconditionally because they're
    // used directly in C++ (watchdog: bold_16/24) and XML (debug modal: bold_28)
    lv_xml_register_font(nullptr, "noto_sans_bold_14", &noto_sans_bold_14);
    lv_xml_register_font(nullptr, "noto_sans_bold_16", &noto_sans_bold_16);
    lv_xml_register_font(nullptr, "noto_sans_bold_18", &noto_sans_bold_18);
    lv_xml_register_font(nullptr, "noto_sans_bold_20", &noto_sans_bold_20);
    lv_xml_register_font(nullptr, "noto_sans_bold_24", &noto_sans_bold_24);
    lv_xml_register_font(nullptr, "noto_sans_bold_28", &noto_sans_bold_28);

    // Source Code Pro - Monospace (for console/terminal displays)
    lv_xml_register_font(nullptr, "source_code_pro_10", &source_code_pro_10);
    lv_xml_register_font(nullptr, "source_code_pro_12", &source_code_pro_12);
    lv_xml_register_font(nullptr, "source_code_pro_14", &source_code_pro_14);
    lv_xml_register_font(nullptr, "source_code_pro_16", &source_code_pro_16);

    // XLarge tier fonts (HiDPI screens > LARGE_MAX height)
#if HELIX_MAX_FONT_TIER >= 5
    if (is_xlarge_plus) {
        lv_xml_register_font(nullptr, "noto_sans_32", &noto_sans_32);
        lv_xml_register_font(nullptr, "noto_sans_bold_32", &noto_sans_bold_32);
        lv_xml_register_font(nullptr, "noto_sans_light_20", &noto_sans_light_20);
        lv_xml_register_font(nullptr, "source_code_pro_18", &source_code_pro_18);
        lv_xml_register_font(nullptr, "mdi_icons_80", &mdi_icons_80);
    } else {
        skipped += 5;
    }
#endif

    // XXLarge tier fonts (HiDPI screens > XLARGE_MAX height, e.g. 2560x1440)
#if HELIX_MAX_FONT_TIER >= 6
    if (is_xxlarge_plus) {
        lv_xml_register_font(nullptr, "noto_sans_40", &noto_sans_40);
        lv_xml_register_font(nullptr, "noto_sans_bold_40", &noto_sans_bold_40);
        lv_xml_register_font(nullptr, "noto_sans_light_26", &noto_sans_light_26);
        lv_xml_register_font(nullptr, "source_code_pro_20", &source_code_pro_20);
        lv_xml_register_font(nullptr, "source_code_pro_24", &source_code_pro_24);
        lv_xml_register_font(nullptr, "mdi_icons_96", &mdi_icons_96);
        lv_xml_register_font(nullptr, "mdi_icons_128", &mdi_icons_128);
    } else {
        skipped += 7;
    }
#endif

    s_fonts_registered = true;
    if (skipped > 0) {
        spdlog::info("[AssetManager] Fonts registered ({} skipped for breakpoint)", skipped);
    } else {
        spdlog::trace("[AssetManager] All fonts registered (large+ breakpoint)");
    }
}

void AssetManager::register_images() {
    if (s_images_registered) {
        spdlog::debug("[AssetManager] Images already registered, skipping");
        return;
    }

    spdlog::trace("[AssetManager] Registering images...");

    // reg_img routes the bundle-relative source through asset_component_uri so
    // the registered src resolves against the mount on firmware
    // (/assets/assets/images/...) and stays byte-identical on desktop
    // (asset_root "."). lv_xml_register_image lv_strdup's the src, so passing a
    // temporary c_str() is safe. The registration NAME is left unchanged — the
    // "path-as-name" entries keep their "A:assets/images/..." literal so existing
    // XML / lv_image_set_src references by that name still resolve.
    auto reg_img = [](const char* name, const char* rel) {
        lv_xml_register_image(nullptr, name, helix::asset_component_uri(rel).c_str());
    };

    // Branding
    reg_img("A:assets/images/helixscreen-logo.png", "assets/images/helixscreen-logo.png");
    reg_img("A:assets/images/about-logo.bin", "assets/images/about-logo.bin");

    // Printer and UI images
    reg_img("A:assets/images/printer_400.png", "assets/images/printer_400.png");
    reg_img("filament_spool", "assets/images/filament_spool.png");
    // Tintable Spoolman mark (AMS editor identity chip)
    reg_img("spoolman_mark", "assets/images/ams/spoolman_24.png");
    reg_img("A:assets/images/placeholder_thumb_centered.png",
            "assets/images/placeholder_thumb_centered.png");
    reg_img("A:assets/images/thumbnail-gradient-bg.png", "assets/images/thumbnail-gradient-bg.png");
    reg_img("A:assets/images/benchy_thumbnail_white.png", "assets/images/benchy_thumbnail_white.png");
    reg_img("A:assets/images/prerendered/benchy_thumbnail_white.bin",
            "assets/images/prerendered/benchy_thumbnail_white.bin");

    // Flag icons (language chooser wizard) - pre-rendered ARGB8888 32x24
    reg_img("flag_en", "assets/images/flags/flag_en.bin");
    reg_img("flag_de", "assets/images/flags/flag_de.bin");
    reg_img("flag_fr", "assets/images/flags/flag_fr.bin");
    reg_img("flag_es", "assets/images/flags/flag_es.bin");
    reg_img("flag_ru", "assets/images/flags/flag_ru.bin");
    reg_img("flag_pt", "assets/images/flags/flag_pt.bin");
    reg_img("flag_it", "assets/images/flags/flag_it.bin");
    reg_img("flag_zh", "assets/images/flags/flag_zh.bin");
    reg_img("flag_ja", "assets/images/flags/flag_ja.bin");

    s_images_registered = true;
    spdlog::trace("[AssetManager] Images registered successfully");
}

void AssetManager::register_all() {
    register_fonts();
    register_images();
}

bool AssetManager::fonts_registered() {
    return s_fonts_registered;
}

bool AssetManager::images_registered() {
    return s_images_registered;
}
