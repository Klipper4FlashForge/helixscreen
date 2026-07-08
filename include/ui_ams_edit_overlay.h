// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui_color_picker.h"
#include "ui_filament_catalog_selector.h"
#include "ui_spoolman_edit_modal.h"

#include "ams_types.h"
#include "overlay_base.h"
#include "spoolman_types.h"
#include "subject_managed_panel.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "hv/json.hpp"

class MoonrakerAPI;
class AmsEditOverlayTestAccess;
class AmsEditOverlayViewTestAccess;

namespace helix::ui {

/**
 * @file ui_ams_edit_overlay.h
 * @brief NavigationManager overlay for editing AMS filament slot properties
 *
 * Single overlay hosting internal views selected by the ams_edit_view int
 * subject (spec §13): 0=overview, 1=Spoolman spool picker (2-4 added by later
 * redesign phases). Header back button routes per-view; header Save action is
 * dirty-gated via the ams_edit_save_disabled subject.
 *
 * Lifecycle: on_deactivate() fires both when the overlay is POPPED and when it
 * is merely COVERED (e.g. QR scanner pushed on top). Completion therefore
 * fires only from Save / back-on-overview (guarded by completion_fired_), with
 * a NavigationManager close callback as the backdrop-tap safety net.
 *
 * ## Usage:
 * @code
 * helix::ui::get_ams_edit_overlay().show_for_slot(
 *     parent, slot_index, slot_info, api,
 *     [](const helix::ui::AmsEditOverlay::EditResult& result) {
 *         if (result.saved) { ... apply result.slot_info to backend ... }
 *     });
 * @endcode
 */
class AmsEditOverlay : public OverlayBase {
  public:
    /**
     * @brief Result returned when the editor closes
     */
    struct EditResult {
        bool saved = false;  ///< True if user saved, false if cancelled/dismissed
        int slot_index = -1; ///< Slot that was edited
        SlotInfo slot_info;  ///< Final slot info (valid if saved)
    };

    using CompletionCallback = std::function<void(const EditResult& result)>;

    // View states for the ams_edit_view subject (spec §13 / review §3)
    static constexpr int kViewOverview = 0;
    static constexpr int kViewSpoolPicker = 1;
    static constexpr int kViewFilamentDetails = 2; // Phase 5
    static constexpr int kViewColor = 3;           // Phase 6
    static constexpr int kViewSpoolDetails = 4;    // Phase 7

    AmsEditOverlay();
    ~AmsEditOverlay() override;

    // Non-copyable, non-movable (singleton; move ops deliberately not ported)
    AmsEditOverlay(const AmsEditOverlay&) = delete;
    AmsEditOverlay& operator=(const AmsEditOverlay&) = delete;

    // === OverlayBase interface ===
    void init_subjects() override;
    lv_obj_t* create(lv_obj_t* parent) override;
    void register_callbacks() override;
    [[nodiscard]] const char* get_name() const override {
        return "AMS Slot Editor";
    }
    void on_activate() override;
    void on_deactivate() override;
    void on_ui_destroyed() override;

    /**
     * @brief Open the editor for a specific slot (pushes the overlay)
     * @param parent Parent screen (fallback if no active screen)
     * @param slot_index Slot being edited (0-based; -2 = external spool)
     * @param initial_info Initial slot info to populate the overview
     * @param api MoonrakerAPI for Spoolman sync (may be nullptr)
     * @param on_complete Fired exactly once when the editor closes
     * @param open_on_picker Open directly on the Spoolman picker (#1071)
     * @return true if the overlay was pushed
     */
    bool show_for_slot(lv_obj_t* parent, int slot_index, const SlotInfo& initial_info,
                       MoonrakerAPI* api, CompletionCallback on_complete,
                       bool open_on_picker = false);

  private:
    // === State ===
    int slot_index_ = -1;
    SlotInfo original_info_; ///< Original info for dirty comparison
    SlotInfo working_info_;  ///< Working copy being edited
    // Explicit "Save to Spoolman" opt-in captured from the filament-details
    // toggle (spec §3.3). Reset per show_for_slot; false = local-only save.
    bool save_to_spoolman_opt_in_ = false;
    MoonrakerAPI* api_ = nullptr;
    CompletionCallback completion_callback_;
    bool completion_fired_ = false;  ///< Guards single-fire completion
    int remaining_pre_edit_pct_ = 0; ///< Remaining % before edit mode

