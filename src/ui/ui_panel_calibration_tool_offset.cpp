// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_panel_calibration_tool_offset.h"

#include "ui_callback_helpers.h"
#include "ui_emergency_stop.h"
#include "ui_event_safety.h"
#include "ui_modal.h"
#include "ui_nav_manager.h"

#include "app_globals.h"
#include "i_moonraker_api.h"
#include "i_moonraker_client.h"
#include "lvgl/src/others/translation/lv_translation.h"
#include "printer_state.h"
#include "static_panel_registry.h"

#include <spdlog/fmt/fmt.h>
#include <spdlog/spdlog.h>

#include <memory>

namespace {
constexpr const char* CONSOLE_HANDLER = "ToolOffsetCalPanel";
constexpr size_t LOG_LINES = 6;
} // namespace

// ============================================================================
// GLOBAL INSTANCE
// ============================================================================

static std::unique_ptr<ToolOffsetCalibrationPanel> g_tool_offset_cal_panel;

ToolOffsetCalibrationPanel& get_global_tool_offset_cal_panel() {
    if (!g_tool_offset_cal_panel) {
        g_tool_offset_cal_panel = std::make_unique<ToolOffsetCalibrationPanel>();
        StaticPanelRegistry::instance().register_destroy(
            "ToolOffsetCalibrationPanel", []() { g_tool_offset_cal_panel.reset(); });
    }
    return *g_tool_offset_cal_panel;
}

// ============================================================================
// CONSTRUCTOR / DESTRUCTOR
// ============================================================================

ToolOffsetCalibrationPanel::ToolOffsetCalibrationPanel() {
    spdlog::debug("[{}] Instance created", get_name());
}

ToolOffsetCalibrationPanel::~ToolOffsetCalibrationPanel() {
    elapsed_.cancel();
    unsubscribe_console();
    if (subjects_initialized_) {
        subjects_.deinit_all();
        subjects_initialized_ = false;
    }
}

// ============================================================================
// SUBJECTS / CALLBACKS
// ============================================================================

void ToolOffsetCalibrationPanel::init_subjects() {
    if (subjects_initialized_) {
        spdlog::debug("[{}] Subjects already initialized", get_name());
        return;
    }

    UI_MANAGED_SUBJECT_STRING(status_, status_buffer_, "Ready to calibrate",
                              "tool_offset_cal_status", subjects_);
    UI_MANAGED_SUBJECT_STRING(log_, log_buffer_, "", "tool_offset_cal_log", subjects_);
    UI_MANAGED_SUBJECT_STRING(hint_, hint_buffer_, "", "tool_offset_cal_hint", subjects_);
    UI_MANAGED_SUBJECT_INT(started_, 0, "tool_offset_cal_started", subjects_);
    UI_MANAGED_SUBJECT_INT(active_, 0, "tool_offset_cal_active", subjects_);
    UI_MANAGED_SUBJECT_INT(complete_, 0, "tool_offset_cal_complete", subjects_);

    subjects_initialized_ = true;

    register_xml_callbacks({
        {"on_tool_offset_cal_start", on_start_clicked},
        {"on_tool_offset_cal_cancel", on_cancel_clicked},
        {"on_tool_offset_cal_save", on_save_clicked},
    });

    spdlog::debug("[{}] Subjects and callbacks registered", get_name());
}

// ============================================================================
// CREATE / SHOW / LIFECYCLE
// ============================================================================

lv_obj_t* ToolOffsetCalibrationPanel::create(lv_obj_t* parent) {
    if (overlay_root_) {
        spdlog::debug("[{}] Overlay already created", get_name());
        return overlay_root_;
    }
    if (!create_overlay_from_xml(parent, "calibration_tool_offset_panel")) {
        return nullptr;
    }
    spdlog::info("[{}] Overlay created", get_name());
    return overlay_root_;
}

void ToolOffsetCalibrationPanel::show() {
    if (!overlay_root_) {
        spdlog::error("[{}] Cannot show: overlay not created", get_name());
        return;
    }
    NavigationManager::instance().register_overlay_instance(overlay_root_, this);
    NavigationManager::instance().push_overlay(overlay_root_);
}

