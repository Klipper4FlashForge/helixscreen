// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_ams_edit_overlay.h"

#include "ui_button.h"
#include "ui_callback_helpers.h"
#include "ui_error_reporting.h"
#include "ui_split_button.h"
#include "ui_swatch.h"
#include "ui_update_queue.h"
#include "ui_utils.h"

#include "static_panel_registry.h"
#include "ui/ui_lazy_panel_helper.h"
#include "ui_nav_manager.h"

#include "ams_state.h"
#include "app_globals.h"
#include "color_utils.h"
#include "filament_database.h"
#include "filament_mapper.h"
#include "format_utils.h"
#if HELIX_HAS_LABEL_PRINTER
#include "ipp_print_modal.h"
#include "label_printer_settings.h"
#include "label_printer_utils.h"
#endif
#include "ui_breakpoint.h"
#include "ui_overlay_qr_scanner.h"
#include "ui_toast_manager.h"

#include "moonraker_api.h"
#include "printer_state.h"
#include "spoolman_slot_saver.h"
#include "spoolman_types.h"
#include "theme_manager.h"
#include "tool_state.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace helix::ui {

// Static member initialization
bool AmsEditOverlay::callbacks_registered_ = false;

// Fire-and-forget: notify Moonraker of the active spool so other clients
// (Mainsail, Fluidd) see the change and filament tracking works.
// Pass 0 to clear the active spool (unlink).
static void sync_active_spool(MoonrakerAPI* api, int spool_id) {
    spdlog::info("[AmsEditOverlay] Syncing active spool to {} on server", spool_id);
    api->spoolman().set_active_spool(
        spool_id,
        [spool_id]() {
            spdlog::debug("[AmsEditOverlay] Active spool synced to {} on server", spool_id);
        },
        [spool_id](const MoonrakerError& err) {
            spdlog::warn("[AmsEditOverlay] Failed to sync active spool to {}: {}", spool_id,
                         err.message);
        });
}

// ============================================================================
// Construction / Destruction
// ============================================================================

namespace {
std::unique_ptr<AmsEditOverlay> g_ams_edit_overlay;
} // namespace

AmsEditOverlay& get_ams_edit_overlay() {
    if (!g_ams_edit_overlay) {
        g_ams_edit_overlay = std::make_unique<AmsEditOverlay>();
        StaticPanelRegistry::instance().register_destroy("AmsEditOverlay",
                                                         []() { g_ams_edit_overlay.reset(); });
    }
    return *g_ams_edit_overlay;
}

AmsEditOverlay::AmsEditOverlay() {
    spdlog::debug("[AmsEditOverlay] Constructed");
}

AmsEditOverlay::~AmsEditOverlay() {
    // Deinitialize subjects first to disconnect observers [L041]
    deinit_subjects();
    spdlog::trace("[AmsEditOverlay] Destroyed");
}

lv_obj_t* AmsEditOverlay::find_widget(const char* name) const {
    return overlay_root_ ? lv_obj_find_by_name(overlay_root_, name) : nullptr;
}

// ============================================================================
// Public API
// ============================================================================

bool AmsEditOverlay::show_for_slot(lv_obj_t* parent, int slot_index, const SlotInfo& initial_info,
                                   MoonrakerAPI* api, CompletionCallback on_complete,
                                   bool open_on_picker) {
    // Store per-invocation state (QrScannerOverlay pattern: params + callback
    // stored on the singleton before push)
    slot_index_ = slot_index;
    original_info_ = initial_info;
    working_info_ = initial_info;
    filament_user_edited_ = false; // no user edit yet this session (#1071)
    api_ = api ? api : get_moonraker_api();
    completion_callback_ = std::move(on_complete);
    completion_fired_ = false;
    remaining_pre_edit_pct_ = 0;
    cached_spools_.clear();
    vendors_loaded_ = false;

    // Always prefer the active screen so the overlay renders above everything
    lv_obj_t* screen = lv_screen_active();
    bool ok = lazy_create_and_push_overlay<AmsEditOverlay>(
        get_ams_edit_overlay, cached_overlay_widget_, screen ? screen : parent, "AMS Slot Editor",
        "AmsEditOverlay");
    if (!ok) {
        spdlog::error("[AmsEditOverlay] Failed to push overlay for slot {}", slot_index);
        return false;
    }

    // Safety net: a dismissal that bypasses our handlers (backdrop tap,
    // external go_back) must still complete as "not saved". fire_completion is
    // idempotent, so the Save/back paths that already fired are unaffected.
    NavigationManager::instance().register_overlay_close_callback(
        cached_overlay_widget_, []() { get_ams_edit_overlay().fire_completion(false); });

    // Reset per-session view state HERE (covered-safe — on_deactivate must not
    // touch it, since it also fires when the QR scanner merely covers us).
    lv_subject_set_int(&remaining_mode_subject_, 0);
    lv_subject_set_int(&view_mode_subject_, open_on_picker ? kViewSpoolPicker : kViewOverview);
    if (open_on_picker) {
        populate_picker();
    }

    // If linked to Spoolman, fetch authoritative filament data (vendor, material, color)
    // so the form shows current Spoolman state, not stale backend data
    if (working_info_.spoolman_id > 0 && api_) {
        const int spool_id = working_info_.spoolman_id;
        auto token = lifetime_.token();
        api_->spoolman().get_spoolman_spool(
            spool_id,
            [this, token, spool_id](const std::optional<SpoolInfo>& spool) {
                if (!spool || token.expired())
                    return;
                // Capture Spoolman's authoritative data for the spool
                int fetched_filament_id = spool->filament_id;
                int fetched_vendor_id = spool->vendor_id;
                std::string fetched_vendor = spool->vendor;
                std::string fetched_material = spool->material;
                std::string fetched_color_hex = spool->color_hex;
                token.defer([this, spool_id, fetched_filament_id, fetched_vendor_id,
                             fetched_vendor = std::move(fetched_vendor),
                             fetched_material = std::move(fetched_material),
                             fetched_color_hex = std::move(fetched_color_hex)]() {
                    if (fetched_filament_id > 0) {
                        original_info_.spoolman_filament_id = fetched_filament_id;
                        working_info_.spoolman_filament_id = fetched_filament_id;
                    }
                    if (fetched_vendor_id > 0) {
                        original_info_.spoolman_vendor_id = fetched_vendor_id;
                        working_info_.spoolman_vendor_id = fetched_vendor_id;
                    }
                    // Update brand/material from Spoolman (authoritative source)
                    if (!fetched_vendor.empty() && working_info_.brand != fetched_vendor) {
                        spdlog::debug(
                            "[AmsEditOverlay] Updating vendor from '{}' to '{}' (Spoolman spool {})",
                            working_info_.brand, fetched_vendor, spool_id);
                        original_info_.brand = fetched_vendor;
                        working_info_.brand = fetched_vendor;
                    }
                    if (!fetched_material.empty() && working_info_.material != fetched_material) {
                        spdlog::debug("[AmsEditOverlay] Updating material from '{}' to '{}' "
                                      "(Spoolman spool {})",
                                      working_info_.material, fetched_material, spool_id);
                        original_info_.material = fetched_material;
                        working_info_.material = fetched_material;
                    }
                    if (!fetched_color_hex.empty()) {
                        uint32_t rgb = 0;
                        if (helix::parse_hex_color(fetched_color_hex.c_str(), rgb) &&
                            working_info_.color_rgb != rgb) {
                            spdlog::debug("[AmsEditOverlay] Updating color from {:#08x} to {:#08x} "
                                          "(Spoolman spool {})",
                                          working_info_.color_rgb, rgb, spool_id);
                            original_info_.color_rgb = rgb;
                            working_info_.color_rgb = rgb;
                        }
                    }
                    spdlog::debug("[AmsEditOverlay] Synced spool {} from Spoolman: vendor='{}', "
                                  "material='{}', filament_id={}, vendor_id={}",
                                  spool_id, working_info_.brand, working_info_.material,
                                  fetched_filament_id, fetched_vendor_id);
                    update_ui();
                });
            },
            [spool_id](const MoonrakerError& err) {
                spdlog::warn("[AmsEditOverlay] Failed to fetch spool {}: {}", spool_id, err.message);
            });
    }

    spdlog::info("[AmsEditOverlay] Shown for slot {} (spoolman_id={}, brand={}, material={})",
                 slot_index, initial_info.spoolman_id, initial_info.brand, initial_info.material);
    return true;
}

// ============================================================================
// OverlayBase Hooks
// ============================================================================

lv_obj_t* AmsEditOverlay::create(lv_obj_t* parent) {
    if (!create_overlay_from_xml(parent, "ams_edit_overlay")) {
        return nullptr;
    }

    // Bind labels to subjects ONCE per widget tree (the tree is cached across
    // opens — binding in on_activate would stack duplicate observers).
    // Header title comes from the shared header_bar ("header_title").
    lv_obj_t* header_title = find_widget("header_title");
    if (header_title) {
        slot_indicator_observer_ =
            lv_label_bind_text(header_title, &slot_indicator_subject_, nullptr);
    }

    lv_obj_t* color_name_label = find_widget("color_name_label");
    if (color_name_label) {
        color_name_observer_ = lv_label_bind_text(color_name_label, &color_name_subject_, nullptr);
    }

    lv_obj_t* spool_name_label = find_widget("spool_name_label");
    if (spool_name_label) {
        spool_name_observer_ = lv_label_bind_text(spool_name_label, &spool_name_subject_, nullptr);
    }

    lv_obj_t* temp_nozzle_label = find_widget("temp_nozzle_label");
    if (temp_nozzle_label) {
        temp_nozzle_observer_ =
            lv_label_bind_text(temp_nozzle_label, &temp_nozzle_subject_, nullptr);
    }

    lv_obj_t* temp_bed_label = find_widget("temp_bed_label");
    if (temp_bed_label) {
        temp_bed_observer_ = lv_label_bind_text(temp_bed_label, &temp_bed_subject_, nullptr);
    }

    lv_obj_t* remaining_pct_label = find_widget("remaining_pct_label");
    if (remaining_pct_label) {
        remaining_pct_observer_ =
            lv_label_bind_text(remaining_pct_label, &remaining_pct_subject_, nullptr);
    }

    spdlog::info("[AmsEditOverlay] Overlay created");
    return overlay_root_;
}

