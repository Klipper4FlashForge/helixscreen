// SPDX-License-Identifier: GPL-3.0-or-later
#include "font_registration.h"

#include "esp_log.h"
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

    ESP_LOGI(TAG, "registered 11 medium-tier fonts (5 text + 1 mono + 5 icon)");
}
