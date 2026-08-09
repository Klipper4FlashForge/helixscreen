// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_context_menu.h"

#include "ui_utils.h"

#include <spdlog/spdlog.h>

namespace helix::ui {

// ============================================================================
// Construction / Destruction
// ============================================================================

ContextMenu::ContextMenu() {
    spdlog::trace("[ContextMenu] Constructed");
}

ContextMenu::~ContextMenu() {
    hide();
    spdlog::trace("[ContextMenu] Destroyed");
}

ContextMenu::ContextMenu(ContextMenu&& other) noexcept
    : menu_(other.menu_), parent_(other.parent_), item_index_(other.item_index_),
      click_point_(other.click_point_), action_callback_(std::move(other.action_callback_)) {
    other.menu_ = nullptr;
    other.parent_ = nullptr;
    other.item_index_ = -1;
}

ContextMenu& ContextMenu::operator=(ContextMenu&& other) noexcept {
    if (this != &other) {
        hide();
        menu_ = other.menu_;
        parent_ = other.parent_;
        item_index_ = other.item_index_;
        click_point_ = other.click_point_;
        action_callback_ = std::move(other.action_callback_);
        other.menu_ = nullptr;
        other.parent_ = nullptr;
        other.item_index_ = -1;
    }
    return *this;
}

// ============================================================================
// Public API
// ============================================================================

void ContextMenu::set_action_callback(ActionCallback callback) {
    action_callback_ = std::move(callback);
}

bool ContextMenu::show_near_widget(lv_obj_t* parent, int item_index, lv_obj_t* near_widget) {
    // Hide any existing menu first
    hide();

    if (!parent || !near_widget) {
        spdlog::warn("[ContextMenu] Cannot show - missing parent or widget");
        return false;
    }

    // Store state
    parent_ = parent;
    item_index_ = item_index;

    // Create context menu from XML
    menu_ = static_cast<lv_obj_t*>(lv_xml_create(parent, xml_component_name(), nullptr));
    if (!menu_) {
        spdlog::error("[ContextMenu] Failed to create menu from XML: {}", xml_component_name());
        return false;
    }

    // Let subclass configure the menu
    on_created(menu_);

    // Widen the action rows to the card, then position it near the target widget
    lv_obj_t* menu_card = lv_obj_find_by_name(menu_, menu_card_name());
    if (menu_card) {
        stretch_rows_to_card(menu_card);
        position_near_widget(menu_card, near_widget);
    }

    spdlog::debug("[ContextMenu] Shown '{}' for item {}", xml_component_name(), item_index);
    return true;
}

void ContextMenu::hide() {
    if (!menu_)
        return;

    // Use deferred delete since we may be called during event processing
    helix::ui::safe_delete_deferred(menu_);
    item_index_ = -1;
    spdlog::debug("[ContextMenu] hide()");
}

// ============================================================================
// Protected Helpers
// ============================================================================

void ContextMenu::on_backdrop_clicked() {
    dispatch_action(-1); // -1 = cancelled
}

void ContextMenu::dispatch_action(int action) {
    int item = item_index_;
    ActionCallback callback_copy = action_callback_;
    spdlog::debug("[ContextMenu] Dispatch action {} for item {}", action, item);

    hide();

    if (callback_copy) {
        callback_copy(action, item);
    }
}

// ============================================================================
// Row sizing
// ============================================================================

// DECLARATIVE_OK: measured layout. A menu card sized `width="content"` cannot stretch
// its rows declaratively — LVGL flex has no cross-axis stretch, and giving a row
// `width="100%"` drops it out of the parent's content-width calculation entirely
// (`w_ignore_size`, lv_obj_pos.c), collapsing the card to its widest non-percentage
// child. So measure the card once, then widen every action row to that measurement:
// the whole row becomes the hit target instead of just the text inside it.
void ContextMenu::stretch_rows_to_card(lv_obj_t* menu_card) {
    lv_obj_update_layout(menu_card);

    int32_t content_w = lv_obj_get_content_width(menu_card);
    if (content_w <= 0)
        return;

    uint32_t child_cnt = lv_obj_get_child_count(menu_card);
    for (uint32_t i = 0; i < child_cnt; i++) {
        lv_obj_t* child = lv_obj_get_child(menu_card, i);
        if (!child)
            continue;
        // Hidden rows do not contribute to content_w, so forcing a width on one could
        // clip it if it is revealed later. Leave them at their natural size.
        if (lv_obj_has_flag(child, LV_OBJ_FLAG_HIDDEN))
            continue;
        // Only rows that can actually be tapped — labels, hints and separators keep
        // their declared sizing.
        if (!lv_obj_has_flag(child, LV_OBJ_FLAG_CLICKABLE))
            continue;
        // Percentage rows already track the card width.
        if (LV_COORD_IS_PCT(lv_obj_get_style_width(child, LV_PART_MAIN)))
            continue;

        lv_obj_set_width(child, content_w);
    }

    spdlog::trace("[ContextMenu] Stretched rows to card content width {}", content_w);
}

// ============================================================================
// Positioning
// ============================================================================

void ContextMenu::position_near_widget(lv_obj_t* menu_card, lv_obj_t* /*near_widget*/) {
    // Update layout to get accurate dimensions
    lv_obj_update_layout(menu_card);

    int32_t menu_width = lv_obj_get_width(menu_card);
    int32_t menu_height = lv_obj_get_height(menu_card);

    // Use click point captured from the triggering event
    lv_point_t click_pt = click_point_;

    // Convert display coordinates to backdrop-local coordinates
    // menu_card's parent is the backdrop (menu_), which is a child of parent_
    lv_obj_t* backdrop = lv_obj_get_parent(menu_card);
    lv_area_t backdrop_area;
    lv_obj_get_coords(backdrop, &backdrop_area);
    int32_t local_x = click_pt.x - backdrop_area.x1;
    int32_t local_y = click_pt.y - backdrop_area.y1;

    int32_t backdrop_w = lv_obj_get_width(backdrop);
    int32_t backdrop_h = lv_obj_get_height(backdrop);

    // Position menu near the click point
    int32_t menu_x = local_x - 10;
    int32_t menu_y = local_y - 10;

    // If menu would go off right edge, flip to left of click
    if (menu_x + menu_width > backdrop_w - 10) {
        menu_x = local_x - menu_width + 10;
    }

    // Clamp to backdrop bounds
    if (menu_x < 10) {
        menu_x = 10;
    }
    if (menu_y < 10) {
        menu_y = 10;
    }
    if (menu_y + menu_height > backdrop_h - 10) {
        menu_y = backdrop_h - menu_height - 10;
    }

    lv_obj_set_pos(menu_card, menu_x, menu_y);

    spdlog::debug("[ContextMenu] Click({},{}) -> local({},{}) -> menu({},{})", click_pt.x,
                  click_pt.y, local_x, local_y, menu_x, menu_y);
}

} // namespace helix::ui