void AmsEditOverlay::on_ui_destroyed() {
    cached_overlay_widget_ = nullptr;
    slot_indicator_observer_ = nullptr;
    color_name_observer_ = nullptr;
    spool_name_observer_ = nullptr;
    temp_nozzle_observer_ = nullptr;
    temp_bed_observer_ = nullptr;
    remaining_pct_observer_ = nullptr;
}

void AmsEditOverlay::on_activate() {
    OverlayBase::on_activate();

    // Fetch vendor list from Spoolman (async, will update dropdown when ready)
    fetch_vendors_from_spoolman();

    // Refresh the UI with current slot data
    update_ui();
    update_sync_button_state();
    update_spoolman_button_state();
}

void AmsEditOverlay::on_deactivate() {
    // Fires when POPPED **and** when COVERED (e.g. QR scanner pushed on top).
    // Must NOT fire completion or reset the view subject — session state
    // resets happen in show_for_slot(). Base invalidates lifetime_ (pending
    // Spoolman fetches for this activation are dropped; token() re-arms).
    OverlayBase::on_deactivate();
    spdlog::debug("[AmsEditOverlay] on_deactivate()");
}

// ============================================================================
// Subject Management
// ============================================================================

void AmsEditOverlay::init_subjects() {
    init_subjects_guarded([this]() {
        // Initialize string subjects with empty/default buffers (bound in
        // create(), not XML-registered)
        slot_indicator_buf_[0] = '-';
        slot_indicator_buf_[1] = '-';
        slot_indicator_buf_[2] = '\0';
        color_name_buf_[0] = '\0';
        spool_name_buf_[0] = '\0';
        snprintf(temp_nozzle_buf_, sizeof(temp_nozzle_buf_), "200-230°C");
        snprintf(temp_bed_buf_, sizeof(temp_bed_buf_), "60°C");
        snprintf(remaining_pct_buf_, sizeof(remaining_pct_buf_), "100%%");

        lv_subject_init_string(&slot_indicator_subject_, slot_indicator_buf_, nullptr,
                               sizeof(slot_indicator_buf_), "--");
        subjects_.register_subject(&slot_indicator_subject_);

        lv_subject_init_string(&color_name_subject_, color_name_buf_, nullptr,
                               sizeof(color_name_buf_), "");
        subjects_.register_subject(&color_name_subject_);

        lv_subject_init_string(&spool_name_subject_, spool_name_buf_, nullptr,
                               sizeof(spool_name_buf_), "");
        subjects_.register_subject(&spool_name_subject_);

        lv_subject_init_string(&temp_nozzle_subject_, temp_nozzle_buf_, nullptr,
                               sizeof(temp_nozzle_buf_), "200-230°C");
        subjects_.register_subject(&temp_nozzle_subject_);

        lv_subject_init_string(&temp_bed_subject_, temp_bed_buf_, nullptr, sizeof(temp_bed_buf_),
                               "60°C");
        subjects_.register_subject(&temp_bed_subject_);

        lv_subject_init_string(&remaining_pct_subject_, remaining_pct_buf_, nullptr,
                               sizeof(remaining_pct_buf_), "100%");
        subjects_.register_subject(&remaining_pct_subject_);

        // Remaining mode (0=view, 1=edit) - registered globally for XML binding
        UI_MANAGED_SUBJECT_INT(remaining_mode_subject_, 0, "edit_remaining_mode", subjects_);

        // View state (kViewOverview..kViewSpoolDetails) - registered globally
        UI_MANAGED_SUBJECT_INT(view_mode_subject_, 0, "ams_edit_view", subjects_);

        // Picker state (0=loading, 1=empty, 2=content) - registered globally
        UI_MANAGED_SUBJECT_INT(picker_state_subject_, 0, "edit_picker_state", subjects_);

        // Header Save button dirty gate (1=disabled). Starts disabled — nothing
        // is dirty when the editor opens.
        UI_MANAGED_SUBJECT_INT(save_disabled_subject_, 1, "ams_edit_save_disabled", subjects_);
    });
}

void AmsEditOverlay::deinit_subjects() {
    deinit_subjects_base(subjects_);
}

void AmsEditOverlay::fetch_vendors_from_spoolman() {
    // Resolve API: prefer stored api_, fall back to global
    if (!api_) {
        api_ = get_moonraker_api();
    }
    if (!api_ || vendors_loaded_) {
        return;
    }

    // Skip Spoolman API call if not configured (avoids "method not found" toast)
    auto* spoolman_subj = lv_xml_get_subject(nullptr, "printer_has_spoolman");
    if (spoolman_subj && lv_subject_get_int(spoolman_subj) != 1) {
        return;
    }

    auto token = lifetime_.token();

    // Use dedicated vendor endpoint instead of downloading all spools
    api_->spoolman().get_spoolman_vendors(
        [this, token](const std::vector<VendorInfo>& vendors_result) {
            if (token.expired())
                return;
            // Build vendor list on background thread, then marshal to main
            std::set<std::string> unique_vendors;
            unique_vendors.insert("Generic"); // Always have Generic as first option
            for (const auto& vendor : vendors_result) {
                if (!vendor.name.empty()) {
                    unique_vendors.insert(vendor.name);
                }
            }

            // Build vendor list with IDs and options string (local copies, no member access)
            // Build a name→id map for lookup
            std::map<std::string, int> vendor_id_map;
            for (const auto& vendor : vendors_result) {
                if (!vendor.name.empty()) {
                    vendor_id_map[vendor.name] = vendor.id;
                }
            }

            std::vector<VendorInfo> vendors;
            std::string options;
            for (const auto& name : unique_vendors) {
                if (!options.empty()) {
                    options += '\n';
                }
                options += name;
                VendorInfo vi;
                vi.name = name;
                auto it = vendor_id_map.find(name);
                vi.id = (it != vendor_id_map.end()) ? it->second : 0;
                vendors.push_back(std::move(vi));
            }

            // Marshal member writes to main thread
            token.defer(
                [this, vendors = std::move(vendors), options = std::move(options)]() mutable {
                    vendor_list_ = std::move(vendors);
                    vendor_options_ = std::move(options);
                    vendors_loaded_ = true;
                    spdlog::debug("[AmsEditOverlay] Loaded {} vendors from Spoolman",
                                  vendor_list_.size());
                    update_vendor_dropdown();
                });
        },
        [](const MoonrakerError& err) {
            spdlog::warn("[AmsEditOverlay] Failed to fetch Spoolman vendors: {}", err.message);
            // Keep using fallback vendors
        });
}

void AmsEditOverlay::update_vendor_dropdown() {
    if (!overlay_root_ || vendor_options_.empty()) {
        return;
    }

    lv_obj_t* vendor_dropdown = find_widget("vendor_dropdown");
    if (!vendor_dropdown) {
        return;
    }

    lv_dropdown_set_options(vendor_dropdown, vendor_options_.c_str());

    // Set selection based on working_info_.brand, and populate vendor_id if missing.
    // Default to "Generic" (not necessarily index 0 since the list is alphabetical).
    int vendor_idx = -1;
    int generic_idx = 0;
    for (size_t i = 0; i < vendor_list_.size(); i++) {
        if (vendor_list_[i].name == "Generic") {
            generic_idx = static_cast<int>(i);
        }
        if (!working_info_.brand.empty() && working_info_.brand == vendor_list_[i].name) {
            vendor_idx = static_cast<int>(i);
            if (working_info_.spoolman_vendor_id == 0 && vendor_list_[i].id > 0) {
                working_info_.spoolman_vendor_id = vendor_list_[i].id;
                spdlog::debug("[AmsEditOverlay] Resolved vendor_id={} from vendor list for '{}'",
                              vendor_list_[i].id, vendor_list_[i].name);
            }
            break;
        }
    }
    if (vendor_idx < 0) {
        vendor_idx = generic_idx;
        if (working_info_.brand.empty())
            working_info_.brand = "Generic";
        if (original_info_.brand.empty())
            original_info_.brand = "Generic";
    }
    lv_dropdown_set_selected(vendor_dropdown, vendor_idx);
}

// ============================================================================
// View Switching
// ============================================================================

void AmsEditOverlay::switch_to_picker() {
    if (!subjects_initialized_) {
        spdlog::warn("[AmsEditOverlay] switch_to_picker() aborted: subjects not initialized");
        return;
    }
    spdlog::debug("[AmsEditOverlay] Switching to picker view (overlay_root_={}, api_={})",
                  static_cast<void*>(overlay_root_), static_cast<void*>(api_));
    lv_subject_set_int(&view_mode_subject_, kViewSpoolPicker);
    populate_picker();
}

void AmsEditOverlay::switch_to_form() {
    if (!subjects_initialized_) {
        return;
    }
    lv_subject_set_int(&view_mode_subject_, kViewOverview);
    spdlog::debug("[AmsEditOverlay] Switched to form view");
}

