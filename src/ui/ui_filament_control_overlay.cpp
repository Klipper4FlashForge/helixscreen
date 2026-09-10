// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui_filament_control_overlay.h"

#include "ui_nav_manager.h"

#include "ui/ui_lazy_panel_helper.h"

void FilamentControlOverlay::init_subjects() {
    // No subjects owned here: FilamentPanel initializes and tears them down.
    subjects_initialized_ = true;
}

lv_obj_t* FilamentControlOverlay::create(lv_obj_t* parent) {
    const char* attrs[] = {"title", title_.c_str(), nullptr};
    return create_overlay_from_xml(parent, component_, attrs);
}

void FilamentControlOverlay::show(lv_obj_t* parent, const std::string& title) {
    title_ = title;
    helix::ui::lazy_create_and_push_overlay<FilamentControlOverlay>(
        [this]() -> FilamentControlOverlay& { return *this; }, overlay_root_, parent, get_name(),
        "FilamentPanel", true);
}

void FilamentControlOverlay::hide() {
    if (overlay_root_ && !NavigationManager::is_destroyed() &&
        NavigationManager::instance().is_panel_on_top(overlay_root_)) {
        NavigationManager::instance().go_back();
    }
}

void FilamentControlOverlay::destroy() {
    if (overlay_root_ && lv_is_initialized() && !NavigationManager::is_destroyed()) {
        destroy_overlay_ui(overlay_root_);
    }
}

FilamentControlOverlay::operator bool() const {
    return overlay_root_ && !NavigationManager::is_destroyed() &&
           NavigationManager::instance().is_panel_in_stack(overlay_root_);
}