    /// Cached overlay widget for lazy_create_and_push_overlay
    lv_obj_t* cached_overlay_widget_ = nullptr;

    // === Spoolman spool detail editor (stacked modal until Phase 7) ===
    SpoolEditModal spool_edit_modal_;

    // === Filament-details view (kViewFilamentDetails) ===
    FilamentCatalogSelector details_selector_;
    uint32_t details_color_ = 0;     ///< pending color chosen in the details view
    bool details_color_set_ = false; ///< true once the user picked a color there

    // === Color view (kViewColor) ===
    lv_subject_t color_mode_subject_; ///< 0=presets, 1=custom ("ams_edit_color_mode")
    int return_view_ = kViewOverview; ///< where the color view pops back to
    uint32_t custom_color_ = 0x808080;

    void open_color_view(int return_view);
    void apply_color(uint32_t rgb);
    void handle_color_swatch(lv_obj_t* swatch);
    void handle_custom_color_changed(uint32_t rgb);
    void handle_color_hex_changed();
    void handle_color_apply();
    static void on_color_swatch_cb(lv_event_t* e);
    static void on_color_mode_presets_cb(lv_event_t* e);
    static void on_color_mode_custom_cb(lv_event_t* e);
    static void on_color_apply_cb(lv_event_t* e);
    static void on_color_hex_changed_cb(lv_event_t* e);

    // === Subjects for XML binding ===
    SubjectManager subjects_;
    lv_subject_t slot_indicator_subject_;
    lv_subject_t color_name_subject_;
    lv_subject_t spool_name_subject_;
    lv_subject_t temp_nozzle_subject_;
    lv_subject_t temp_bed_subject_;
    lv_subject_t remaining_pct_subject_;
    lv_subject_t remaining_mode_subject_; ///< 0=view, 1=edit ("edit_remaining_mode")
    lv_subject_t view_mode_subject_;      ///< kView* ("ams_edit_view")
    lv_subject_t picker_state_subject_;   ///< 0=loading, 1=empty, 2=content
    lv_subject_t save_disabled_subject_;  ///< 1=Save disabled ("ams_edit_save_disabled")
    lv_subject_t is_managed_subject_;     ///< 1=linked Spoolman spool ("ams_edit_is_managed")
    lv_subject_t chip_text_subject_;      ///< identity chip label text
    char chip_text_buf_[96] = {0};

    char slot_indicator_buf_[32] = {0};
    char color_name_buf_[32] = {0};
    char spool_name_buf_[64] = {0};
    char temp_nozzle_buf_[16] = {0};
    char temp_bed_buf_[16] = {0};
    char remaining_pct_buf_[48] = {0};

    // === Observer tracking for cleanup [L020] ===
    lv_observer_t* slot_indicator_observer_ = nullptr;
    lv_observer_t* color_name_observer_ = nullptr;
    lv_observer_t* spool_name_observer_ = nullptr;
    lv_observer_t* temp_nozzle_observer_ = nullptr;
    lv_observer_t* temp_bed_observer_ = nullptr;
    lv_observer_t* remaining_pct_observer_ = nullptr;
    lv_observer_t* chip_text_observer_ = nullptr;

    // === Dropdown data (removed in Phase 3/8 with the dropdowns) ===
    std::string material_options_;
    std::vector<std::string> material_list_;
    std::string vendor_options_;
    std::vector<VendorInfo> vendor_list_;
    bool vendors_loaded_ = false;

    // === Picker state (Spoolman spool selection) ===
    std::vector<SpoolInfo> cached_spools_;

    // === Internal Methods ===
    void fetch_vendors_from_spoolman();
    void update_vendor_dropdown();
    void deinit_subjects();
    void update_ui();
    void update_temp_display();
    void format_remaining_label(int pct);
    bool is_dirty() const;
    void update_sync_button_state();
    lv_obj_t* find_widget(const char* name) const;

    // === View switching ===
    void switch_to_picker();
    void switch_to_form();
    void populate_picker();
    void render_spool_list(const std::string& filter);
    void handle_spool_selected(int spool_id);
    void enter_filament_details();
    void handle_details_select();
    void handle_quick_swatch(lv_obj_t* swatch);
    void handle_unlink();
    void handle_picker_search(const char* text);
    void update_spoolman_button_state();