void AmsEditOverlay::populate_picker() {
    // Resolve API: prefer stored api_, fall back to global (matches SpoolmanPanel pattern)
    if (!api_) {
        api_ = get_moonraker_api();
    }
    if (!overlay_root_ || !api_) {
        spdlog::warn("[AmsEditOverlay] populate_picker() aborted: overlay_root_={}, api_={}",
                     static_cast<void*>(overlay_root_), static_cast<void*>(api_));
        lv_subject_set_int(&picker_state_subject_, 1);
        return;
    }

    // Show loading state
    lv_subject_set_int(&picker_state_subject_, 0);

    // Clear search input
    lv_obj_t* search = find_widget("picker_search");
    if (search) {
        lv_textarea_set_text(search, "");
    }

    auto token = lifetime_.token();

    spdlog::debug("[AmsEditOverlay] populate_picker() fetching spools from Spoolman...");

    api_->spoolman().get_spoolman_spools(
        [this, token](const std::vector<SpoolInfo>& spools) {
            if (token.expired())
                return;
            spdlog::debug("[AmsEditOverlay] Spoolman returned {} spools", spools.size());
            token.defer([this, spools]() {
                if (!overlay_root_) {
                    spdlog::warn("[AmsEditOverlay] populate_picker callback dropped: overlay_root_ null");
                    return;
                }
                if (!subjects_initialized_) {
                    spdlog::warn("[AmsEditOverlay] populate_picker callback dropped: subjects not "
                                 "initialized");
                    return;
                }

                if (spools.empty()) {
                    spdlog::debug("[AmsEditOverlay] Spoolman returned empty spool list");
                    lv_subject_set_int(&picker_state_subject_, 1);
                    return;
                }

                // Spools arrive already ordered most-recently-used first (then
                // most-recently-created for never-used) — sort_spools_by_recency()
                // is applied once in the API layer on fetch (#1071). filter_spools()
                // preserves order, so filtering doesn't need to re-sort.
                cached_spools_ = spools;
                render_spool_list("");
            });
        },
        [this, token](const MoonrakerError& err) {
            spdlog::warn("[AmsEditOverlay] Spoolman fetch error: {}", err.message);
            token.defer([this, msg = err.message]() {
                if (!overlay_root_ || !subjects_initialized_) {
                    spdlog::warn("[AmsEditOverlay] Error callback dropped: overlay_root_={}, "
                                 "subjects={}",
                                 static_cast<void*>(overlay_root_), subjects_initialized_);
                    return;
                }
                spdlog::warn("[AmsEditOverlay] Failed to fetch spools: {}", msg);
                lv_subject_set_int(&picker_state_subject_, 1);
            });
        });
}

void AmsEditOverlay::render_spool_list(const std::string& filter) {
    lv_obj_t* spool_list = find_widget("picker_spool_list");
    if (!spool_list) {
        return;
    }

    // Invoked from a token.defer() callback (UpdateQueue batch). Sync
    // lv_obj_clean in that context corrupts LVGL's event linked list →
    // SIGSEGV in lv_event_mark_deleted (#776).
    helix::ui::safe_clean_children(spool_list);

    // Reuse shared filter_spools() from spoolman_types
    auto filtered = filter_spools(cached_spools_, filter);

    // Get spool IDs assigned to other tools (exclude current slot's tool)
    auto in_use = ToolState::instance().assigned_spool_ids(slot_index_);

    // Compact single-line rows on short panels (more rows visible). Keyed on the
    // same responsive breakpoint the design tokens use (VERTICAL resolution).
    // Medium (≤550px, e.g. 800x480) and below get compact; Large+ keep the rich
    // two-line layout. Computed once — the breakpoint is constant per render.
    lv_subject_t* bp_subj = theme_manager_get_breakpoint_subject();
    UiBreakpoint bp = bp_subj ? as_breakpoint(lv_subject_get_int(bp_subj)) : UiBreakpoint::Medium;
    const bool is_compact = bp <= UiBreakpoint::Medium;
    const char* attrs[] = {"compact",     is_compact ? "true" : "false",
                           "detail_flow", is_compact ? "row" : "column",
                           nullptr,       nullptr};

    for (const auto& spool : filtered) {
        lv_obj_t* item =
            static_cast<lv_obj_t*>(lv_xml_create(spool_list, "spoolman_spool_item", attrs));
        if (!item) {
            continue;
        }

        lv_obj_set_user_data(item, reinterpret_cast<void*>(static_cast<intptr_t>(spool.id)));

        lv_obj_t* name_label = lv_obj_find_by_name(item, "spool_name");
        if (name_label) {
            std::string name = "#" + std::to_string(spool.id) + " ";
            name += spool.vendor.empty() ? spool.material : (spool.vendor + " " + spool.material);
            lv_label_set_text(name_label, name.c_str());
        }

        lv_obj_t* color_label = lv_obj_find_by_name(item, "spool_color");
        if (color_label && !spool.color_name.empty()) {
            lv_label_set_text(color_label, spool.color_name.c_str());
        }

        lv_obj_t* weight_label = lv_obj_find_by_name(item, "spool_weight");
        if (weight_label && spool.remaining_weight_g > 0) {
            char buf[32];
            snprintf(buf, sizeof(buf), "%.0fg", spool.remaining_weight_g);
            lv_label_set_text(weight_label, buf);
        }

        lv_obj_t* swatch = lv_obj_find_by_name(item, "spool_swatch");
        if (swatch) {
            uint32_t rgb = helix::parse_hex_color(spool.color_hex).value_or(0x808080);
            helix::ui::apply_swatch_color(swatch, rgb, spool.multi_color_hexes);
        }

        // Mark current spool as checked (matching spoolman list view pattern)
        bool is_selected = (spool.id == working_info_.spoolman_id);
        lv_obj_set_state(item, LV_STATE_CHECKED, is_selected);
        if (is_selected) {
            lv_obj_t* check_icon = lv_obj_find_by_name(item, "selected_icon");
            if (check_icon) {
                lv_obj_remove_flag(check_icon, LV_OBJ_FLAG_HIDDEN);
            }
        }

        // Disable spools already assigned to other tools
        if (in_use.count(spool.id)) {
            lv_obj_add_state(item, LV_STATE_DISABLED);
            lv_obj_remove_flag(item, LV_OBJ_FLAG_CLICKABLE);
        }
    }

    lv_subject_set_int(&picker_state_subject_, filtered.empty() ? 1 : 2);
    spdlog::debug("[AmsEditOverlay] Rendered {} spool items (filter='{}')", filtered.size(), filter);
}

void AmsEditOverlay::handle_spool_selected(int spool_id) {
    spdlog::info("[AmsEditOverlay] Spool {} selected for slot {}", spool_id, slot_index_);

    // Look up SpoolInfo from cached spools
    for (const auto& spool : cached_spools_) {
        if (spool.id == spool_id) {
            // Auto-fill working_info_ from the selected spool
            working_info_.spoolman_id = spool.id;
            working_info_.spoolman_filament_id = spool.filament_id;
            working_info_.spoolman_vendor_id = spool.vendor_id;
            working_info_.color_name = spool.color_name;
            working_info_.multi_color_hexes = spool.multi_color_hexes;
            working_info_.material = spool.material;
            working_info_.brand = spool.vendor;
            working_info_.spool_name = spool.vendor + " " + spool.material;
            working_info_.remaining_weight_g = static_cast<float>(spool.remaining_weight_g);
            working_info_.total_weight_g = static_cast<float>(spool.initial_weight_g);
            working_info_.nozzle_temp_min = spool.nozzle_temp_min;
            working_info_.nozzle_temp_max = spool.nozzle_temp_max;
            working_info_.bed_temp = spool.bed_temp_recommended;

            // Parse color hex to RGB
            if (!spool.color_hex.empty()) {
                uint32_t rgb = 0;
                if (helix::parse_hex_color(spool.color_hex.c_str(), rgb)) {
                    working_info_.color_rgb = rgb;
                } else {
                    spdlog::warn("[AmsEditOverlay] Failed to parse color hex: {}", spool.color_hex);
                }
            }

            break;
        }
    }

    // Switch to form view and refresh UI
    switch_to_form();
    update_ui();
    update_sync_button_state();
    update_spoolman_button_state();
}

void AmsEditOverlay::handle_change_spool() {
    // Dual-mode control: with Spoolman configured, open the Spoolman spool list;
    // without it, open the offline branded-filament catalog picker.
    auto* subj = lv_xml_get_subject(nullptr, "printer_has_spoolman");
    bool has_spoolman = subj && lv_subject_get_int(subj) == 1;
    if (has_spoolman) {
        spdlog::debug("[AmsEditOverlay] Change spool requested - switching to Spoolman picker");
        switch_to_picker();
        return;
    }
    spdlog::debug("[AmsEditOverlay] Choose filament requested - opening branded catalog picker");
    open_branded_catalog_picker();
}

void AmsEditOverlay::open_branded_catalog_picker() {
    // Restrict the picker's Type dropdown to the backend's accepted materials (nullopt
    // = any). Seed the Type from the slot's current material so the user lands on a
    // relevant list.
    auto* backend = AmsState::instance().get_backend();
    auto allowed = backend ? backend->get_supported_materials() : std::nullopt;
    std::optional<std::string> seed = working_info_.material.empty()
                                          ? std::nullopt
                                          : std::optional<std::string>(working_info_.material);
    catalog_picker_.show(
        overlay_root_, seed, allowed,
        [this](const helix::printer::EffectiveFilament& ef) { apply_branded_pick(ef); });
}

void AmsEditOverlay::apply_branded_pick(const helix::printer::EffectiveFilament& ef) {
    // Map the branded catalog product onto the working slot. No color fields (the
    // offline catalog has no per-spool color) and no spoolman_id (this is the
    // Spoolman-absent path).
    working_info_.material = ef.type;
    working_info_.brand = ef.brand;
    working_info_.nozzle_temp_min = ef.nozzle_min;
    working_info_.nozzle_temp_max = ef.nozzle_max;
    working_info_.bed_temp = ef.bed_temp;
    // Gates new-spool creation (#1071) — same flag handle_material_changed sets on a
    // genuine user filament edit.
    filament_user_edited_ = true;
    switch_to_form();
    update_ui();
    update_temp_display();
    update_sync_button_state();
    update_spoolman_button_state();
    spdlog::info("[AmsEditOverlay] Slot {} assigned branded filament '{} {}' ({}-{}/{}°C)",
                 slot_index_, ef.brand, ef.type, ef.nozzle_min, ef.nozzle_max, ef.bed_temp);
}

void AmsEditOverlay::handle_picker_search(const char* text) {
    if (cached_spools_.empty()) {
        return;
    }
    render_spool_list(text ? text : "");
}

