// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_context_menu.h"

#include "ui_utils.h"

#include "theme_manager.h"

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

    // Fit the card to the screen, widen its action rows, then position it near the
    // target widget. Order matters: the column layout decides the row widths, and
    // both decide the height the positioner has to place.
    lv_obj_t* menu_card = lv_obj_find_by_name(menu_, menu_card_name());
    if (menu_card) {
        fit_card_to_screen(menu_card);
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
// child. So measure the container once, then widen every action row to that
// measurement: the whole row becomes the hit target instead of just the text inside it.
void ContextMenu::stretch_rows_in(lv_obj_t* container) {
    int32_t content_w = lv_obj_get_content_width(container);
    if (content_w <= 0)
        return;

    uint32_t child_cnt = lv_obj_get_child_count(container);
    for (uint32_t i = 0; i < child_cnt; i++) {
        lv_obj_t* child = lv_obj_get_child(container, i);
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
        // Percentage rows already track the container width.
        if (LV_COORD_IS_PCT(lv_obj_get_style_width(child, LV_PART_MAIN)))
            continue;

        lv_obj_set_width(child, content_w);
    }
}

void ContextMenu::stretch_rows_to_card(lv_obj_t* menu_card) {
    lv_obj_update_layout(menu_card);

    // Rows inside a column group must span their own column, not the whole card —
    // stretching them to the card width would make each one as wide as both
    // columns together and blow the row flow apart.
    lv_obj_t* columns = lv_obj_find_by_name(menu_card, kColumnsName);
    if (columns) {
        uint32_t col_cnt = lv_obj_get_child_count(columns);
        for (uint32_t i = 0; i < col_cnt; i++) {
            lv_obj_t* col = lv_obj_get_child(columns, i);
            if (col && !lv_obj_has_flag(col, LV_OBJ_FLAG_HIDDEN))
                stretch_rows_in(col);
        }
    }

    stretch_rows_in(menu_card);
}

// ============================================================================
// Fitting the card to the screen
// ============================================================================

// DECLARATIVE_OK: measured layout. Whether the stacked card overflows depends on how
// many rows the backend left visible, which is only known after on_created() has run
// and the layout has been measured — there is no subject to bind a structural
// conditional to at build time. The widget tree is identical in both layouts, so this
// only flips one container's flex flow; nothing is created or destroyed.
void ContextMenu::fit_card_to_screen(lv_obj_t* menu_card) {
    lv_obj_t* backdrop = lv_obj_get_parent(menu_card);
    if (!backdrop)
        return;

    lv_obj_update_layout(menu_card);

    // Leave a margin top and bottom so the card reads as a menu floating over the
    // screen rather than a panel that happens to fill it.
    const int32_t margin = theme_manager_get_spacing("space_md");
    const int32_t available_h = lv_obj_get_height(backdrop) - (margin * 2);
    if (available_h <= 0)
        return;

    // A column group whose actions are all hidden would otherwise render as a
    // heading and a rule with nothing under them (external-spool mode hides every
    // lane action, for one).
    lv_obj_t* columns = lv_obj_find_by_name(menu_card, kColumnsName);
    if (columns) {
        tidy_column_groups(columns);
    }

    lv_obj_update_layout(menu_card);
    if (lv_obj_get_height(menu_card) <= available_h) {
        return; // Stacked layout fits; leave it as one list.
    }

    if (columns) {
        lv_obj_set_flex_flow(columns, LV_FLEX_FLOW_ROW);
        lv_obj_update_layout(menu_card);
        spdlog::debug("[ContextMenu] Card exceeded {}px — switched to side-by-side columns ({}px)",
                      available_h, lv_obj_get_height(menu_card));
    }

    // Even side by side the card can outgrow a very short screen, so cap it and let
    // the remainder scroll rather than fall off the edge.
    if (lv_obj_get_height(menu_card) > available_h) {
        lv_obj_set_style_max_height(menu_card, available_h, LV_PART_MAIN);
        lv_obj_add_flag(menu_card, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scroll_dir(menu_card, LV_DIR_VER);
        lv_obj_update_layout(menu_card);
        spdlog::debug("[ContextMenu] Card still over budget — capped at {}px, scrollable",
                      available_h);
    }
}

void ContextMenu::tidy_column_groups(lv_obj_t* columns) {
    lv_obj_t* last_visible = nullptr;
    uint32_t visible_cnt = 0;

    uint32_t col_cnt = lv_obj_get_child_count(columns);
    for (uint32_t i = 0; i < col_cnt; i++) {
        lv_obj_t* col = lv_obj_get_child(columns, i);
        if (!col)
            continue;

        // A column earns its heading only if something tappable survived on_created().
        // Height matters as well as the flag: a bare lv_obj is CLICKABLE by default in
        // LVGL, so the 1px rule under the heading otherwise counts as an action and
        // keeps an empty column alive. The rules are also marked clickable="false" —
        // this is the belt to that pair of braces, so an unmarked divider added later
        // cannot quietly resurrect the bug.
        bool has_action = false;
        uint32_t row_cnt = lv_obj_get_child_count(col);
        for (uint32_t r = 0; r < row_cnt && !has_action; r++) {
            lv_obj_t* row = lv_obj_get_child(col, r);
            if (row && !lv_obj_has_flag(row, LV_OBJ_FLAG_HIDDEN) &&
                lv_obj_has_flag(row, LV_OBJ_FLAG_CLICKABLE) &&
                lv_obj_get_height(row) >= kMinTappableH) {
                has_action = true;
            }
        }

        if (has_action) {
            lv_obj_remove_flag(col, LV_OBJ_FLAG_HIDDEN);
            last_visible = col;
            visible_cnt++;
        } else {
            lv_obj_add_flag(col, LV_OBJ_FLAG_HIDDEN);
        }
    }

    // A group heading only earns its space when there is another group to tell it
    // apart from. Alone, it restates the card header one line below it — the
    // external-spool menu read "External Spool" and then "Spool".
    for (uint32_t i = 0; i < col_cnt; i++) {
        lv_obj_t* col = lv_obj_get_child(columns, i);
        if (!col)
            continue;
        // The trigger is how many sibling groups survived on_created(), counted above
        // from live widget state. Binding it would mean every menu publishing a
        // visible-group-count subject purely to feed a generic base-class rule.
        if (lv_obj_t* heading = lv_obj_find_by_name(col, kColumnHeadingName)) {
            if (visible_cnt <= 1 && col == last_visible) {
                // DECLARATIVE_OK: measured layout — visible sibling group count
                lv_obj_add_flag(heading, LV_OBJ_FLAG_HIDDEN);
            } else {
                // DECLARATIVE_OK: measured layout — visible sibling group count
                lv_obj_remove_flag(heading, LV_OBJ_FLAG_HIDDEN);
            }
        }
    }
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

    // Clamp to backdrop bounds. Both axes must clamp the LOW edge last: pushing the
    // card up to fit its bottom on screen can drive y negative when the card is
    // taller than the backdrop, and a negative y clips the header and the first
    // action off the top of the screen (#1212-adjacent; the x path always had this
    // order, the y path did not).
    if (menu_y + menu_height > backdrop_h - 10) {
        menu_y = backdrop_h - menu_height - 10;
    }
    if (menu_x < 10) {
        menu_x = 10;
    }
    if (menu_y < 10) {
        menu_y = 10;
    }

    lv_obj_set_pos(menu_card, menu_x, menu_y);

    spdlog::debug("[ContextMenu] Click({},{}) -> local({},{}) -> menu({},{})", click_pt.x,
                  click_pt.y, local_x, local_y, menu_x, menu_y);
}

} // namespace helix::ui
