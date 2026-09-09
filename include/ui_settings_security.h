// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file ui_settings_security.h
 * @brief Security Settings overlay — PIN management and auto-lock configuration.
 *
 * Exposes:
 * - Set PIN (when no PIN is configured)
 * - Change PIN (when PIN is configured)
 * - Remove PIN (when PIN is configured)
 * - Auto-lock toggle (when PIN is configured)
 *
 * Visibility of rows is driven by the global `lock_pin_set` subject from
 * LockManager (0 = no PIN, 1 = PIN set).
 *
 * @pattern Overlay (lazy init, StaticPanelRegistry cleanup)
 * @threading Main thread only
 *
 * @see LockManager for PIN storage and hashing
 * @see PinEntryModal for the numeric keypad dialog
 */

#pragma once

#include "lvgl/lvgl.h"
#include "overlay_base.h"

namespace helix::settings {

/**
 * @class SecuritySettingsOverlay
 * @brief Overlay for configuring screen lock PIN and auto-lock behaviour.
 */
class SecuritySettingsOverlay : public OverlayBase {
  public:
    SecuritySettingsOverlay();
    ~SecuritySettingsOverlay() override;

    //
    // === OverlayBase Interface ===
    //

    void init_subjects() override;
    void register_callbacks() override;

    const char* get_name() const override {
        return "Security Settings";
    }

    void on_activate() override;

    //
    // === UI Creation ===
    //

    lv_obj_t* create(lv_obj_t* parent) override;
    void show(lv_obj_t* parent_screen);

    //
    // === Event Handlers (public for static callbacks) ===
    //

    void handle_set_pin_clicked();
    void handle_change_pin_clicked();
    void handle_remove_pin_clicked();
    void handle_auto_lock_changed(bool enabled);
    void run_set_pin_flow();

  private:
    //
    // === Internal Helpers ===
    //

    void init_auto_lock_toggle();

    //
    // === Static Callbacks ===
    //

    static void on_set_pin_clicked(lv_event_t* e);
    static void on_change_pin_clicked(lv_event_t* e);
    static void on_remove_pin_clicked(lv_event_t* e);
    static void on_auto_lock_changed(lv_event_t* e);
};

/**
 * @brief Global instance accessor.
 *
 * Creates the overlay on first access and registers it for cleanup
 * with StaticPanelRegistry.
 */
SecuritySettingsOverlay& get_security_settings_overlay();

} // namespace helix::settings
