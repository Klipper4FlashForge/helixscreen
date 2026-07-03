// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_material_picker_menu.h"

#include "ui_callback_helpers.h"
#include "ui_event_safety.h"
#include "ui_update_queue.h"

#include "filament_database.h"
#include "theme_manager.h"
#include "ui_icon_codepoints.h"

#include <spdlog/spdlog.h>

namespace helix::ui {

// Static member initialization
MaterialPickerMenu* MaterialPickerMenu::s_active_instance_ = nullptr;
bool MaterialPickerMenu::s_callbacks_registered_ = false;

// ============================================================================
// Public API
// ============================================================================

void MaterialPickerMenu::register_callbacks() {
    if (s_callbacks_registered_) {
        return;
    }
    register_xml_callbacks({
        {"material_picker_backdrop_cb", on_backdrop_cb},
        {"material_picker_reset_cb", on_reset_cb},
    });
    s_callbacks_registered_ = true;
    spdlog::debug("[MaterialPickerMenu] Callbacks registered");
}

void MaterialPickerMenu::show(lv_obj_t* parent, lv_obj_t* near_widget,
                              const std::string& current_material) {
    register_callbacks();
    current_material_ = current_material;

    // Anchor at the top-center of the button using ABSOLUTE (screen) coords, so
    // nesting depth does not matter (base class converts to backdrop-local).
    lv_area_t area;
    lv_obj_get_coords(near_widget, &area);
    lv_point_t pt;
    pt.x = (area.x1 + area.x2) / 2;
    pt.y = area.y1;
    set_click_point(pt);

    s_active_instance_ = this;
    show_near_widget(parent, 0, near_widget);
    spdlog::debug("[MaterialPickerMenu] Shown (current='{}')", current_material_);
}

// ============================================================================
// ContextMenu overrides
// ============================================================================

void MaterialPickerMenu::on_created(lv_obj_t* /*menu*/) {
    populate_material_list(current_material_);
}

// ============================================================================
// Material list population
// ============================================================================

void MaterialPickerMenu::populate_material_list(const std::string& current_material) {
    lv_obj_t* list = lv_obj_find_by_name(menu(), "material_list");
    if (!list) {
        spdlog::error("[MaterialPickerMenu] material_list container not found");
        return;
    }

    // Cap scroll height to ~2/3 of screen so a long list scrolls (mirrors PrinterSwitchMenu).
    lv_obj_t* screen = lv_obj_get_screen(menu());
    int screen_h = lv_obj_get_height(screen);
    lv_obj_set_style_max_height(list, screen_h * 2 / 3, 0);

    lv_color_t accent = theme_manager_get_color("primary");
    lv_color_t text_color = theme_manager_get_color("text");

    // Resolve fonts via the XML token system (same pattern as PrinterSwitchMenu). The
    // check indicator MUST use the icon font — LV_SYMBOL_OK isn't in the body font and
    // renders as a missing-glyph box.
    const char* body_font_name = lv_xml_get_const(nullptr, "font_body");
    const lv_font_t* body_font =
        body_font_name ? lv_xml_get_font(nullptr, body_font_name) : lv_font_get_default();
    const char* icon_font_name = lv_xml_get_const(nullptr, "icon_font_xs");
    const lv_font_t* icon_font =
        icon_font_name ? lv_xml_get_font(nullptr, icon_font_name) : body_font;
    const char* check_codepoint = ui_icon::lookup_codepoint("check");

    // Iterate the database in its native (logical-group) order — do NOT sort.
    for (const char* name : filament::get_all_material_names()) {
        bool is_current = (current_material == name);

        lv_obj_t* row = lv_obj_create(list);
        lv_obj_remove_style_all(row);
        lv_obj_set_width(row, LV_PCT(100));
        lv_obj_set_height(row, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_hor(row, 10, 0);
        lv_obj_set_style_pad_ver(row, 8, 0);
        lv_obj_set_style_pad_gap(row, 8, 0);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_color(row, accent, LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(row, LV_OPA_30, LV_STATE_PRESSED);
        lv_obj_set_style_radius(row, 4, 0);
        lv_obj_set_name(row, name); // stash the material name for the click handler

        // Check indicator (current material) or blank spacer — fixed width keeps the
        // material names aligned in a column.
        lv_obj_t* indicator = lv_label_create(row);
        lv_obj_set_style_text_font(indicator, icon_font, 0);
        lv_obj_set_style_min_width(indicator, 16, 0);
        if (is_current && check_codepoint) {
            lv_label_set_text(indicator, check_codepoint);
            lv_obj_set_style_text_color(indicator, accent, 0);
        } else {
            lv_label_set_text(indicator, "");
        }
        lv_obj_remove_flag(indicator, LV_OBJ_FLAG_CLICKABLE);

        lv_obj_t* lbl = lv_label_create(row);
        lv_label_set_text(lbl, name); // material names are not translated [L070]
        lv_obj_set_style_text_font(lbl, body_font, 0);
        lv_obj_set_style_text_color(lbl, is_current ? accent : text_color, 0);
        lv_obj_remove_flag(lbl, LV_OBJ_FLAG_CLICKABLE); // click lands on the row

        lv_obj_add_event_cb(row, on_material_row_cb, LV_EVENT_CLICKED, nullptr);
    }
}

// ============================================================================
// Action handlers
// ============================================================================

void MaterialPickerMenu::dispatch_select(const std::string& material) {
    SelectCallback cb = select_cb_; // copy before hide() tears us down
    s_active_instance_ = nullptr;
    hide();
    if (cb) {
        std::string m = material;
        helix::ui::queue_update([cb, m]() { cb(m); });
    }
}

void MaterialPickerMenu::dispatch_reset() {
    ResetCallback cb = reset_cb_;
    s_active_instance_ = nullptr;
    hide();
    if (cb) {
        helix::ui::queue_update([cb]() { cb(); });
    }
}

// ============================================================================
// Static callbacks
// ============================================================================

void MaterialPickerMenu::on_backdrop_cb(lv_event_t* /*e*/) {
    LVGL_SAFE_EVENT_CB_BEGIN("[MaterialPickerMenu] on_backdrop_cb");
    if (s_active_instance_) {
        MaterialPickerMenu* self = s_active_instance_;
        s_active_instance_ = nullptr;
        self->hide();
    }
    LVGL_SAFE_EVENT_CB_END();
}

void MaterialPickerMenu::on_reset_cb(lv_event_t* /*e*/) {
    LVGL_SAFE_EVENT_CB_BEGIN("[MaterialPickerMenu] on_reset_cb");
    if (s_active_instance_) {
        s_active_instance_->dispatch_reset();
    }
    LVGL_SAFE_EVENT_CB_END();
}

void MaterialPickerMenu::on_material_row_cb(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[MaterialPickerMenu] on_material_row_cb");
    lv_obj_t* row = lv_event_get_current_target_obj(e); // cb attached to the row
    if (!row || !s_active_instance_) {
        return; // still inside the try block opened by BEGIN() — no early END() needed
    }
    const char* name = lv_obj_get_name(row);
    if (name) {
        s_active_instance_->dispatch_select(name);
    }
    LVGL_SAFE_EVENT_CB_END();
}

} // namespace helix::ui