void ToolOffsetCalibrationPanel::on_activate() {
    OverlayBase::on_activate();
    // Refresh the macro's description each open — config may have changed
    fetch_macro_description();
    if (!calibration_active_ && !calibration_complete_) {
        reset_ui_state();
    }
}

void ToolOffsetCalibrationPanel::on_deactivate() {
    // Backing out mid-calibration: the macro keeps running the printer
    // otherwise (it blocks the gcode queue) — same policy as the wizard step
    // and the PID panel.
    if (calibration_active_) {
        spdlog::info("[{}] Aborting calibration on deactivate", get_name());
        abort_in_progress_calibration();
    }
    elapsed_.cancel();
    unsubscribe_console();
    OverlayBase::on_deactivate();
}

void ToolOffsetCalibrationPanel::cleanup() {
    if (calibration_active_) {
        abort_in_progress_calibration();
    }
    elapsed_.cancel();
    unsubscribe_console();
    if (overlay_root_) {
        NavigationManager::instance().unregister_overlay_instance(overlay_root_);
    }
    OverlayBase::cleanup();
}

void ToolOffsetCalibrationPanel::reset_ui_state() {
    if (!subjects_initialized_) {
        return;
    }
    lv_subject_set_int(&started_, 0);
    lv_subject_set_int(&active_, 0);
    lv_subject_set_int(&complete_, 0);
    log_lines_.clear();
    lv_subject_copy_string(&log_, "");
    lv_subject_copy_string(&status_, lv_tr("Ready to calibrate"));
}

// ============================================================================
// CAPABILITY GATE
// ============================================================================

bool ToolOffsetCalibrationPanel::printer_supports_calibration() {
    const auto& hw = get_printer_state().get_discovery();
    return hw.has_tool_changer() && hw.has_macro(CALIBRATE_MACRO);
}

// ============================================================================
// XML EVENT TRAMPOLINES
// ============================================================================

void ToolOffsetCalibrationPanel::on_start_clicked(lv_event_t* e) {
    (void)e;
    LVGL_SAFE_EVENT_CB_BEGIN("[ToolOffsetCal] on_start_clicked");
    auto& panel = get_global_tool_offset_cal_panel();
    // The macro's description carries the printer's preconditions (remove the
    // build plate, clean the nozzles, ...) — moves that can crash a nozzle
    // deserve an explicit confirmation, not a muted hint line.
    const char* hint = lv_subject_get_string(panel.get_hint_subject());
    if (!hint || !*hint) {
        panel.start_calibration();
    } else {
        helix::ui::modal_show_confirmation(
            lv_tr("Before calibrating"), hint, ModalSeverity::Warning, lv_tr("Start"),
            [](lv_event_t* ev) {
                (void)ev;
                LVGL_SAFE_EVENT_CB_BEGIN("[ToolOffsetCal] confirm_start");
                Modal::hide(Modal::get_top());
                get_global_tool_offset_cal_panel().start_calibration();
                LVGL_SAFE_EVENT_CB_END();
            },
            [](lv_event_t* ev) {
                (void)ev;
                LVGL_SAFE_EVENT_CB_BEGIN("[ToolOffsetCal] cancel_start");
                Modal::hide(Modal::get_top());
                LVGL_SAFE_EVENT_CB_END();
            },
            &panel);
    }
    LVGL_SAFE_EVENT_CB_END();
}

void ToolOffsetCalibrationPanel::on_cancel_clicked(lv_event_t* e) {
    (void)e;
    LVGL_SAFE_EVENT_CB_BEGIN("[ToolOffsetCal] on_cancel_clicked");
    get_global_tool_offset_cal_panel().abort_in_progress_calibration();
    LVGL_SAFE_EVENT_CB_END();
}

void ToolOffsetCalibrationPanel::on_save_clicked(lv_event_t* e) {
    (void)e;
    LVGL_SAFE_EVENT_CB_BEGIN("[ToolOffsetCal] on_save_clicked");
    get_global_tool_offset_cal_panel().save_calibration();
    LVGL_SAFE_EVENT_CB_END();
}

