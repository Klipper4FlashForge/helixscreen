// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_tool_action_menu.h"

#include "ui_callback_helpers.h"

namespace helix::ui {

bool ToolActionMenu::callbacks_registered_ = false;

ToolActionMenu::ToolActionMenu() {
    register_callbacks();
}

bool ToolActionMenu::show_for_tool(lv_obj_t* parent, int tool_index, lv_obj_t* anchor) {
    return show_below_widget(parent, tool_index, anchor, AnchorAlign::Center);
}

void ToolActionMenu::register_callbacks() {
    if (callbacks_registered_) {
        return;
    }
    register_xml_callbacks({
        {"tool_action_set_active_cb", on_set_active},
        {"tool_action_unload_cb", on_unload},
        {"tool_action_preheat_cb", on_preheat},
    });
    callbacks_registered_ = true;
}

void ToolActionMenu::on_set_active(lv_event_t*) {
    if (auto* menu = ContextMenu::active_as<ToolActionMenu>()) {
        menu->dispatch_action(Pick);
    }
}

void ToolActionMenu::on_unload(lv_event_t*) {
    if (auto* menu = ContextMenu::active_as<ToolActionMenu>()) {
        menu->dispatch_action(Dock);
    }
}

void ToolActionMenu::on_preheat(lv_event_t*) {
    if (auto* menu = ContextMenu::active_as<ToolActionMenu>()) {
        menu->dispatch_action(Preheat);
    }
}

} // namespace helix::ui