void AmsEditOverlay::handle_unlink() {
    spdlog::info("[AmsEditOverlay] Unlink requested for slot {}", slot_index_);
    working_info_.spoolman_id = 0;
    working_info_.spool_name.clear();
    update_ui();
    update_sync_button_state();
    update_spoolman_button_state();
}

void AmsEditOverlay::handle_spool_details() {
    if (working_info_.spoolman_id <= 0 || !api_) {
        return;
    }

    // Find the SpoolInfo in our cache, or build a minimal one
    SpoolInfo spool_info;
    bool found = false;
    for (const auto& spool : cached_spools_) {
        if (spool.id == working_info_.spoolman_id) {
            spool_info = spool;
            found = true;
            break;
        }
    }

    if (!found) {
        spool_info.id = working_info_.spoolman_id;
        spool_info.filament_id = working_info_.spoolman_filament_id;
        spool_info.vendor = working_info_.brand;
        spool_info.material = working_info_.material;
        spool_info.color_name = working_info_.color_name;
        spool_info.remaining_weight_g = working_info_.remaining_weight_g;
        spool_info.initial_weight_g = working_info_.total_weight_g;
        if (working_info_.color_rgb != 0) {
            char hex_buf[8];
            snprintf(hex_buf, sizeof(hex_buf), "#%06X", working_info_.color_rgb);
            spool_info.color_hex = hex_buf;
        }
    }

    // When spool details are saved, re-fetch the spool data to update our form
    auto token = lifetime_.token();
    spool_edit_modal_.set_completion_callback([this, token](bool saved) {
        if (!saved || token.expired() || !api_) {
            return;
        }
        // Re-fetch the spool to pick up any changes (color, weight, etc.)
        int spool_id = working_info_.spoolman_id;
        api_->spoolman().get_spoolman_spool(
            spool_id,
            [this, token, spool_id](const std::optional<SpoolInfo>& spool) {
                if (token.expired() || !spool.has_value()) {
                    return;
                }
                token.defer([this, spool = *spool]() {
                    // Update working_info_ from refreshed spool data
                    working_info_.brand = spool.vendor;
                    working_info_.material = spool.material;
                    working_info_.color_name = spool.color_name;
                    if (!spool.color_hex.empty()) {
                        uint32_t rgb = 0;
                        if (helix::parse_hex_color(spool.color_hex.c_str(), rgb)) {
                            working_info_.color_rgb = rgb;
                        }
                    }
                    if (spool.remaining_weight_g > 0 && spool.initial_weight_g > 0) {
                        working_info_.remaining_weight_g =
                            static_cast<float>(spool.remaining_weight_g);
                        working_info_.total_weight_g = static_cast<float>(spool.initial_weight_g);
                    }
                    update_ui();
                });
            },
            [spool_id](const MoonrakerError& err) {
                spdlog::warn("[AmsEditOverlay] Failed to refresh spool {} after edit: {}", spool_id,
                             err.message);
            });
    });

    lv_obj_t* parent = overlay_root_ ? lv_obj_get_parent(overlay_root_) : lv_screen_active();
    spool_edit_modal_.show_for_spool(parent, spool_info, api_);
}

void AmsEditOverlay::handle_scan_qr() {
    spdlog::info("[AmsEditOverlay] Scan QR requested for slot {}", slot_index_);

    int slot = slot_index_;
    auto* api = api_ ? api_ : get_moonraker_api();

    // Phase-1 parity with the modal: dismiss the editor silently (no
    // completion) and let the QR result write directly to the backend.
    // Phase 8 upgrades this to scanner-over-editor with live repopulation.
    completion_fired_ = true; // suppress the close-callback safety net
    NavigationManager::instance().go_back();

    auto& scanner = helix::ui::get_qr_scanner_overlay();
    scanner.show(lv_screen_active(), slot, [slot, api](const SpoolInfo& spool) {
        // QR scan result: apply spool data directly.
        SlotInfo info;
        info.slot_index = slot;
        info.global_index = slot;
        info.spoolman_id = spool.id;
        info.spoolman_filament_id = spool.filament_id;
        info.spoolman_vendor_id = spool.vendor_id;
        info.color_name = spool.color_name;
        info.material = spool.material;
        info.brand = spool.vendor;
        info.spool_name = spool.vendor + " " + spool.material;
        info.remaining_weight_g = static_cast<float>(spool.remaining_weight_g);
        info.total_weight_g = static_cast<float>(spool.initial_weight_g);
        info.nozzle_temp_min = spool.nozzle_temp_min;
        info.nozzle_temp_max = spool.nozzle_temp_max;
        info.bed_temp = spool.bed_temp_recommended;
        if (!spool.color_hex.empty()) {
            uint32_t rgb = 0;
            if (helix::parse_hex_color(spool.color_hex.c_str(), rgb)) {
                info.color_rgb = rgb;
            }
        }

        if (slot == -2) {
            AmsState::instance().set_external_spool_info(info);
            spdlog::info("[AmsEditOverlay] QR scan auto-saved spool #{} to external spool",
                         spool.id);
        } else {
            AmsBackend* be = AmsState::instance().get_backend();
            if (be) {
                AmsError err = be->set_slot_info(slot, info);
                if (err.success()) {
                    AmsState::instance().sync_from_backend();
                    spdlog::info("[AmsEditOverlay] QR scan auto-saved spool #{} to slot {}",
                                 spool.id, slot);
                } else {
                    spdlog::error("[AmsEditOverlay] QR scan save failed: {}", err.user_msg);
                }
            }
        }

        if (api && spool.id > 0) {
            sync_active_spool(api, spool.id);
        }

        NOTIFY_INFO("{} {} assigned via QR scan", spool.vendor, spool.material);
    });
}

#if HELIX_HAS_LABEL_PRINTER
void AmsEditOverlay::handle_print_label() {
    auto& settings = helix::LabelPrinterSettingsManager::instance();

    if (!settings.is_configured()) {
        ToastManager::instance().show(ToastSeverity::INFO,
                                      lv_tr("Set up your label printer in Settings"), 3000);
        return;
    }

    // Build SpoolInfo from AMS slot data
    SpoolInfo spool_info;
    bool found = false;
    for (const auto& spool : cached_spools_) {
        if (spool.id == working_info_.spoolman_id) {
            spool_info = spool;
            found = true;
            break;
        }
    }

    if (!found) {
        spool_info.id = working_info_.spoolman_id;
        spool_info.vendor = working_info_.brand;
        spool_info.material = working_info_.material;
        spool_info.color_name = working_info_.color_name;
        spool_info.remaining_weight_g = working_info_.remaining_weight_g;
        spool_info.initial_weight_g = working_info_.total_weight_g;
    }

    // Use the standard print flow (handles all printer types including IPP modal)
    auto print_cb = [](bool success, const std::string& error) {
        if (success) {
            ToastManager::instance().show(ToastSeverity::SUCCESS, lv_tr("Label printed"), 2000);
        } else {
            spdlog::error("[AmsEditOverlay] Print failed: {}", error);
            ToastManager::instance().show(ToastSeverity::ERROR,
                                          helix::friendly_label_printer_error(error).c_str(), 5000);
        }
    };

    if (!helix::maybe_show_ipp_print_modal(spool_info, print_cb)) {
        ToastManager::instance().show(ToastSeverity::INFO, lv_tr("Printing label..."), 2000);
        helix::print_spool_label(spool_info, print_cb);
    }
}
#endif

