// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui_context_menu.h"

#include <functional>
#include <string>

namespace helix::ui {

/**
 * @brief Anchored popup that lists all materials from the filament database
 *
 * Shows a popup near the triggering preset button, listing every material
 * name known to the filament database (in database order — not sorted) with
 * a checkmark on the currently-assigned material, plus a "Reset all to
 * defaults" action. Modeled on PrinterSwitchMenu.
 */
class MaterialPickerMenu : public ContextMenu {
  public:
    using SelectCallback = std::function<void(const std::string& material)>;
    using ResetCallback = std::function<void()>;

    void set_select_callback(SelectCallback cb) {
        select_cb_ = std::move(cb);
    }
    void set_reset_callback(ResetCallback cb) {
        reset_cb_ = std::move(cb);
    }

    // Anchor the popup near `near_widget`; highlight `current_material` in the list.
    void show(lv_obj_t* parent, lv_obj_t* near_widget, const std::string& current_material);

    static void register_callbacks();

  protected:
    const char* xml_component_name() const override {
        return "material_picker_menu";
    }
    void on_created(lv_obj_t* menu) override;

  private:
    friend class MaterialPickerTestAccess;

    void populate_material_list(const std::string& current_material);
    void dispatch_select(const std::string& material);
    void dispatch_reset();

    static void on_backdrop_cb(lv_event_t* e);
    static void on_reset_cb(lv_event_t* e);
    static void on_material_row_cb(lv_event_t* e);

    SelectCallback select_cb_;
    ResetCallback reset_cb_;
    std::string current_material_;

    static MaterialPickerMenu* s_active_instance_;
    static bool s_callbacks_registered_;
};

} // namespace helix::ui