// ============================================================================
// CALIBRATION FLOW
// ============================================================================

void ToolOffsetCalibrationPanel::start_calibration() {
    if (calibration_active_) {
        return;
    }
    auto* api = get_moonraker_api();
    if (!api) {
        spdlog::warn("[{}] No API - cannot start calibration", get_name());
        return;
    }

    calibration_active_ = true;
    calibration_complete_ = false;
    log_lines_.clear();
    lv_subject_copy_string(&log_, "");
    lv_subject_set_int(&started_, 1);
    lv_subject_set_int(&active_, 1);
    lv_subject_set_int(&complete_, 0);

    subscribe_console();
    elapsed_.begin(&status_, [](uint32_t elapsed_seconds) {
        return fmt::format(lv_tr("Calibrating tool offsets... {}s"), elapsed_seconds);
    });

    spdlog::info("[{}] Running {}", get_name(), CALIBRATE_MACRO);
    // Moonraker's printer.gcode.script answers when the macro finishes, so the
    // success callback IS the completion signal. Four tools of probing can run
    // well past the 5-minute macro ceiling.
    api->execute_gcode(
        CALIBRATE_MACRO,
        lifetime_.bg_cb("ToolOffsetCalPanel::calibrate_done",
                        [this]() { on_calibration_finished(true, ""); }),
        lifetime_.bg_cb("ToolOffsetCalPanel::calibrate_error",
                        [this](const MoonrakerError& err) {
                            on_calibration_finished(false, err.message);
                        }),
        IMoonrakerAPI::PRE_START_MACRO_TIMEOUT_MS);
}

void ToolOffsetCalibrationPanel::on_calibration_finished(bool ok, const std::string& error) {
    elapsed_.cancel();
    unsubscribe_console();
    calibration_active_ = false;
    lv_subject_set_int(&active_, 0);
    // Start is visible again either way: for a retry on failure, or a re-run
    // after success.
    lv_subject_set_int(&started_, 0);

    if (ok) {
        spdlog::info("[{}] {} finished", get_name(), CALIBRATE_MACRO);
        calibration_complete_ = true;
        lv_subject_set_int(&complete_, 1);
        lv_subject_copy_string(&status_, lv_tr("Calibration complete!"));
        return;
    }

    spdlog::error("[{}] {} failed: {}", get_name(), CALIBRATE_MACRO, error);
    lv_subject_copy_string(&status_, error.empty() ? lv_tr("Calibration failed") : error.c_str());
}

bool ToolOffsetCalibrationPanel::abort_in_progress_calibration() {
    if (!calibration_active_) {
        return false;
    }
    spdlog::info("[{}] Aborting in-progress calibration (M112 + firmware_restart)", get_name());

    // Expected reconnect — keep the shutdown/disconnect modals quiet
    EmergencyStopOverlay::instance().suppress_recovery_dialog(RecoverySuppression::LONG);
    auto* api = get_moonraker_api();
    if (api) {
        api->suppress_disconnect_modal(15000);
    }

    // Drop the in-flight execute_gcode callbacks (they report the M112 shutdown)
    lifetime_.invalidate();
    elapsed_.cancel();
    unsubscribe_console();
    calibration_active_ = false;

    if (api) {
        api->emergency_stop(
            [api]() {
                spdlog::debug("[ToolOffsetCal] M112 sent, restarting firmware");
                api->restart_firmware([]() {}, [](const MoonrakerError& err) {
                    spdlog::error("[ToolOffsetCal] Firmware restart failed: {}", err.message);
                });
            },
            [](const MoonrakerError& err) {
                spdlog::error("[ToolOffsetCal] Emergency stop failed: {}", err.message);
            });
    }

    reset_ui_state();
    lv_subject_copy_string(&status_, lv_tr("Cancelled"));
    return true;
}

