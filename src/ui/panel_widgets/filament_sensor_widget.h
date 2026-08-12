// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui_observer_guard.h"
#include "ui_runout_guidance_modal.h"

#include "async_lifetime_guard.h"
#include "filament_widget_tap_policy.h"
#include "panel_widget.h"

#include "hv/json.hpp"

namespace helix {

/**
 * @brief Home tile showing filament sensor state, tappable for load/unload.
 *
 * The tile was XML-only and inert until this class existed. Tap routing lives
 * in filament_widget_tap_policy.h; this class only executes the decision.
 *
 * The XML binds one subject (filament_tile_state) which this class mirrors from
 * whichever role subject the user picked. That is a runtime source mux, not a
 * compound condition, so <subject_expr> cannot express it.
 */
class FilamentSensorWidget : public PanelWidget {
  public:
    FilamentSensorWidget();
    ~FilamentSensorWidget() override;

    void attach(lv_obj_t* widget_obj, lv_obj_t* parent_screen) override;
    void detach() override;
    const char* id() const override {
        return "filament";
    }

    void set_config(const nlohmann::json& config) override;

    bool has_edit_configure() const override {
        return true;
    }
    bool on_edit_configure() override;

    static void clicked_cb(lv_event_t* e);

  private:
    void handle_click();
    /// `status_only` selects the modal copy: mid-print the manual Load/Unload/Purge
    /// row is hidden by XML, so "Load or unload filament." would be nonsense there.
    /// Task 4 gives this same flag a second job (whether to wire the action callbacks).
    void show_tap_modal(bool status_only);
    void open_sensor_settings();
    void rebind_source();

    /// Load/unload/purge dispatch, reached from tap_modal_'s callbacks (wired
    /// only when show_tap_modal(status_only=false) is used). Thin calls into
    /// helix::ui::execute_filament_{load,unload,purge}() — see
    /// filament_op_execute.h. Each one resolving its own backend/slot mirrors
    /// PrintStatusWidget::dispatch_load(): this tile has no slot picker either.
    void dispatch_load();
    void dispatch_unload();
    void dispatch_purge();

    lv_obj_t* widget_obj_ = nullptr;
    lv_obj_t* parent_screen_ = nullptr;

    ui::FilamentTileSource source_ = ui::FilamentTileSource::Auto;
    nlohmann::json config_;

    RunoutGuidanceModal tap_modal_;
    ObserverGuard source_observer_;
    /// Death signal for the role subject rebind_source() observes. No
    /// FilamentSensorManager accessor hands back an owner-backed lifetime today
    /// (unlike TemperatureSensorManager::get_temp_subject), so this stays an
    /// empty shared_ptr - observe_int_sync's `if (lifetime)` gate treats that as
    /// "no token" and skips set_alive_token(), same as omitting the parameter.
    /// Declared so the call site upgrades trivially if that accessor is added.
    SubjectLifetime source_lifetime_;

    // MUST stay the LAST non-static member (the statics below are never torn
    // down with the instance, so they do not count): reverse-declaration
    // destruction makes this the
    // first member torn down, invalidating every captured token before
    // source_observer_ destructs. Without this, a queued observer callback sees
    // token.expired() == false after the observer is already gone and
    // dereferences a half-destroyed widget. Same hazard documented in
    // thermistor_widget.h and temp_stack_widget.h (commit 45abc8c2a, bundle AX3CKAKB).
    helix::AsyncLifetimeGuard lifetime_;

    /// Mirror of the selected role subject; the XML's single binding target.
    static inline lv_subject_t tile_state_subject_{};
    /// 1 = opened by a deliberate tap (neutral icon), 0 = a real runout.
    static inline lv_subject_t advisory_subject_{};
    static inline bool subjects_initialized_ = false;

    static void init_static_subjects();

    friend void register_filament_sensor_widget();
};

void register_filament_sensor_widget();

} // namespace helix