    // === Completion / close orchestration ===
    void fire_completion(bool saved); ///< Idempotent; does NOT pop the overlay
    void close_editor(bool saved);    ///< fire_completion + NavigationManager go_back

    friend class ::AmsEditOverlayTestAccess;
    friend class ::AmsEditOverlayViewTestAccess;

    // === Event Handlers ===
    void handle_back(); ///< Header back: per-view routing (cancel on overview)
    void handle_chip_clicked();
    void handle_setup_entry();
    void handle_color_clicked();
    void handle_remaining_changed(int percent);
    void handle_remaining_edit();
    void handle_remaining_accept();
    void handle_remaining_cancel();
    void handle_save();

    // Pure decision for whether handle_save() should create a NEW Spoolman
    // spool from the working slot: only for an unlinked slot, only when the
    // user explicitly opted in via the "Save to Spoolman" toggle (spec §3.3 —
    // replaces the silent auto-create / filament_user_edited_ heuristic,
    // #1071), and only when the metadata is complete.
    static bool should_create_new_spool(const SlotInfo& working_info, bool save_to_spoolman);

    static bool is_material_identity_change(const SlotInfo& original, const SlotInfo& edited);

    // Pure: true when saving would push a changed filament identity onto a
    // LINKED Spoolman spool — material change or color beyond match tolerance
    // on a slot that stays linked. Generalized to ALL Spoolman backends
    // (spec §6; formerly AD5X-only).
    static bool needs_identity_confirmation(const SlotInfo& original, const SlotInfo& edited);

    // Pure: split spool edits into the two Spoolman PATCH bodies — per-spool
    // fields vs shared-filament fields (same split as SpoolEditModal, which
    // stays standalone for SpoolmanPanel; review §1.2).
    static void build_spool_patches(const SpoolInfo& original, const SpoolInfo& edited,
                                    nlohmann::json& spool_patch, nlohmann::json& filament_patch);

    void do_spoolman_save();
    void prompt_identity_change_then_save();
    static void on_identity_confirm_cb(lv_event_t* e);
    static void on_identity_cancel_cb(lv_event_t* e);
#if HELIX_HAS_LABEL_PRINTER
    void handle_print_label();
#endif
    void handle_scan_qr();
    void handle_spool_details();
    void handle_tool_changed(int index);
    void handle_weight_input_changed();

    // === Static Callback Registration ===
    static bool callbacks_registered_;

    // === Static Callbacks ===
    static void on_back_cb(lv_event_t* e);
    static void on_chip_clicked_cb(lv_event_t* e);
    static void on_spool_details_cb(lv_event_t* e);
    static void on_setup_entry_cb(lv_event_t* e);
    static void on_details_select_cb(lv_event_t* e);
    static void on_quick_swatch_cb(lv_event_t* e);
    static void on_custom_color_cb(lv_event_t* e);
    static void on_color_clicked_cb(lv_event_t* e);
    static void on_remaining_changed_cb(lv_event_t* e);
    static void on_remaining_edit_cb(lv_event_t* e);
    static void on_remaining_accept_cb(lv_event_t* e);
    static void on_remaining_cancel_cb(lv_event_t* e);
    static void on_save_cb(lv_event_t* e);
    static void on_spool_actions_clicked_cb(lv_event_t* e);
    static void on_spool_actions_changed_cb(lv_event_t* e);
    static void on_scan_qr_cb(lv_event_t* e);
    static void on_picker_search_cb(lv_event_t* e);
    static void on_picker_retry_cb(lv_event_t* e);
    static void on_spool_item_cb(lv_event_t* e);
    static void on_spool_item_edit_cb(lv_event_t* e);
    static void on_tool_changed_cb(lv_event_t* e);
    static void on_weight_changed_cb(lv_event_t* e);

    /**
     * @brief Resolve the editor instance for a static XML callback.
     * The editor is a process-lifetime singleton, so this is just the accessor
     * (replaces the modal's s_active_instance_ routing).
     */
    static AmsEditOverlay* get_instance_from_event(lv_event_t* e);
};

/**
 * @brief Global instance accessor (creates on first use, registers cleanup
 *        with StaticPanelRegistry)
 */
AmsEditOverlay& get_ams_edit_overlay();

} // namespace helix::ui
