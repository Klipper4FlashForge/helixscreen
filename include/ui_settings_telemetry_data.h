// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file ui_settings_telemetry_data.h
 * @brief Telemetry Data overlay - shows queued telemetry events for transparency
 *
 * This overlay lets users inspect exactly what anonymous telemetry data
 * would be sent. It displays queued events from TelemetryManager as
 * human-readable cards showing event type, timestamp, and payload.
 *
 * Users can:
 * - See all queued events and their contents
 * - Clear the queue to purge all pending data
 *
 * @pattern Overlay (lazy init, dynamic row creation)
 * @threading Main thread only
 *
 * @see TelemetryManager for event queue management
 * @see SettingsPanel for entry point (on_telemetry_view_data)
 */

#pragma once

#include "lvgl/lvgl.h"
#include "overlay_base.h"
#include "subject_managed_panel.h"

namespace helix::settings {

/**
 * @class TelemetryDataOverlay
 * @brief Overlay for displaying queued telemetry events
 *
 * This overlay provides:
 * - Status card showing telemetry state and event count
 * - List of event cards with type, timestamp, and key fields
 * - Clear queue button to purge all pending events
 * - Empty state when no events are queued
 *
 * ## Usage:
 *
 * @code
 * auto& overlay = helix::settings::get_telemetry_data_overlay();
 * overlay.show(parent_screen);
 * @endcode
 */
class TelemetryDataOverlay : public OverlayBase {
  public:
    TelemetryDataOverlay();
    ~TelemetryDataOverlay() override;

    // Non-copyable (inherited from OverlayBase)

    //
    // === OverlayBase Interface ===
    //

    /**
     * @brief Initialize subjects for reactive binding
     */
    void init_subjects() override;

    /**
     * @brief Create the overlay UI (called lazily)
     * @param parent Parent widget to attach overlay to (usually screen)
     * @return Root object of overlay, or nullptr on failure
     */
    lv_obj_t* create(lv_obj_t* parent) override;

    /**
     * @brief Get human-readable overlay name
     * @return "Telemetry Data"
     */
    const char* get_name() const override {
        return "Telemetry Data";
    }

    /**
     * @brief Register event callbacks with lv_xml system
     */
    void register_callbacks() override;

    /**
     * @brief Called when overlay becomes visible - populates event list
     */
    void on_activate() override;

    //
    // === Public Interface ===
    //

    /**
     * @brief Show the overlay (populates events first)
     * @param parent_screen The parent screen for overlay creation
     */
    void show(lv_obj_t* parent_screen);

    /**
     * @brief Handle clear queue button click
     */
    void handle_clear_queue();

  private:
    //
    // === Internal Methods ===
    //

    /**
     * @brief Populate event list from TelemetryManager queue snapshot
     */
    void populate_events();

    /**
     * @brief Update status card text based on current state
     */
    void update_status();

    //
    // === Subjects ===
    //

    /// RAII manager for automatic subject cleanup
    SubjectManager subjects_;

    /// Status text subjects
    lv_subject_t status_subject_;
    lv_subject_t detail_subject_;
    lv_subject_t count_subject_;

    char status_buf_[64];
    char detail_buf_[128];

    //
    // === Static Callbacks ===
    //

    static void on_telemetry_clear_queue(lv_event_t* e);
};

/**
 * @brief Global instance accessor
 * @return Reference to singleton TelemetryDataOverlay
 */
TelemetryDataOverlay& get_telemetry_data_overlay();

} // namespace helix::settings
