// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ui_context_menu.h"

namespace helix::ui {

class ToolActionMenu : public ContextMenu {
    HELIX_CONTEXT_MENU_KIND(ToolActionMenu)

  public:
    enum Action { Pick = 1, Dock = 2, Preheat = 3 };

    ToolActionMenu();
    bool show_for_tool(lv_obj_t* parent, int tool_index, lv_obj_t* anchor);

  protected:
    const char* xml_component_name() const override {
        return "tool_action_menu";
    }

    CardWidth card_width() const override {
        return {.pct = 28, .min = 180, .max = 240};
    }

  private:
    static void register_callbacks();
    static void on_set_active(lv_event_t* e);
    static void on_unload(lv_event_t* e);
    static void on_preheat(lv_event_t* e);
    static bool callbacks_registered_;
};

} // namespace helix::ui