void AmsEditOverlay::update_spoolman_button_state() {
    if (!overlay_root_) {
        return;
    }

    // Read Spoolman availability synchronously — the XML binding fires asynchronously,
    // so a fresh read here closes the race window when the modal opens before Spoolman
    // detection completes (#311).
    auto* spoolman_subj = lv_xml_get_subject(nullptr, "printer_has_spoolman");
    bool has_spoolman = spoolman_subj && lv_subject_get_int(spoolman_subj) == 1;

    // The filament-actions row is ALWAYS shown now: btn_change_spool is a dual-mode
    // control ("Choose Spool" with Spoolman, "Choose Filament" without).
    lv_obj_t* actions_container = find_widget("spoolman_actions");
    if (actions_container) {
        lv_obj_remove_flag(actions_container, LV_OBJ_FLAG_HIDDEN);
        // Retry vendor fetch if it was skipped due to the race (#311)
        if (has_spoolman && !vendors_loaded_) {
            fetch_vendors_from_spoolman();
        }
    }

    // Dual-mode label on the shared button.
    lv_obj_t* btn_change = find_widget("btn_change_spool");
    if (btn_change) {
        ui_button_set_text(btn_change,
                           has_spoolman ? lv_tr("Choose Spool") : lv_tr("Choose Filament"));
    }

    lv_obj_t* btn_actions = find_widget("btn_spool_actions");
    lv_obj_t* btn_scan_qr = find_widget("btn_scan_qr_code");

    if (!has_spoolman) {
        // Spoolman absent: only the dual-mode Choose Filament button is meaningful —
        // QR scan and the spool-actions split button have no offline analogue.
        if (btn_scan_qr) {
            lv_obj_add_flag(btn_scan_qr, LV_OBJ_FLAG_HIDDEN);
        }
        if (btn_actions) {
            lv_obj_add_flag(btn_actions, LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }

    if (working_info_.spoolman_id > 0) {
        // Linked: show split button with all spool actions
        if (btn_scan_qr) {
            lv_obj_add_flag(btn_scan_qr, LV_OBJ_FLAG_HIDDEN);
        }
        if (btn_actions) {
            lv_obj_remove_flag(btn_actions, LV_OBJ_FLAG_HIDDEN);
            ui_split_button_set_text(btn_actions, lv_tr("Scan QR Code"));

            // Build options list with translated strings
            std::string options = std::string(lv_tr("Scan QR Code")) + "\n" +
                                  lv_tr("Spool Details") + "\n" + lv_tr("Unlink");
#if HELIX_HAS_LABEL_PRINTER
            if (helix::LabelPrinterSettingsManager::instance().is_configured()) {
                options += std::string("\n") + lv_tr("Print Label");
            }
#endif
            ui_split_button_set_options(btn_actions, options.c_str());
        }
    } else {
        // Not linked: show standalone "Scan QR Code" button, hide split button
        if (btn_scan_qr) {
            lv_obj_remove_flag(btn_scan_qr, LV_OBJ_FLAG_HIDDEN);
        }
        if (btn_actions) {
            lv_obj_add_flag(btn_actions, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

// ============================================================================
// Internal Methods
// ============================================================================

void AmsEditOverlay::update_ui() {
    if (!overlay_root_) {
        return;
    }

    // Update slot indicator via subject (used in header)
    if (slot_index_ < 0) {
        snprintf(slot_indicator_buf_, sizeof(slot_indicator_buf_), "%s",
                 lv_tr("External Filament"));
    } else {
        snprintf(slot_indicator_buf_, sizeof(slot_indicator_buf_), lv_tr("Slot %d Filament"),
                 slot_index_ + 1);
    }
    lv_subject_copy_string(&slot_indicator_subject_, slot_indicator_buf_);

    // Update Spoolman ID label in header
    lv_obj_t* spoolman_label = find_widget("spoolman_id_label");
    if (spoolman_label) {
        if (working_info_.spoolman_id > 0) {
            char spoolman_text[32];
            snprintf(spoolman_text, sizeof(spoolman_text), "(Spoolman #%d)",
                     working_info_.spoolman_id);
            lv_label_set_text(spoolman_label, spoolman_text);
            lv_obj_remove_flag(spoolman_label, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(spoolman_label, LV_OBJ_FLAG_HIDDEN);
        }
    }

    // Build material options from filament database (if not already built).
    // When the active backend advertises a firmware whitelist (e.g., AD5X IFS
    // accepts only PLA / PLA-CF / SILK / TPU / ABS / PETG / PETG-CF), restrict
    // the dropdown to that set. Whitelist entries not present in the shared
    // filament DB (e.g., "SILK") are still appended so users aren't silently
    // locked out of a firmware-supported option.
    if (material_list_.empty()) {
        auto* backend = AmsState::instance().get_backend();
        auto supported = backend ? backend->get_supported_materials() : std::nullopt;
        const bool filtered = supported.has_value() && !supported->empty();

        auto to_lower = [](std::string s) {
            std::transform(s.begin(), s.end(), s.begin(),
                           [](unsigned char c) { return std::tolower(c); });
            return s;
        };

        auto is_supported = [&](const char* name) {
            if (!filtered) {
                return true;
            }
            std::string n_lc = to_lower(name);
            for (const auto& s : *supported) {
                if (to_lower(s) == n_lc) {
                    return true;
                }
            }
            return false;
        };

        auto all_materials = filament::get_all_material_names();
        material_list_.reserve(filtered ? supported->size() : all_materials.size());
        for (const char* mat : all_materials) {
            if (!is_supported(mat)) {
                continue;
            }
            if (!material_options_.empty()) {
                material_options_ += '\n';
            }
            material_options_ += mat;
            material_list_.push_back(mat);
        }

        // Ensure every whitelist entry appears even if the shared DB doesn't
        // have a case-matching name for it (e.g., AD5X's "SILK" vs DB's "Silk PLA").
        if (filtered) {
            for (const auto& s : *supported) {
                bool found = false;
                for (const auto& existing : material_list_) {
                    if (existing == s) {
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    if (!material_options_.empty()) {
                        material_options_ += '\n';
                    }
                    material_options_ += s;
                    material_list_.push_back(s);
                }
            }
        }

        spdlog::debug("[AmsEditOverlay] Built material list with {} materials (filtered={})",
                      material_list_.size(), filtered);
    }

    // Set up vendor dropdown (use cached vendors from Spoolman, or fallback)
    lv_obj_t* vendor_dropdown = find_widget("vendor_dropdown");
    if (vendor_dropdown) {
        if (!vendor_options_.empty()) {
            // Use vendors from Spoolman
            lv_dropdown_set_options(vendor_dropdown, vendor_options_.c_str());
        } else {
            // Fallback: static vendor list while Spoolman query is pending
            static const char* fallback_vendors =
                "Generic\nPolymaker\nBambu\neSUN\nOverture\nPrusa\nHatchbox";
            lv_dropdown_set_options(vendor_dropdown, fallback_vendors);

            // Build fallback vendor_list_ for index lookup (id=0 for static entries)
            if (vendor_list_.empty()) {
                for (const auto& name :
                     {"Generic", "Polymaker", "Bambu", "eSUN", "Overture", "Prusa", "Hatchbox"}) {
                    VendorInfo vi;
                    vi.name = name;
                    vendor_list_.push_back(std::move(vi));
                }
            }
        }

        // Set initial selection based on working_info_.brand, falling back to
        // "Generic" if the brand is empty or unknown. Normalize both working
        // and original to "Generic" when empty so the dropdown display matches
        // state and unchanged saves don't flip-flop the field.
        int vendor_idx = -1;
        int generic_idx = 0;
        for (size_t i = 0; i < vendor_list_.size(); i++) {
            if (vendor_list_[i].name == "Generic") {
                generic_idx = static_cast<int>(i);
            }
            if (!working_info_.brand.empty() && working_info_.brand == vendor_list_[i].name) {
                vendor_idx = static_cast<int>(i);
                break;
            }
        }
        if (vendor_idx < 0) {
            vendor_idx = generic_idx;
            if (working_info_.brand.empty())
                working_info_.brand = "Generic";
            if (original_info_.brand.empty())
                original_info_.brand = "Generic";
        }
        lv_dropdown_set_selected(vendor_dropdown, vendor_idx);
    }

    // Set up material dropdown from filament database
    lv_obj_t* material_dropdown = find_widget("material_dropdown");
    if (material_dropdown) {
        lv_dropdown_set_options(material_dropdown, material_options_.c_str());

        // Set initial selection based on working_info_.material
        int material_idx = 0; // Default to first (PLA)
        for (size_t i = 0; i < material_list_.size(); i++) {
            if (working_info_.material == material_list_[i]) {
                material_idx = static_cast<int>(i);
                break;
            }
        }
        lv_dropdown_set_selected(material_dropdown, material_idx);

        // Sync working_info_ when dropdown defaults to first entry
        if (working_info_.material.empty() && !material_list_.empty()) {
            working_info_.material = material_list_[material_idx];
        }
    }

    // Update color swatch
    lv_obj_t* color_swatch = find_widget("color_swatch");
    if (color_swatch) {
        helix::ui::apply_swatch_color(color_swatch, working_info_.color_rgb,
                                      working_info_.multi_color_hexes);
    }

    // Update color name label via subject
    if (!working_info_.color_name.empty()) {
        snprintf(color_name_buf_, sizeof(color_name_buf_), "%s", working_info_.color_name.c_str());
    } else {
        color_name_buf_[0] = '\0';
    }
    lv_subject_copy_string(&color_name_subject_, color_name_buf_);

    // Update filament/spool product-line label. Hidden when empty so it
    // doesn't leave a blank row on backends/slots that don't populate it.
    lv_obj_t* spool_name_label = find_widget("spool_name_label");
    if (!working_info_.spool_name.empty()) {
        snprintf(spool_name_buf_, sizeof(spool_name_buf_), "%s", working_info_.spool_name.c_str());
        if (spool_name_label)
            lv_obj_remove_flag(spool_name_label, LV_OBJ_FLAG_HIDDEN);
    } else {
        spool_name_buf_[0] = '\0';
        if (spool_name_label)
            lv_obj_add_flag(spool_name_label, LV_OBJ_FLAG_HIDDEN);
    }
    lv_subject_copy_string(&spool_name_subject_, spool_name_buf_);

    // Update remaining slider and label
    // Use synthetic 1000g total if no weight data (manual spool without Spoolman)
    if (working_info_.total_weight_g <= 0) {
        working_info_.total_weight_g = 1000.0f;
        working_info_.remaining_weight_g =
            (working_info_.remaining_weight_g > 0) ? working_info_.remaining_weight_g : 1000.0f;
    }
    int remaining_pct =
        static_cast<int>(100.0f * working_info_.remaining_weight_g / working_info_.total_weight_g);
    remaining_pct = std::max(0, std::min(100, remaining_pct));

    lv_obj_t* remaining_slider = find_widget("remaining_slider");
    if (remaining_slider) {
        lv_slider_set_value(remaining_slider, remaining_pct, LV_ANIM_OFF);
    }

    // Show/hide weight input based on whether we have real weight data
    lv_obj_t* weight_input = find_widget("remaining_weight_input");
    if (weight_input) {
        if (original_info_.total_weight_g > 0) {
            lv_obj_remove_flag(weight_input, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(weight_input, LV_OBJ_FLAG_HIDDEN);
        }
    }

    // Update remaining label: "/ 1000g (75%)" or "75%" if no weight data
    format_remaining_label(remaining_pct);

    // Update progress bar fill width (shown in view mode)
    // Use percentage width to avoid layout timing issues
    lv_obj_t* progress_fill = find_widget("remaining_progress_fill");
    if (progress_fill) {
        lv_obj_set_width(progress_fill, lv_pct(remaining_pct));
    }

    // Update temperature display based on material
    update_temp_display();

    // Populate tool dropdown with available tools
    // Show tool remap dropdown only for backends that support it
    lv_obj_t* tool_remap_row = find_widget("tool_remap_row");
    lv_obj_t* tool_dropdown = find_widget("tool_dropdown");
    auto* backend = AmsState::instance().get_backend();
    bool can_remap = backend && backend->get_system_info().supports_tool_mapping;

    if (tool_remap_row) {
        if (can_remap) {
            lv_obj_remove_flag(tool_remap_row, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(tool_remap_row, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (tool_dropdown && can_remap) {
        int tool_count = static_cast<int>(backend->get_system_info().tool_to_slot_map.size());
        std::string tool_options;
        for (int i = 0; i < tool_count; i++) {
            if (!tool_options.empty()) {
                tool_options += '\n';
            }
            tool_options += "T" + std::to_string(i);
        }
        lv_dropdown_set_options(tool_dropdown, tool_options.c_str());

        // Set initial selection: T0 → index 0, T1 → index 1, etc.
        // Default to T0 if mapped_tool is -1 (shouldn't happen for remappable backends)
        int tool_idx = std::max(0, working_info_.mapped_tool);
        tool_idx = std::min(tool_idx, tool_count - 1);
        lv_dropdown_set_selected(tool_dropdown, tool_idx);
    }
}

void AmsEditOverlay::update_temp_display() {
    if (!overlay_root_) {
        return;
    }

    // Get temperature range from slot info (populated from Spoolman or material defaults)
    int nozzle_min = working_info_.nozzle_temp_min;
    int nozzle_max = working_info_.nozzle_temp_max;
    int bed_temp = working_info_.bed_temp;

    // Fall back to material-based defaults from filament database if not set
    if (nozzle_min == 0 && nozzle_max == 0 && !working_info_.material.empty()) {
        auto mat_info = filament::find_material(working_info_.material);
        if (mat_info) {
            nozzle_min = mat_info->nozzle_min;
            nozzle_max = mat_info->nozzle_max;
            bed_temp = mat_info->bed_temp;
            spdlog::debug("[AmsEditOverlay] Using filament database temps for {}: {}-{}°C nozzle, "
                          "{}°C bed",
                          working_info_.material, nozzle_min, nozzle_max, bed_temp);
        } else {
            // Fallback to PLA defaults for unknown materials
            auto pla_info = filament::find_material("PLA");
            if (pla_info) {
                nozzle_min = pla_info->nozzle_min;
                nozzle_max = pla_info->nozzle_max;
                bed_temp = pla_info->bed_temp;
            } else {
                // Ultimate fallback (should never happen - PLA is in database)
                nozzle_min = 200;
                nozzle_max = 230;
                bed_temp = 60;
            }
            spdlog::debug("[AmsEditOverlay] Material '{}' not in database, using PLA defaults",
                          working_info_.material);
        }
    }

    // Update nozzle temp label via subject
    snprintf(temp_nozzle_buf_, sizeof(temp_nozzle_buf_), "%d-%d°C", nozzle_min, nozzle_max);
    lv_subject_copy_string(&temp_nozzle_subject_, temp_nozzle_buf_);

    // Update bed temp label via subject
    snprintf(temp_bed_buf_, sizeof(temp_bed_buf_), "%d°C", bed_temp);
    lv_subject_copy_string(&temp_bed_subject_, temp_bed_buf_);
}

void AmsEditOverlay::format_remaining_label(int pct) {
    bool has_weight = original_info_.total_weight_g > 0;

    if (has_weight) {
        // Weight input field shows the remaining grams — label shows "/ Xg (Y%)"
        int total_g = static_cast<int>(working_info_.total_weight_g);
        snprintf(remaining_pct_buf_, sizeof(remaining_pct_buf_), "/ %dg (%d%%)", total_g, pct);
    } else {
        helix::format::format_percent(pct, remaining_pct_buf_, sizeof(remaining_pct_buf_));
    }
    lv_subject_copy_string(&remaining_pct_subject_, remaining_pct_buf_);

    // Sync the weight input field if it exists and we have weight data
    if (has_weight) {
        lv_obj_t* weight_input = find_widget("remaining_weight_input");
        if (weight_input) {
            if (working_info_.remaining_weight_g < 0.0f) {
                // Sentinel: remaining weight unknown. Show blank rather than "-1"
                // (which is an internal sentinel, not a user-facing value).
                lv_textarea_set_text(weight_input, "");
            } else {
                char buf[8];
                snprintf(buf, sizeof(buf), "%d",
                         static_cast<int>(working_info_.remaining_weight_g));
                lv_textarea_set_text(weight_input, buf);
            }
        }
    }
}

bool AmsEditOverlay::is_dirty() const {
    // Compare relevant fields that can be edited
    return working_info_.color_rgb != original_info_.color_rgb ||
           working_info_.material != original_info_.material ||
           working_info_.brand != original_info_.brand ||
           working_info_.spoolman_id != original_info_.spoolman_id ||
           working_info_.mapped_tool != original_info_.mapped_tool ||
           std::abs(working_info_.remaining_weight_g - original_info_.remaining_weight_g) > 0.1f;
}

void AmsEditOverlay::update_sync_button_state() {
    if (!subjects_initialized_) {
        return;
    }
    // Header Save action is disabled until something is dirty (replaces the
    // modal's Save/Close text morph).
    lv_subject_set_int(&save_disabled_subject_, is_dirty() ? 0 : 1);
}

void AmsEditOverlay::show_color_picker() {
    if (!parent_screen_) {
        spdlog::warn("[AmsEditOverlay] No parent for color picker");
        return;
    }

    // Create picker on first use (lazy initialization)
    if (!color_picker_) {
        color_picker_ = std::make_unique<ColorPicker>();
    }

    // Set callback to update edit modal when color is selected
    color_picker_->set_color_callback([this](uint32_t color_rgb, const std::string& color_name) {
        // Update the working slot info with selected color
        working_info_.color_rgb = color_rgb;
        working_info_.color_name = color_name;
        // A hand-picked single color replaces any inherited multi-color swatch.
        working_info_.multi_color_hexes.clear();
        filament_user_edited_ = true; // genuine user edit gates new-spool create (#1071)

        // Update the edit modal's color swatch to show new selection
        if (overlay_root_) {
            lv_obj_t* swatch = find_widget("color_swatch");
            if (swatch) {
                helix::ui::apply_swatch_color(swatch, color_rgb, working_info_.multi_color_hexes);
            }

            // Update color name label via subject
            snprintf(color_name_buf_, sizeof(color_name_buf_), "%s", color_name.c_str());
            lv_subject_copy_string(&color_name_subject_, color_name_buf_);

            update_sync_button_state();
        }
    });

    // Show with current edit color
    color_picker_->show_with_color(parent_screen_, working_info_.color_rgb);
}

// ============================================================================
// Save Orchestration
// ============================================================================

void AmsEditOverlay::fire_completion(bool saved) {
    if (completion_fired_) {
        return; // Save/back already completed; safety-net close callback is a no-op
    }
    completion_fired_ = true;
    spdlog::info("[AmsEditOverlay] fire_completion saved={} slot={} spoolman_id={} material={}",
                 saved, slot_index_, working_info_.spoolman_id, working_info_.material);
    if (completion_callback_) {
        EditResult result;
        result.saved = saved;
        result.slot_index = slot_index_;
        result.slot_info = working_info_;
        completion_callback_(result);
    }
}

void AmsEditOverlay::close_editor(bool saved) {
    fire_completion(saved);
    NavigationManager::instance().go_back();
}

// ============================================================================
// Event Handlers
// ============================================================================

void AmsEditOverlay::handle_back() {
    int view = lv_subject_get_int(&view_mode_subject_);
    switch (view) {
    case kViewSpoolPicker:
        // Back from the picker returns to the overview (old "manual entry" path)
        switch_to_form();
        break;
    case kViewOverview:
    default:
        // Back on the overview = Cancel: discard changes, close (spec §13.3)
        spdlog::debug("[AmsEditOverlay] Back on overview - cancelling");
        working_info_ = original_info_;
        close_editor(false);
        break;
    }
}

void AmsEditOverlay::handle_vendor_changed(int index) {
    if (index >= 0 && index < static_cast<int>(vendor_list_.size())) {
        working_info_.brand = vendor_list_[index].name;
        working_info_.spoolman_vendor_id = vendor_list_[index].id;
        filament_user_edited_ = true; // genuine user edit gates new-spool create (#1071)
        spdlog::debug("[AmsEditOverlay] Vendor changed to: {} (vendor_id={})", working_info_.brand,
                      working_info_.spoolman_vendor_id);
        update_sync_button_state();
    }
}

void AmsEditOverlay::handle_material_changed(int index) {
    if (index >= 0 && index < static_cast<int>(material_list_.size())) {
        working_info_.material = material_list_[index];
        filament_user_edited_ = true; // genuine user edit gates new-spool create (#1071)
        spdlog::debug("[AmsEditOverlay] Material changed to: {}", working_info_.material);

        // Clear existing temp values so update_temp_display uses material-based defaults
        working_info_.nozzle_temp_min = 0;
        working_info_.nozzle_temp_max = 0;
        working_info_.bed_temp = 0;

        // Update temperature display based on new material
        update_temp_display();
        update_sync_button_state();
    }
}

void AmsEditOverlay::handle_color_clicked() {
    spdlog::info("[AmsEditOverlay] Opening color picker");
    show_color_picker();
}

void AmsEditOverlay::handle_remaining_changed(int percent) {
    if (!overlay_root_) {
        return;
    }

    // Update the remaining label via format_remaining_label
    format_remaining_label(percent);

    // Update slot info remaining weight based on percentage
    // Use synthetic 1000g total if no weight data (manual spool without Spoolman)
    if (working_info_.total_weight_g <= 0) {
        working_info_.total_weight_g = 1000.0f;
    }
    working_info_.remaining_weight_g =
        working_info_.total_weight_g * static_cast<float>(percent) / 100.0f;

    update_sync_button_state();
    spdlog::trace("[AmsEditOverlay] Remaining changed to {}%", percent);
}

void AmsEditOverlay::handle_weight_input_changed() {
    if (!overlay_root_) {
        return;
    }

    lv_obj_t* weight_input = find_widget("remaining_weight_input");
    if (!weight_input) {
        return;
    }

    const char* text = lv_textarea_get_text(weight_input);
    if (!text || text[0] == '\0') {
        // Blank field: treat as "unknown weight" sentinel so save preserves -1
        // rather than coercing blank to 0. Reset the label to a plain percentage
        // display and leave the slider where it is.
        working_info_.remaining_weight_g = -1.0f;
        int total = static_cast<int>(working_info_.total_weight_g);
        if (total > 0) {
            snprintf(remaining_pct_buf_, sizeof(remaining_pct_buf_), "/ %dg", total);
        } else {
            remaining_pct_buf_[0] = '\0';
        }
        lv_subject_copy_string(&remaining_pct_subject_, remaining_pct_buf_);
        update_sync_button_state();
        return;
    }

    int grams = std::atoi(text);
    if (grams < 0) {
        grams = 0;
    }
    int total_g = static_cast<int>(working_info_.total_weight_g);
    if (total_g > 0 && grams > total_g) {
        grams = total_g;
    }

    working_info_.remaining_weight_g = static_cast<float>(grams);

    // Recalculate percentage and update slider + label
    int pct = (working_info_.total_weight_g > 0)
                  ? static_cast<int>(100.0f * grams / working_info_.total_weight_g)
                  : 0;
    pct = std::max(0, std::min(100, pct));

    lv_obj_t* slider = find_widget("remaining_slider");
    if (slider) {
        lv_slider_set_value(slider, pct, LV_ANIM_OFF);
    }

    lv_obj_t* progress_fill = find_widget("remaining_progress_fill");
    if (progress_fill) {
        lv_obj_set_width(progress_fill, lv_pct(pct));
    }

    // Update the info label ("/ 1000g (75%)") without re-setting the input text
    int total = static_cast<int>(working_info_.total_weight_g);
    snprintf(remaining_pct_buf_, sizeof(remaining_pct_buf_), "/ %dg (%d%%)", total, pct);
    lv_subject_copy_string(&remaining_pct_subject_, remaining_pct_buf_);

    update_sync_button_state();
    spdlog::trace("[AmsEditOverlay] Weight input changed to {}g ({}%)", grams, pct);
}

void AmsEditOverlay::handle_remaining_edit() {
    if (!overlay_root_) {
        return;
    }

    // Store current remaining percentage before entering edit mode
    lv_obj_t* slider = find_widget("remaining_slider");
    if (slider) {
        remaining_pre_edit_pct_ = lv_slider_get_value(slider);
    }

    // Enter edit mode - subject binding will show slider/accept/cancel, hide progress/edit button
    lv_subject_set_int(&remaining_mode_subject_, 1);
    spdlog::debug("[AmsEditOverlay] Entered remaining edit mode (was {}%)", remaining_pre_edit_pct_);
}

void AmsEditOverlay::handle_remaining_accept() {
    if (!overlay_root_) {
        return;
    }

    // Get the current slider value
    lv_obj_t* slider = find_widget("remaining_slider");
    int new_pct = slider ? lv_slider_get_value(slider) : remaining_pre_edit_pct_;

    // Update the progress bar fill to match
    lv_obj_t* progress_fill = find_widget("remaining_progress_fill");
    if (progress_fill) {
        lv_obj_set_width(progress_fill, lv_pct(new_pct));
    }

    // Exit edit mode - subject binding will show progress/edit button, hide slider/accept/cancel
    lv_subject_set_int(&remaining_mode_subject_, 0);
    spdlog::debug("[AmsEditOverlay] Accepted remaining edit: {}%", new_pct);
}

void AmsEditOverlay::handle_remaining_cancel() {
    if (!overlay_root_) {
        return;
    }

    // Revert slider to pre-edit value
    lv_obj_t* slider = find_widget("remaining_slider");
    if (slider) {
        lv_slider_set_value(slider, remaining_pre_edit_pct_, LV_ANIM_OFF);
    }

    // Revert the remaining weight in working_info_ before updating label
    if (working_info_.total_weight_g > 0) {
        working_info_.remaining_weight_g =
            working_info_.total_weight_g * static_cast<float>(remaining_pre_edit_pct_) / 100.0f;
    }

    // Revert the remaining label via subject
    format_remaining_label(remaining_pre_edit_pct_);

    // Exit edit mode
    lv_subject_set_int(&remaining_mode_subject_, 0);
    update_sync_button_state();
    spdlog::debug("[AmsEditOverlay] Cancelled remaining edit (reverted to {}%)",
                  remaining_pre_edit_pct_);
}

void AmsEditOverlay::handle_tool_changed(int index) {
    // No "None" — index 0 = T0, index 1 = T1, etc.
    working_info_.mapped_tool = index;
    working_info_.tool_mapping_override = (index != original_info_.mapped_tool);
    spdlog::debug("[AmsEditOverlay] Tool changed to: T{} (override={})", index,
                  working_info_.tool_mapping_override);
    update_sync_button_state();
}

bool AmsEditOverlay::should_create_new_spool(const SlotInfo& working_info,
                                           bool filament_user_edited) {
    // Unlinked + a genuine user edit + complete metadata. The user-edit term is
    // the #1071 Symptom C fix: an unedited open auto-defaults brand="Generic",
    // which alone satisfies is_filament_complete() and would otherwise spawn a
    // phantom spool on save.
    return working_info.spoolman_id == 0 && filament_user_edited &&
           helix::SpoolmanSlotSaver::is_filament_complete(working_info);
}

void AmsEditOverlay::handle_save() {
    spdlog::info("[AmsEditOverlay] Saving edits for slot {}", slot_index_);

    // Resolve API: prefer stored api_, fall back to global
    if (!api_) {
        api_ = get_moonraker_api();
    }

    // Sync active spool with Moonraker on every save — covers assignment changes
    // AND re-saves of an already-linked slot whose state Moonraker has lost
    // (e.g. after a Moonraker restart, a Spoolman outage, or an earlier create
    // path that didn't propagate the new ID). The underlying POST is idempotent
    // when the ID is unchanged. Fire-and-forget: local save proceeds regardless
    // of server response. Skipped for the new-spool-on-save path (working.id=0
    // until creation); that case re-syncs from the create callback below.
    if (api_ && working_info_.spoolman_id > 0) {
        sync_active_spool(api_, working_info_.spoolman_id);
    } else if (api_ && original_info_.spoolman_id > 0 && working_info_.spoolman_id == 0) {
        // Unassignment: propagate 0 so Moonraker clears its active spool.
        sync_active_spool(api_, 0);
    }

    // Delegate to SpoolmanSlotSaver whenever either:
    //   - There's a linked spool with pending edits (update filament/weight), OR
    //   - There's no linked spool but the user entered complete manual metadata
    //     (brand + material + non-default color) — create a new Spoolman spool.
    // SpoolmanSlotSaver::save() contains its own internal gates, so this is just
    // the outer guard that decides whether to invoke the async path at all.
    // Skip entirely if Spoolman isn't available on this printer — otherwise every
    // save would emit a "server.spoolman.proxy Method not found" error toast.
    if (api_ && get_printer_state().is_spoolman_available()) {
        const bool has_linked_spool = working_info_.spoolman_id > 0;
        auto changes = helix::SpoolmanSlotSaver::detect_changes(original_info_, working_info_);
        // Require an explicit user filament edit before creating a new Spoolman
        // spool. update_vendor_dropdown auto-defaults brand="Generic" on an
        // unedited open, which alone makes is_filament_complete() true; without
        // this gate an open+save with no edits spawns a phantom "Generic" spool
        // (#1071 Symptom C). Editing fields routes through handle_vendor_changed /
        // handle_material_changed / the color picker, which set the flag; picking
        // an existing spool (handle_spool_selected) does NOT — it takes the
        // has_linked_spool branch instead.
        const bool can_create_new = should_create_new_spool(working_info_, filament_user_edited_);

        // #1071 Symptom B: updating a LINKED spool whose filament identity changed
        // (different material, or color past the match tolerance) probably
        // clobbers a DIFFERENT physical spool's Spoolman definition. Confirm
        // first; "Update anyway" writes Spoolman, "Cancel" keeps the linked spool
        // untouched and saves the slot locally, tapping outside aborts back to the
        // editor. Scoped to AD5X IFS — the only backend where #1071's
        // keep-the-link-across-eject policy makes a silent identity swap likely;
        // other backends keep the prior straight-to-update behavior.
        auto* active_backend = AmsState::instance().get_backend();
        const bool is_ad5x = active_backend && active_backend->get_type() == AmsType::AD5X_IFS;
        if (has_linked_spool && changes.any() && is_ad5x &&
            is_material_identity_change(original_info_, working_info_)) {
            prompt_identity_change_then_save();
            return;
        }

        if ((has_linked_spool && changes.any()) || can_create_new) {
            do_spoolman_save();
            return; // Async path - fire_completion called from callback
        }
    }

    // No Spoolman changes (or no Spoolman) - save locally immediately
    close_editor(true);
}

void AmsEditOverlay::do_spoolman_save() {
    auto token = lifetime_.token();
    auto saver = std::make_shared<helix::SpoolmanSlotSaver>(api_);
    saver->save(
        original_info_, working_info_, [this, token, saver](const helix::SaveResult& result) {
            if (token.expired()) {
                return;
            }
            // Spoolman callback arrives on a background thread — defer
            // to the UI thread before touching LVGL subjects/widgets.
            token.defer([this, result]() {
                if (!result.success) {
                    // Local save still proceeds; only the Spoolman mirror failed.
                    spdlog::error("[AmsEditOverlay] Spoolman save failed, saving locally");
                    ToastManager::instance().show(ToastSeverity::ERROR,
                                                  lv_tr("Couldn't update Spoolman — saved locally"),
                                                  3000);
                } else if (result.created_new_spool || result.repointed_filament) {
                    // Persist new Spoolman IDs into working_info_ so the
                    // completion callback's backend->set_slot_info() writes
                    // the link back to the slot. Without this, a subsequent
                    // edit would not know the spool exists and would create
                    // a duplicate.
                    if (result.new_spool_id != 0) {
                        working_info_.spoolman_id = result.new_spool_id;
                    }
                    if (result.new_filament_id != 0) {
                        working_info_.spoolman_filament_id = result.new_filament_id;
                    }
                    if (result.new_vendor_id != 0) {
                        working_info_.spoolman_vendor_id = result.new_vendor_id;
                    }
                    // The early sync_active_spool() above was skipped because
                    // spoolman_id was 0 on both sides (creation hadn't happened
                    // yet). Notify Moonraker now so Mainsail/Fluidd show the
                    // new spool as active and filament tracking starts.
                    if (result.created_new_spool && result.new_spool_id != 0 && api_) {
                        sync_active_spool(api_, result.new_spool_id);
                    }
                    if (result.created_new_spool) {
                        ToastManager::instance().show(ToastSeverity::INFO,
                                                      lv_tr("Added to Spoolman"), 2500);
                    }
                    // Repoint is silent — IDs change but no toast.
                }
                close_editor(true);
            });
        });
}

bool AmsEditOverlay::is_material_identity_change(const SlotInfo& original, const SlotInfo& edited) {
    if (!helix::FilamentMapper::materials_match(original.material, edited.material)) {
        return true;
    }
    return !helix::FilamentMapper::colors_match(original.color_rgb, edited.color_rgb);
}

void AmsEditOverlay::prompt_identity_change_then_save() {
    // Dismiss-safe binary confirmation. "Update anyway" -> update the linked
    // spool; "Cancel" -> keep the linked Spoolman spool untouched and save the
    // slot locally (re-point later via "Choose Spool"); tapping outside aborts
    // the confirmation and returns to the editor (no save, link untouched). No
    // path writes a materially-different spool without an explicit confirm.
    lv_obj_t* dlg = modal_show_confirmation(
        lv_tr("Different filament?"),
        lv_tr("This looks like a different spool than the one linked. Update the linked Spoolman "
              "spool anyway? Cancel keeps it unchanged."),
        ModalSeverity::Warning, lv_tr("Update anyway"), on_identity_confirm_cb,
        on_identity_cancel_cb, nullptr);
    if (!dlg) {
        // Couldn't show the dialog — fall back to the pre-gate behavior rather
        // than stranding the save (which would never fire_completion).
        spdlog::warn("[AmsEditOverlay] identity-change confirmation failed to show; updating anyway");
        do_spoolman_save();
    }
}

void AmsEditOverlay::on_identity_confirm_cb(lv_event_t* /*e*/) {
    // Dismiss the confirmation FIRST — modal_dialog has no auto-close, and
    // leaving it up would keep the buttons re-tappable, double-firing the
    // Spoolman write. Confirmation modals still stack above the overlay (§13.6).
    Modal::hide(Modal::get_top());
    get_ams_edit_overlay().do_spoolman_save();
}

void AmsEditOverlay::on_identity_cancel_cb(lv_event_t* /*e*/) {
    // Dismiss the confirmation first, then save the slot locally WITHOUT
    // touching the linked Spoolman spool (dismiss-safe).
    Modal::hide(Modal::get_top());
    get_ams_edit_overlay().close_editor(true);
}

// ============================================================================
// Static Callback Registration
// ============================================================================

void AmsEditOverlay::register_callbacks() {
    if (callbacks_registered_) {
        return;
    }

    register_xml_callbacks({
        {"ams_edit_back_cb", on_back_cb},
        {"ams_edit_vendor_changed_cb", on_vendor_changed_cb},
        {"ams_edit_material_changed_cb", on_material_changed_cb},
        {"ams_edit_color_clicked_cb", on_color_clicked_cb},
        {"ams_edit_remaining_changed_cb", on_remaining_changed_cb},
        {"ams_edit_remaining_edit_cb", on_remaining_edit_cb},
        {"ams_edit_remaining_accept_cb", on_remaining_accept_cb},
        {"ams_edit_remaining_cancel_cb", on_remaining_cancel_cb},
        {"ams_edit_save_cb", on_save_cb},
        {"ams_edit_change_spool_cb", on_change_spool_cb},
        {"ams_edit_spool_actions_clicked_cb", on_spool_actions_clicked_cb},
        {"ams_edit_spool_actions_changed_cb", on_spool_actions_changed_cb},
        {"ams_edit_scan_qr_cb", on_scan_qr_cb},
        {"ams_edit_picker_search_cb", on_picker_search_cb},
        {"ams_edit_picker_retry_cb", on_picker_retry_cb},
        // Shared spool_item component uses this callback name
        {"spoolman_spool_item_clicked_cb", on_spool_item_cb},
        {"ams_edit_tool_changed_cb", on_tool_changed_cb},
        {"ams_edit_weight_changed_cb", on_weight_changed_cb},
    });

    callbacks_registered_ = true;
    spdlog::debug("[AmsEditOverlay] Callbacks registered");
}

// ============================================================================
// Static Callbacks
// ============================================================================

AmsEditOverlay* AmsEditOverlay::get_instance_from_event(lv_event_t* /*e*/) {
    // Process-lifetime singleton — the accessor IS the instance resolution.
    return &get_ams_edit_overlay();
}

void AmsEditOverlay::on_back_cb(lv_event_t* e) {
    auto* self = get_instance_from_event(e);
    if (self) {
        self->handle_back();
    }
}

void AmsEditOverlay::on_vendor_changed_cb(lv_event_t* e) {
    auto* self = get_instance_from_event(e);
    if (self) {
        auto* dropdown = static_cast<lv_obj_t*>(lv_event_get_target(e));
        int index = lv_dropdown_get_selected(dropdown);
        self->handle_vendor_changed(index);
    }
}

void AmsEditOverlay::on_material_changed_cb(lv_event_t* e) {
    auto* self = get_instance_from_event(e);
    if (self) {
        auto* dropdown = static_cast<lv_obj_t*>(lv_event_get_target(e));
        int index = lv_dropdown_get_selected(dropdown);
        self->handle_material_changed(index);
    }
}

void AmsEditOverlay::on_color_clicked_cb(lv_event_t* e) {
    auto* self = get_instance_from_event(e);
    if (self) {
        self->handle_color_clicked();
    }
}

void AmsEditOverlay::on_remaining_changed_cb(lv_event_t* e) {
    auto* self = get_instance_from_event(e);
    if (self) {
        auto* slider = static_cast<lv_obj_t*>(lv_event_get_target(e));
        int value = lv_slider_get_value(slider);
        self->handle_remaining_changed(value);
    }
}

void AmsEditOverlay::on_remaining_edit_cb(lv_event_t* e) {
    auto* self = get_instance_from_event(e);
    if (self) {
        self->handle_remaining_edit();
    }
}

void AmsEditOverlay::on_remaining_accept_cb(lv_event_t* e) {
    auto* self = get_instance_from_event(e);
    if (self) {
        self->handle_remaining_accept();
    }
}

void AmsEditOverlay::on_remaining_cancel_cb(lv_event_t* e) {
    auto* self = get_instance_from_event(e);
    if (self) {
        self->handle_remaining_cancel();
    }
}

void AmsEditOverlay::on_save_cb(lv_event_t* e) {
    auto* self = get_instance_from_event(e);
    if (self) {
        self->handle_save();
    }
}

void AmsEditOverlay::on_change_spool_cb(lv_event_t* e) {
    auto* self = get_instance_from_event(e);
    if (self) {
        self->handle_change_spool();
    }
}

void AmsEditOverlay::on_spool_actions_clicked_cb(lv_event_t* e) {
    auto* self = get_instance_from_event(e);
    if (self) {
        self->handle_scan_qr();
    }
}

void AmsEditOverlay::on_spool_actions_changed_cb(lv_event_t* e) {
    auto* self = get_instance_from_event(e);
    if (!self)
        return;

    auto* split_btn = self->find_widget("btn_spool_actions");
    if (!split_btn)
        return;
    uint32_t idx = ui_split_button_get_selected(split_btn);
    switch (idx) {
    case 0:
        self->handle_scan_qr();
        break;
    case 1:
        self->handle_spool_details();
        break;
    case 2:
        self->handle_unlink();
        break;
#if HELIX_HAS_LABEL_PRINTER
    case 3:
        self->handle_print_label();
        break;
#endif
    default:
        break;
    }
}

void AmsEditOverlay::on_scan_qr_cb(lv_event_t* e) {
    auto* self = get_instance_from_event(e);
    if (self) {
        self->handle_scan_qr();
    }
}

void AmsEditOverlay::on_picker_search_cb(lv_event_t* e) {
    auto* self = get_instance_from_event(e);
    if (self) {
        auto* ta = static_cast<lv_obj_t*>(lv_event_get_target(e));
        const char* text = lv_textarea_get_text(ta);
        self->handle_picker_search(text);
    }
}

void AmsEditOverlay::on_picker_retry_cb(lv_event_t* e) {
    auto* self = get_instance_from_event(e);
    if (self) {
        spdlog::info("[AmsEditOverlay] Picker retry requested by user");
        self->populate_picker();
    }
}

void AmsEditOverlay::on_tool_changed_cb(lv_event_t* e) {
    auto* self = get_instance_from_event(e);
    if (self) {
        auto* dropdown = static_cast<lv_obj_t*>(lv_event_get_target(e));
        int index = lv_dropdown_get_selected(dropdown);
        self->handle_tool_changed(index);
    }
}

void AmsEditOverlay::on_weight_changed_cb(lv_event_t* e) {
    auto* self = get_instance_from_event(e);
    if (self) {
        self->handle_weight_input_changed();
    }
}

void AmsEditOverlay::on_spool_item_cb(lv_event_t* e) {
    auto* self = get_instance_from_event(e);
    if (!self) {
        return;
    }

    // Use current_target (the button with the handler), not target (the clicked child)
    lv_obj_t* item = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    if (!item) {
        return;
    }
    auto spool_id = static_cast<int>(reinterpret_cast<intptr_t>(lv_obj_get_user_data(item)));
    if (spool_id <= 0) {
        spdlog::warn("[AmsEditOverlay] Spool item clicked with invalid spool_id={}", spool_id);
        return;
    }
    self->handle_spool_selected(spool_id);
}

} // namespace helix::ui