void ToolOffsetCalibrationPanel::save_calibration() {
    auto* api = get_moonraker_api();
    if (!api || !calibration_complete_) {
        return;
    }
    // SAVE_CONFIG restarts Klipper — an expected disconnect, not an error
    api->suppress_disconnect_modal(15000);
    calibration_complete_ = false;
    lv_subject_set_int(&complete_, 0);
    lv_subject_copy_string(&status_, lv_tr("Saving — Klipper is restarting..."));
    spdlog::info("[{}] Sending SAVE_CONFIG", get_name());
    api->execute_gcode(
        "SAVE_CONFIG",
        lifetime_.bg_cb("ToolOffsetCalPanel::save_done",
                        [this]() { lv_subject_copy_string(&status_, lv_tr("Offsets saved")); }),
        lifetime_.bg_cb("ToolOffsetCalPanel::save_error",
                        [this](const MoonrakerError& err) {
                            // The restart usually swallows the response; the save
                            // itself has already happened by then. A genuine
                            // refusal (e.g. "conflicts with included value")
                            // reaches the user via Klipper's !! console router —
                            // caller_surfaces_errors=false keeps it armed.
                            spdlog::debug("[ToolOffsetCal] SAVE_CONFIG response lost: {}",
                                          err.message);
                            lv_subject_copy_string(&status_, lv_tr("Offsets saved"));
                        }),
        0, false, nullptr, /*caller_surfaces_errors=*/false);
}

// ============================================================================
// CONSOLE MIRROR + MACRO DESCRIPTION
// ============================================================================

void ToolOffsetCalibrationPanel::append_log_line(const std::string& raw) {
    std::string line = raw;
    if (line.rfind("// ", 0) == 0) {
        line.erase(0, 3); // Klipper's respond_info prefix
    }
    if (line.empty()) {
        return;
    }
    log_lines_.push_back(line);
    while (log_lines_.size() > LOG_LINES) {
        log_lines_.pop_front();
    }
    std::string joined;
    for (const auto& l : log_lines_) {
        if (!joined.empty()) {
            joined += '\n';
        }
        joined += l;
    }
    lv_subject_copy_string(&log_, joined.c_str());
}

void ToolOffsetCalibrationPanel::subscribe_console() {
    auto* client = get_moonraker_client();
    if (!client || console_subscribed_) {
        return;
    }
    // WS thread → bg_cb queues the body to the main thread (threading rule 1)
    client->register_method_callback(
        "notify_gcode_response", CONSOLE_HANDLER,
        lifetime_.bg_cb("ToolOffsetCalPanel::console", [this](const nlohmann::json& msg) {
            if (!msg.contains("params") || !msg["params"].is_array()) {
                return;
            }
            for (const auto& p : msg["params"]) {
                if (p.is_string()) {
                    append_log_line(p.get<std::string>());
                } else if (p.is_array()) {
                    for (const auto& line : p) {
                        if (line.is_string()) {
                            append_log_line(line.get<std::string>());
                        }
                    }
                }
            }
        }));
    console_subscribed_ = true;
}

void ToolOffsetCalibrationPanel::unsubscribe_console() {
    if (!console_subscribed_) {
        return;
    }
    if (auto* client = get_moonraker_client()) {
        client->unregister_method_callback("notify_gcode_response", CONSOLE_HANDLER);
    }
    console_subscribed_ = false;
}

void ToolOffsetCalibrationPanel::fetch_macro_description() {
    auto* client = get_moonraker_client();
    if (!client) {
        return;
    }
    // printer.gcode.help → {"CMD": "description", ...}; the macro's own
    // `description:` is the instruction text ("remove the build plate", ...).
    client->send_jsonrpc(
        "printer.gcode.help", nlohmann::json::object(),
        lifetime_.bg_cb("ToolOffsetCalPanel::gcode_help", [this](const nlohmann::json& resp) {
            const nlohmann::json& result = resp.contains("result") ? resp["result"] : resp;
            if (!result.is_object() || !result.contains(CALIBRATE_MACRO) ||
                !result[CALIBRATE_MACRO].is_string()) {
                return;
            }
            const std::string desc = result[CALIBRATE_MACRO].get<std::string>();
            if (desc.empty() || desc == "G-Code macro") {
                return; // Klipper's placeholder for a macro without description:
            }
            lv_subject_copy_string(&hint_, desc.c_str());
        }));
}
