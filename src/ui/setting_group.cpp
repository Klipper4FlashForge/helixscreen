// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#include "setting_group.h"

#include "helix-xml/src/xml/lv_xml_parser.h"
#include "helix-xml/src/xml/lv_xml_widget.h"
#include "helix-xml/src/xml/parsers/lv_xml_obj_parser.h"
#include "lvgl/lvgl.h"
#include "theme_manager.h"

#include <spdlog/spdlog.h>

// Divider logic + draw callback are added in Task 2.
uint32_t setting_group_divider_count(lv_obj_t* group) {
    LV_UNUSED(group);
    return 0;
}

static void* setting_group_xml_create(lv_xml_parser_state_t* state, const char** attrs) {
    LV_UNUSED(attrs);

    void* parent = lv_xml_state_get_parent(state);
    lv_obj_t* obj = lv_obj_create((lv_obj_t*)parent);
    if (!obj) {
        spdlog::error("[SettingGroup] Failed to create lv_obj");
        return nullptr;
    }

    // Card shell: card_bg fill + theme border + theme border_radius (reactive).
    // Strip the LVGL theme's LV_PART_MAIN styles first so the shared style wins.
    lv_obj_remove_style(obj, nullptr, LV_PART_MAIN);
    lv_obj_add_style(obj, ThemeManager::instance().get_style(StyleRole::Card), LV_PART_MAIN);

    // Clip children to the rounded rect: first/last row corners round automatically,
    // honoring the theme radius fully (no clamp) including Pill/Full.
    lv_obj_set_style_clip_corner(obj, true, LV_PART_MAIN);

    // Full width; rows own their own internal padding, so the card has none.
    lv_obj_set_width(obj, LV_PCT(100));
    lv_obj_set_height(obj, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_row(obj, 0, LV_PART_MAIN); // L055: flex gaps are separate from pad_all
    lv_obj_set_style_pad_column(obj, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(obj, LV_FLEX_FLOW_COLUMN);

    // Responsive inset: side margins float the card; bottom margin separates groups.
    // theme_manager_get_spacing() returns the value for the current breakpoint.
    int32_t side = theme_manager_get_spacing("space_sm");
    int32_t gap = theme_manager_get_spacing("space_md");
    lv_obj_set_style_margin_left(obj, side, LV_PART_MAIN);
    lv_obj_set_style_margin_right(obj, side, LV_PART_MAIN);
    lv_obj_set_style_margin_bottom(obj, gap, LV_PART_MAIN);

    // Fixed container, not a scroll area (the overlay content scrolls).
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);

    spdlog::trace("[SettingGroup] Created <setting_group> with card shell");
    return (void*)obj;
}

void setting_group_register(void) {
    lv_xml_register_widget("setting_group", setting_group_xml_create, lv_xml_obj_apply);
    spdlog::trace("[SettingGroup] Registered <setting_group> widget");
}
