// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui_observer_guard.h"

#include "async_lifetime_guard.h"
#include "overlay_base.h"
#include "subject_managed_panel.h"

#include <array>

namespace helix::ui {

/** Material-first overview for every lane/tool exposed by the active backend. */
class MaterialsOverlay : public OverlayBase {
  public:
    static constexpr int MAX_ROWS = 16;

    MaterialsOverlay() = default;
    ~MaterialsOverlay() override;

    void init_subjects() override;
    void register_callbacks() override;
    lv_obj_t* create(lv_obj_t* parent) override;
    const char* get_name() const override {
        return "Materials";
    }
    void on_activate() override;
    void on_ui_destroyed() override;

    void show(lv_obj_t* parent_screen);

  private:
    void refresh();
    void open_slot(int slot_index, bool open_on_picker);
    void unassign_slot(int slot_index);
    void open_library();
    void open_device_controls();

    static void on_change_clicked(lv_event_t* e);
    static void on_unassign_clicked(lv_event_t* e);
    static void on_library_clicked(lv_event_t* e);
    static void on_device_clicked(lv_event_t* e);

    SubjectManager subjects_;
    lv_subject_t row_count_subject_;
    std::array<lv_subject_t, MAX_ROWS> position_subjects_{};
    std::array<lv_subject_t, MAX_ROWS> identity_subjects_{};
    std::array<lv_subject_t, MAX_ROWS> detail_subjects_{};
    std::array<lv_subject_t, MAX_ROWS> remaining_subjects_{};
    std::array<lv_subject_t, MAX_ROWS> action_subjects_{};
    std::array<lv_subject_t, MAX_ROWS> fill_subjects_{};
    std::array<lv_subject_t, MAX_ROWS> has_capacity_subjects_{};
    std::array<lv_subject_t, MAX_ROWS> assigned_subjects_{};
    std::array<std::array<char, 24>, MAX_ROWS> position_bufs_{};
    std::array<std::array<char, 96>, MAX_ROWS> identity_bufs_{};
    std::array<std::array<char, 96>, MAX_ROWS> detail_bufs_{};
    std::array<std::array<char, 32>, MAX_ROWS> remaining_bufs_{};
    std::array<std::array<char, 16>, MAX_ROWS> action_bufs_{};

    ObserverGuard slots_version_observer_;
    AsyncLifetimeGuard lifetime_;
    bool refresh_pending_ = false;
    bool callbacks_registered_ = false;
    lv_obj_t* parent_screen_ = nullptr;
    lv_obj_t* spoolman_panel_ = nullptr;
};

MaterialsOverlay& get_materials_overlay();

} // namespace helix::ui
