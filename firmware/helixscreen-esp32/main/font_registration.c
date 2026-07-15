// SPDX-License-Identifier: GPL-3.0-or-later
#include "font_registration.h"

#include "esp_log.h"
#include "lvgl.h"
#include "src/xml/lv_xml.h"

static const char *TAG = "font_registration";

// Font symbols come from LV_FONT_CUSTOM_DECLARE in lv_conf.h (extern
// lv_font_t declarations), backed by the .c sources compiled into helixcore
// (see components/helixcore/CMakeLists.txt HELIX_FONT_SRCS). Referencing
// every symbol here keeps a live caller of this function from dropping any
// individual face, but Task 6 (theme init) is what supplies that caller —
// until then nothing in the image calls helix_fonts_register() itself, so
// --gc-sections would discard the whole function (and with it every font
// symbol it's the only reference to). __attribute__((used)) marks it as a
// GC root regardless of callers, so the fonts stay linked in now and Task 6
// only has to add the call site.
__attribute__((used)) void helix_fonts_register(void) {
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
