// SPDX-License-Identifier: GPL-3.0-or-later
#include "font_registration.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "lvgl.h"
#include "src/xml/lv_xml.h"

static const char *TAG = "font_registration";

// Font symbols come from LV_FONT_CUSTOM_DECLARE in lv_conf.h (extern
// lv_font_t declarations), backed by the .c sources compiled into helixcore
// (see components/helixcore/CMakeLists.txt HELIX_FONT_SRCS). Referencing
// every symbol here keeps any individual face from being dropped — but only
// while this function itself survives the link. With no caller in the image,
// --gc-sections discards it (this Xtensa toolchain does NOT honor
// __attribute__((used)) as a GC root — verified via link map, symbols at
// 0x0). What actually anchors it is `-Wl,--undefined=helix_fonts_register`
// in main/CMakeLists.txt: do not remove that flag unless a real call site
// exists AND the font symbols are re-verified present post-link (nm/readelf).
void helix_fonts_register(void) {
    // These 5 faces' glyph data lives in frogfs .bin files (moved out of the
    // compiled app image; see components/helixcore/moved_fonts_shim.c for their
    // zero-init writable symbols). Populate the shim structs HERE — before the
    // lv_xml_register_font() token registrations below AND before the
    // AssetManager::register_all() by-symbol registrations (app_boot.cpp:266) —
    // so both point at valid, fully-populated shims. On load failure we fall
    // back to a copy of noto_sans_18 (compiled in) so text still renders.
    {
        int64_t t0 = esp_timer_get_time();
        lv_font_t *loaded = lv_binfont_create("A:/assets/assets/fonts/noto_sans_bold_28.bin");
        int64_t dt_ms = (esp_timer_get_time() - t0) / 1000;
        if (loaded) {
            noto_sans_bold_28 = *loaded;
            ESP_LOGI(TAG, "font %s loaded from .bin in %lld ms", "noto_sans_bold_28", dt_ms);
        } else {
            noto_sans_bold_28 = noto_sans_18;
            ESP_LOGW(TAG, "font %s .bin load FAILED (%lld ms) — fell back to noto_sans_18", "noto_sans_bold_28", dt_ms);
        }
    }
    {
        int64_t t0 = esp_timer_get_time();
        lv_font_t *loaded = lv_binfont_create("A:/assets/assets/fonts/noto_sans_light_16.bin");
        int64_t dt_ms = (esp_timer_get_time() - t0) / 1000;
        if (loaded) {
            noto_sans_light_16 = *loaded;
            ESP_LOGI(TAG, "font %s loaded from .bin in %lld ms", "noto_sans_light_16", dt_ms);
        } else {
            noto_sans_light_16 = noto_sans_18;
            ESP_LOGW(TAG, "font %s .bin load FAILED (%lld ms) — fell back to noto_sans_18", "noto_sans_light_16", dt_ms);
        }
    }
    {
        int64_t t0 = esp_timer_get_time();
        lv_font_t *loaded = lv_binfont_create("A:/assets/assets/fonts/noto_sans_light_12.bin");
        int64_t dt_ms = (esp_timer_get_time() - t0) / 1000;
        if (loaded) {
            noto_sans_light_12 = *loaded;
            ESP_LOGI(TAG, "font %s loaded from .bin in %lld ms", "noto_sans_light_12", dt_ms);
        } else {
            noto_sans_light_12 = noto_sans_18;
            ESP_LOGW(TAG, "font %s .bin load FAILED (%lld ms) — fell back to noto_sans_18", "noto_sans_light_12", dt_ms);
        }
    }
    {
        int64_t t0 = esp_timer_get_time();
        lv_font_t *loaded = lv_binfont_create("A:/assets/assets/fonts/mdi_icons_48.bin");
        int64_t dt_ms = (esp_timer_get_time() - t0) / 1000;
        if (loaded) {
            mdi_icons_48 = *loaded;
            ESP_LOGI(TAG, "font %s loaded from .bin in %lld ms", "mdi_icons_48", dt_ms);
        } else {
            mdi_icons_48 = noto_sans_18;
            ESP_LOGW(TAG, "font %s .bin load FAILED (%lld ms) — fell back to noto_sans_18", "mdi_icons_48", dt_ms);
        }
    }
    {
        int64_t t0 = esp_timer_get_time();
        lv_font_t *loaded = lv_binfont_create("A:/assets/assets/fonts/mdi_icons_64.bin");
        int64_t dt_ms = (esp_timer_get_time() - t0) / 1000;
        if (loaded) {
            mdi_icons_64 = *loaded;
            ESP_LOGI(TAG, "font %s loaded from .bin in %lld ms", "mdi_icons_64", dt_ms);
        } else {
            mdi_icons_64 = noto_sans_18;
            ESP_LOGW(TAG, "font %s .bin load FAILED (%lld ms) — fell back to noto_sans_18", "mdi_icons_64", dt_ms);
        }
    }
    // The lv_binfont_create() results are intentionally never destroyed: they
    // live for the process lifetime and their glyph/cmap tables back the shim
    // struct-copies above, so lv_binfont_destroy would free data still in use.

    lv_xml_register_font(NULL, "noto_sans_26", &noto_sans_26);
    lv_xml_register_font(NULL, "noto_sans_bold_28", &noto_sans_bold_28);
    lv_xml_register_font(NULL, "noto_sans_18", &noto_sans_18);
    lv_xml_register_font(NULL, "noto_sans_light_16", &noto_sans_light_16);
    lv_xml_register_font(NULL, "noto_sans_light_12", &noto_sans_light_12);
    lv_xml_register_font(NULL, "source_code_pro_14", &source_code_pro_14);
    lv_xml_register_font(NULL, "mdi_icons_16", &mdi_icons_16);
    lv_xml_register_font(NULL, "mdi_icons_24", &mdi_icons_24);
    lv_xml_register_font(NULL, "mdi_icons_32", &mdi_icons_32);
    lv_xml_register_font(NULL, "mdi_icons_48", &mdi_icons_48);
    lv_xml_register_font(NULL, "mdi_icons_64", &mdi_icons_64);

    ESP_LOGI(TAG, "registered 11 medium-tier fonts (6 compiled + 5 runtime .bin)");
}
