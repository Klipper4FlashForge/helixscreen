// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_materials_overlay.h"

#include "ui_ams_detail.h"
#include "ui_ams_edit_overlay.h"
#include "ui_callback_helpers.h"
#include "ui_error_reporting.h"
#include "ui_event_safety.h"
#include "ui_panel_ams_overview.h"
#include "ui_panel_spoolman.h"
#include "ui_spool_canvas.h"

#include "ams_backend.h"
#include "ams_state.h"
#include "app_globals.h"
#include "observer_factory.h"
#include "static_panel_registry.h"
#include "ui/ui_lazy_panel_helper.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

namespace helix::ui {

namespace {
std::unique_ptr<MaterialsOverlay> g_materials_overlay;

std::string identity_for(const SlotInfo& slot) {
    if (!slot.product_name.empty()) {
        return slot.product_name;
    }
    if (!slot.spool_name.empty()) {
        return slot.spool_name;
    }
    if (!slot.material.empty()) {
        return slot.material;
    }
    return slot.status == SlotStatus::EMPTY ? lv_tr("No filament") : lv_tr("Material not assigned");
}

std::string detail_for(const SlotInfo& slot) {
    std::string detail;
    if (!slot.brand.empty()) {
        detail = slot.brand;
    }
    if (!slot.material.empty() && slot.material != slot.product_name &&
        slot.material != slot.spool_name) {
        if (!detail.empty()) {
            detail += " · ";
        }
        detail += slot.material;
    }
    if (detail.empty()) {
        detail = slot.is_present() ? lv_tr("Filament detected") : lv_tr("Tap Set to assign");
    }
    return detail;
}

std::string remaining_for(const SlotInfo& slot) {
    if (slot.total_weight_g > 0) {
        char buf[24];
        if (slot.remaining_weight_g >= 0) {
            std::snprintf(buf, sizeof(buf), "%.0f / %.0f g", slot.remaining_weight_g,
                          slot.total_weight_g);
        } else {
            std::snprintf(buf, sizeof(buf), "— / %.0f g", slot.total_weight_g);
        }
        return buf;
    }
    if (slot.remaining_weight_g >= 0) {
        char buf[24];
        std::snprintf(buf, sizeof(buf), "%.0f g", slot.remaining_weight_g);
        return buf;
    }
    if (slot.remaining_length_m > 0) {
        char buf[24];
        std::snprintf(buf, sizeof(buf), "%.0f m", slot.remaining_length_m);
        return buf;
    }
    return "";
}

int slot_index_from_target(lv_event_t* event) {
    const lv_obj_t* target = static_cast<lv_obj_t*>(lv_event_get_current_target(event));
    const char* name = target ? lv_obj_get_name(target) : nullptr;
    if (!name) {
        return -1;
    }
    const char* suffix = std::strrchr(name, '_');
    return suffix ? std::atoi(suffix + 1) : -1;
}
} // namespace

MaterialsOverlay& get_materials_overlay() {
    if (!g_materials_overlay) {
        g_materials_overlay = std::make_unique<MaterialsOverlay>();
        StaticPanelRegistry::instance().register_destroy("MaterialsOverlay",
                                                         []() { g_materials_overlay.reset(); });
    }
    return *g_materials_overlay;
}

MaterialsOverlay::~MaterialsOverlay() {
    slots_version_observer_.reset();
    subjects_.deinit_all();
}

void MaterialsOverlay::init_subjects() {
    if (subjects_initialized_) {
        return;
    }

    UI_MANAGED_SUBJECT_INT(row_count_subject_, 0, "materials_row_count", subjects_);
    for (int i = 0; i < MAX_ROWS; ++i) {
        char name[48];

        std::snprintf(name, sizeof(name), "materials_position_%d", i);
        lv_subject_init_string(&position_subjects_[i], position_bufs_[i].data(), nullptr,
                               position_bufs_[i].size(), "");
        lv_xml_register_subject(nullptr, name, &position_subjects_[i]);
        subjects_.register_subject(&position_subjects_[i], name);

        std::snprintf(name, sizeof(name), "materials_identity_%d", i);
        lv_subject_init_string(&identity_subjects_[i], identity_bufs_[i].data(), nullptr,
                               identity_bufs_[i].size(), "");
        lv_xml_register_subject(nullptr, name, &identity_subjects_[i]);
        subjects_.register_subject(&identity_subjects_[i], name);

        std::snprintf(name, sizeof(name), "materials_detail_%d", i);
        lv_subject_init_string(&detail_subjects_[i], detail_bufs_[i].data(), nullptr,
                               detail_bufs_[i].size(), "");
        lv_xml_register_subject(nullptr, name, &detail_subjects_[i]);
        subjects_.register_subject(&detail_subjects_[i], name);

        std::snprintf(name, sizeof(name), "materials_remaining_%d", i);
        lv_subject_init_string(&remaining_subjects_[i], remaining_bufs_[i].data(), nullptr,
                               remaining_bufs_[i].size(), "");
        lv_xml_register_subject(nullptr, name, &remaining_subjects_[i]);
        subjects_.register_subject(&remaining_subjects_[i], name);

        std::snprintf(name, sizeof(name), "materials_action_%d", i);
        lv_subject_init_string(&action_subjects_[i], action_bufs_[i].data(), nullptr,
                               action_bufs_[i].size(), "");
        lv_xml_register_subject(nullptr, name, &action_subjects_[i]);
        subjects_.register_subject(&action_subjects_[i], name);

        std::snprintf(name, sizeof(name), "materials_fill_%d", i);
        lv_subject_init_int(&fill_subjects_[i], 0);
        lv_xml_register_subject(nullptr, name, &fill_subjects_[i]);
        subjects_.register_subject(&fill_subjects_[i], name);

        std::snprintf(name, sizeof(name), "materials_has_capacity_%d", i);
        lv_subject_init_int(&has_capacity_subjects_[i], 0);
        lv_xml_register_subject(nullptr, name, &has_capacity_subjects_[i]);
        subjects_.register_subject(&has_capacity_subjects_[i], name);

        std::snprintf(name, sizeof(name), "materials_assigned_%d", i);
        lv_subject_init_int(&assigned_subjects_[i], 0);
        lv_xml_register_subject(nullptr, name, &assigned_subjects_[i]);
        subjects_.register_subject(&assigned_subjects_[i], name);
    }

    slots_version_observer_ = observe_int_sync<MaterialsOverlay>(
        AmsState::instance().get_slots_version_subject(), this,
        [](MaterialsOverlay* self, int) {
            if (self->refresh_pending_) {
                return;
            }
            self->refresh_pending_ = true;
            self->lifetime_.defer("MaterialsOverlay::refresh", [self]() {
                self->refresh_pending_ = false;
                self->refresh();
            });
        },
        AmsState::instance().get_subjects_lifetime());

    subjects_initialized_ = true;
}

void MaterialsOverlay::register_callbacks() {
    if (callbacks_registered_) {
        return;
    }
    register_xml_callbacks({
        {"materials_change_cb", on_change_clicked},
        {"materials_unassign_cb", on_unassign_clicked},
        {"materials_library_cb", on_library_clicked},
        {"materials_device_cb", on_device_clicked},
    });
    callbacks_registered_ = true;
}

lv_obj_t* MaterialsOverlay::create(lv_obj_t* parent) {
    register_callbacks();
    return create_overlay_from_xml(parent, "materials_overlay");
}

void MaterialsOverlay::show(lv_obj_t* parent_screen) {
    parent_screen_ = parent_screen;
    lazy_create_and_push_overlay<MaterialsOverlay>(
        get_materials_overlay, overlay_root_, parent_screen, get_name(), "FilamentPanel", true);
}

void MaterialsOverlay::on_activate() {
    refresh();
}

void MaterialsOverlay::on_ui_destroyed() {
    spoolman_panel_ = nullptr;
}

void MaterialsOverlay::refresh() {
    AmsBackend* backend = AmsState::instance().get_backend();
    if (!backend || !overlay_root_) {
        return;
    }

    const AmsSystemInfo info = backend->get_system_info();
    const int count = std::clamp(info.total_slots, 0, MAX_ROWS);
    lv_subject_set_int(&row_count_subject_, count);

    for (int i = 0; i < count; ++i) {
        const SlotInfo slot = backend->get_slot_info(i);
        const bool toolchanger = is_tool_changer(info.type);
        const int display_tool = slot.mapped_tool >= 0 ? slot.mapped_tool : i;
        char position[24];
        std::snprintf(position, sizeof(position), toolchanger ? "T%d" : lv_tr("Slot %d"),
                      toolchanger ? display_tool : i + 1);
        const std::string identity = identity_for(slot);
        const std::string detail = detail_for(slot);
        const std::string remaining = remaining_for(slot);

        // Pass separate source storage to LVGL. Mutating a subject's backing
        // buffer before lv_subject_copy_string() makes its equality check see
        // the new value as the old one, so observers never repaint the row.
        lv_subject_copy_string(&position_subjects_[i], position);
        lv_subject_copy_string(&identity_subjects_[i], identity.c_str());
        lv_subject_copy_string(&detail_subjects_[i], detail.c_str());
        lv_subject_copy_string(&remaining_subjects_[i], remaining.c_str());
        lv_subject_copy_string(&action_subjects_[i],
                               slot.has_filament_info() ? lv_tr("Change") : lv_tr("Set"));
        const float fill = slot.get_remaining_percent();
        lv_subject_set_int(&fill_subjects_[i], fill >= 0 ? static_cast<int>(fill) : 0);
        lv_subject_set_int(&has_capacity_subjects_[i], fill >= 0 ? 1 : 0);
        lv_subject_set_int(&assigned_subjects_[i], slot.has_filament_info() ? 1 : 0);

        char spool_name[48];
        std::snprintf(spool_name, sizeof(spool_name), "materials_spool_%d", i);
        if (lv_obj_t* spool = lv_obj_find_by_name(overlay_root_, spool_name)) {
            ui_spool_canvas_set_color(spool, lv_color_hex(slot.color_rgb));
            ui_spool_canvas_set_fill_level(spool, fill >= 0 ? fill / 100.0f : 0.75f);
        }
    }
}

void MaterialsOverlay::unassign_slot(int slot_index) {
    // Reuse the context menu's canonical clear path. It clears identity and
    // weight metadata through commit_slot_edit(), including Spoolman unlink
    // and backend-specific persistence, and never moves physical filament.
    ams_dispatch_backend_action(AmsContextMenu::MenuAction::CLEAR_SPOOL, slot_index, nullptr);
}

void MaterialsOverlay::open_slot(int slot_index, bool open_on_picker) {
    AmsBackend* backend = AmsState::instance().get_backend();
    if (!backend || slot_index < 0 || slot_index >= backend->get_system_info().total_slots) {
        return;
    }

    const SlotInfo initial = backend->get_slot_info(slot_index);
    get_ams_edit_overlay().show_for_slot(
        parent_screen_, slot_index, initial, get_moonraker_api(),
        [](const AmsEditOverlay::EditResult& result) {
            if (!result.saved || result.slot_index < 0) {
                return;
            }
            if (AmsBackend* current = AmsState::instance().get_backend()) {
                const SlotInfo original = current->get_slot_info(result.slot_index);
                const AmsError error = AmsState::instance().commit_slot_edit(
                    result.slot_index, original, result.slot_info);
                if (!error.success()) {
                    notify_ams_error(error);
                }
            }
        },
        open_on_picker);
}

void MaterialsOverlay::open_library() {
    lazy_create_and_push_overlay<SpoolmanPanel>(get_global_spoolman_panel, spoolman_panel_,
                                                parent_screen_, "Spoolman", get_name(), true);
}

void MaterialsOverlay::open_device_controls() {
    navigate_to_ams_panel();
}

void MaterialsOverlay::on_change_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[MaterialsOverlay] on_change_clicked");
    get_materials_overlay().open_slot(slot_index_from_target(e), true);
    LVGL_SAFE_EVENT_CB_END();
}

void MaterialsOverlay::on_unassign_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[MaterialsOverlay] on_unassign_clicked");
    get_materials_overlay().unassign_slot(slot_index_from_target(e));
    LVGL_SAFE_EVENT_CB_END();
}

void MaterialsOverlay::on_library_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[MaterialsOverlay] on_library_clicked");
    LV_UNUSED(e);
    get_materials_overlay().open_library();
    LVGL_SAFE_EVENT_CB_END();
}

void MaterialsOverlay::on_device_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[MaterialsOverlay] on_device_clicked");
    LV_UNUSED(e);
    get_materials_overlay().open_device_controls();
    LVGL_SAFE_EVENT_CB_END();
}

} // namespace helix::ui
