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
#include "tool_state.h"

#include <cstdio>
#include <cstdlib>

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

    // Per-tool row slots (fixed MAX_TOOLS, extra rows stay hidden). The XML
    // engine copies the registration name, so fmt-built names are safe here.
    for (int i = 0; i < MAX_TOOLS; ++i) {
        UI_MANAGED_SUBJECT_INT(row_visible_[i], 0,
                               fmt::format("tool_offset_cal_row_visible_{}", i).c_str(),
                               subjects_);
        UI_MANAGED_SUBJECT_STRING(row_text_[i], row_text_buffer_[i], "X --   Y --   Z --",
                                  fmt::format("tool_offset_cal_text_{}", i).c_str(), subjects_);
        UI_MANAGED_SUBJECT_INT(row_spin_[i], 0, fmt::format("tool_offset_cal_spin_{}", i).c_str(),
                               subjects_);
    }

    UI_MANAGED_SUBJECT_INT(station_spin_, 0, "tool_offset_cal_station_spin", subjects_);
    UI_MANAGED_SUBJECT_STRING(station_text_, station_text_buffer_, "Not calibrated",
                              "tool_offset_cal_station_text", subjects_);
    UI_MANAGED_SUBJECT_INT(save_pending_, 0, "tool_offset_cal_save_pending", subjects_);
    UI_MANAGED_SUBJECT_STRING(warning_, warning_buffer_, "", "tool_offset_cal_warning", subjects_);
    UI_MANAGED_SUBJECT_INT(has_warning_, 0, "tool_offset_cal_has_warning", subjects_);

    subjects_initialized_ = true;

    register_xml_callbacks({
        {"on_tool_offset_cal_start", on_start_clicked},
        {"on_tool_offset_cal_tool", on_tool_clicked},
        {"on_tool_offset_cal_locate", on_locate_clicked},
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
    refresh_tool_rows();
    // Rows show what the printer actually has stored, not just what this
    // session measured — a tool calibrated last week is already valid.
    refresh_from_printer();
    if (!calibration_active_ && !calibration_complete_) {
        reset_ui_state();
    }
}

void ToolOffsetCalibrationPanel::refresh_tool_rows() {
    if (!subjects_initialized_) {
        return;
    }
    int count = lv_subject_get_int(helix::ToolState::instance().get_tool_count_subject());
    // A tool changer without a reported tool list still has at least two tools
    // (otherwise this overlay is unreachable); clamp into the row range.
    if (count < 2) {
        count = 2;
    }
    if (count > MAX_TOOLS) {
        count = MAX_TOOLS;
    }
    for (int i = 0; i < MAX_TOOLS; ++i) {
        lv_subject_set_int(&row_visible_[i], i < count ? 1 : 0);
        set_row_text(i);
    }
}

void ToolOffsetCalibrationPanel::set_row_text(int tool) {
    if (!subjects_initialized_ || tool < 0 || tool >= MAX_TOOLS) {
        return;
    }
    if (!applied_valid_[tool]) {
        lv_subject_copy_string(&row_text_[tool], lv_tr("Not calibrated"));
        return;
    }
    // dX/dY vs the base tool, Z absolute — the same three numbers a
    // toolchange applies, so they read the same here as in the console.
    lv_subject_copy_string(&row_text_[tool],
                           fmt::format("dX {:+.3f}   dY {:+.3f}   Z {:+.3f}", applied_[tool][0],
                                       applied_[tool][1], applied_[tool][2])
                               .c_str());
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
    for (auto& spin : row_spin_) {
        lv_subject_set_int(&spin, 0);
    }
    lv_subject_set_int(&station_spin_, 0);
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

// The queue for the pending confirmation — set before the modal opens, read by
// its confirm callback. Main-thread only, so a plain member-free static is fine.
static std::vector<int> g_pending_tools;

void ToolOffsetCalibrationPanel::confirm_and_run(std::vector<int> tools) {
    // Probing runs below plate level: an explicit build-plate confirmation
    // guards every start, with the macro's own description appended when the
    // printer provides one.
    g_pending_tools = std::move(tools);
    // The firmware measures whether the plate is off and refuses on its own,
    // so this is no longer a promise the operator makes. What it CANNOT check
    // is the part that decides how good the result is — a blob of filament on
    // a nozzle gets measured as part of the nozzle.
    // The macro's own description says the same thing in fewer words, so
    // showing both just repeats itself. This text is the superset: it also
    // covers the two conditions the plate check cannot measure.
    const std::string msg =
        lv_tr("Take the build plate off and clean every nozzle. Tools should be cold and "
              "docked. The printer checks the plate itself and will refuse if it is still on.");
    helix::ui::modal_show_confirmation(
        lv_tr("Before calibrating"), msg.c_str(), ModalSeverity::Warning, lv_tr("Start"),
        [](lv_event_t* ev) {
            (void)ev;
            LVGL_SAFE_EVENT_CB_BEGIN("[ToolOffsetCal] confirm_start");
            Modal::hide(Modal::get_top());
            get_global_tool_offset_cal_panel().begin_run(std::move(g_pending_tools));
            LVGL_SAFE_EVENT_CB_END();
        },
        [](lv_event_t* ev) {
            (void)ev;
            LVGL_SAFE_EVENT_CB_BEGIN("[ToolOffsetCal] cancel_start");
            Modal::hide(Modal::get_top());
            LVGL_SAFE_EVENT_CB_END();
        },
        this);
}

void ToolOffsetCalibrationPanel::on_start_clicked(lv_event_t* e) {
    (void)e;
    LVGL_SAFE_EVENT_CB_BEGIN("[ToolOffsetCal] on_start_clicked");
    get_global_tool_offset_cal_panel().start_calibration();
    LVGL_SAFE_EVENT_CB_END();
}

void ToolOffsetCalibrationPanel::on_tool_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[ToolOffsetCal] on_tool_clicked");
    // user_data carries the tool index as a string ("0".."3"), the same
    // convention as the wizard language chooser buttons.
    const char* arg = static_cast<const char*>(lv_event_get_user_data(e));
    if (arg && *arg) {
        get_global_tool_offset_cal_panel().start_calibration_for_tool(std::atoi(arg));
    }
    LVGL_SAFE_EVENT_CB_END();
}

void ToolOffsetCalibrationPanel::on_locate_clicked(lv_event_t* e) {
    (void)e;
    LVGL_SAFE_EVENT_CB_BEGIN("[ToolOffsetCal] on_locate_clicked");
    get_global_tool_offset_cal_panel().start_locate_sensor();
    LVGL_SAFE_EVENT_CB_END();
}

void ToolOffsetCalibrationPanel::on_cancel_clicked(lv_event_t* e) {
    (void)e;
    LVGL_SAFE_EVENT_CB_BEGIN("[ToolOffsetCal] on_cancel_clicked");
    // Between tools this is pure UI state, so Stop is clean: finish the tool
    // currently probing, then stop issuing commands. (Backing out of the
    // overlay mid-probe still goes through the M112 abort in on_deactivate.)
    get_global_tool_offset_cal_panel().request_stop();
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
    // Same shape as the CALIBRATE_TOOL_OFFSETS macro: the reference first,
    // then every tool. Driving it here rather than running the macro is what
    // keeps Stop clean and the per-row progress honest.
    std::vector<int> steps{STATION_STEP};
    for (int i = 0; i < MAX_TOOLS; ++i) {
        if (lv_subject_get_int(&row_visible_[i])) {
            steps.push_back(i);
        }
    }
    confirm_and_run(std::move(steps));
}

void ToolOffsetCalibrationPanel::start_locate_sensor() {
    if (calibration_active_) {
        return;
    }
    confirm_and_run({STATION_STEP});
}

void ToolOffsetCalibrationPanel::start_calibration_for_tool(int tool) {
    if (calibration_active_ || tool < 0 || tool >= MAX_TOOLS) {
        return;
    }
    // A tool pass without a station reference still runs, but loses the gap
    // guard that catches a mis-triggered Z. Fold the reference in when the
    // printer has never had one.
    if (!station_known_) {
        spdlog::info("[{}] No station reference yet — running {} first", get_name(), LOCATE_CMD);
        confirm_and_run({STATION_STEP, tool});
        return;
    }
    confirm_and_run({tool});
}

void ToolOffsetCalibrationPanel::request_stop() {
    if (!calibration_active_ || stop_requested_) {
        return;
    }
    stop_requested_ = true;
    lv_subject_copy_string(&status_, lv_tr("Stopping after the current step..."));
}

void ToolOffsetCalibrationPanel::begin_run(std::vector<int> steps) {
    if (calibration_active_ || steps.empty()) {
        return;
    }
    auto* api = get_moonraker_api();
    if (!api) {
        spdlog::warn("[{}] No API - cannot start calibration", get_name());
        return;
    }

    calibration_active_ = true;
    calibration_complete_ = false;
    stop_requested_ = false;
    run_queue_ = std::move(steps);
    log_lines_.clear();
    lv_subject_copy_string(&log_, "");
    lv_subject_set_int(&started_, 1);
    lv_subject_set_int(&active_, 1);
    lv_subject_set_int(&complete_, 0);

    subscribe_console();
    send_next_step();
}

void ToolOffsetCalibrationPanel::send_next_step() {
    auto* api = get_moonraker_api();
    if (!api || run_queue_.empty()) {
        finish_run(true, "");
        return;
    }
    current_step_ = run_queue_.front();
    run_queue_.erase(run_queue_.begin());

    std::string cmd;
    if (current_step_ == STATION_STEP) {
        lv_subject_set_int(&station_spin_, 1);
        elapsed_.begin(&status_, [](uint32_t elapsed_seconds) {
            return fmt::format(lv_tr("Locating the station — empty carriage... {}s"),
                               elapsed_seconds);
        });
        cmd = LOCATE_CMD;
    } else {
        lv_subject_set_int(&row_spin_[current_step_], 1);
        elapsed_.begin(&status_, [tool = current_step_](uint32_t elapsed_seconds) {
            return fmt::format(lv_tr("Calibrating T{} — probing nozzle... {}s"), tool,
                               elapsed_seconds);
        });
        // TOOL_CALIBRATE_TOOL_OFFSET takes no arguments — it measures whatever
        // is on the carriage, so the tool has to be selected first. Klipper
        // runs a multi-line script line by line.
        cmd = fmt::format("SELECT_TOOL T={}\n{}", current_step_, CALIBRATE_TOOL_CMD);
    }

    spdlog::info("[{}] Running {}", get_name(), cmd);
    // Moonraker's printer.gcode.script answers when the script finishes, so the
    // success callback IS the completion signal. A single pass can still run
    // past the 5-minute macro ceiling.
    api->execute_gcode(
        cmd,
        lifetime_.bg_cb("ToolOffsetCalPanel::calibrate_done",
                        [this]() { on_step_finished(true, ""); }),
        lifetime_.bg_cb("ToolOffsetCalPanel::calibrate_error",
                        [this](const MoonrakerError& err) { on_step_finished(false, err.message); }),
        IMoonrakerAPI::PRE_START_MACRO_TIMEOUT_MS);
}

void ToolOffsetCalibrationPanel::on_step_finished(bool ok, const std::string& error) {
    elapsed_.cancel();
    if (current_step_ == STATION_STEP) {
        lv_subject_set_int(&station_spin_, 0);
    } else if (current_step_ >= 0 && current_step_ < MAX_TOOLS) {
        lv_subject_set_int(&row_spin_[current_step_], 0);
    }
    current_step_ = -2;

    if (!ok) {
        finish_run(false, error);
        return;
    }
    if (stop_requested_ || run_queue_.empty()) {
        finish_run(true, "");
        return;
    }
    send_next_step();
}

void ToolOffsetCalibrationPanel::finish_run(bool ok, const std::string& error) {
    elapsed_.cancel();
    unsubscribe_console();
    calibration_active_ = false;
    stop_requested_ = false;
    run_queue_.clear();
    lv_subject_set_int(&active_, 0);
    lv_subject_set_int(&started_, 0);

    if (ok) {
        spdlog::info("[{}] Calibration run finished", get_name());
        calibration_complete_ = true;
        lv_subject_set_int(&complete_, 1);
        lv_subject_copy_string(&status_, lv_tr("Calibration complete."));
        // Picks up save_config_pending and anything the console block did not
        // carry (e.g. a stopped run that still calibrated some tools).
        refresh_from_printer();
        return;
    }
    // A refused run leaves the previous calibration intact, and the refusal
    // itself is the interesting output — re-read so the rows stay truthful.
    refresh_from_printer();

    spdlog::error("[{}] Calibration failed: {}", get_name(), error);
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
    stop_requested_ = false;
    current_step_ = -2;
    run_queue_.clear();

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
    // The firmware's report block is the source of the displayed numbers:
    //   "  T<n>: dX +0.0000  dY +0.0000  Z +3.1514"
    // dX/dY are differences against the base tool, Z is absolute. sscanf's
    // literal spaces match any run of whitespace, so the leading indent and
    // the double spaces between fields both fall out for free.
    {
        int tool = -1;
        double dx = 0, dy = 0, z = 0;
        if (std::sscanf(line.c_str(), " T%d: dX %lf dY %lf Z %lf", &tool, &dx, &dy, &z) == 4 &&
            tool >= 0 && tool < MAX_TOOLS) {
            applied_[tool][0] = dx;
            applied_[tool][1] = dy;
            applied_[tool][2] = z;
            applied_valid_[tool] = true;
            set_row_text(tool);
        }
    }
    // The reference pass reports "station = (<x>, <y>, <z>)".
    {
        double sx = 0, sy = 0, sz = 0;
        if (std::sscanf(line.c_str(), " station = (%lf, %lf, %lf)", &sx, &sy, &sz) == 3) {
            station_known_ = true;
            station_z_ = sz;
            lv_subject_copy_string(
                &station_text_,
                fmt::format(fmt::runtime(lv_tr("Station Z {:.3f}")), sz).c_str());
        }
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

// ============================================================================
// ADVANCED-PANEL ROW ENTRY
// ============================================================================

static void on_tool_offset_row_clicked(lv_event_t* e) {
    (void)e;
    LVGL_SAFE_EVENT_CB_BEGIN("[ToolOffsetCal] row_clicked");
    auto& panel = get_global_tool_offset_cal_panel();
    // Subjects and callbacks MUST exist before the XML is built, or every
    // bind_flag/event_cb in the component silently no-ops and the panel comes
    // up with all buttons and spinners visible at once. Same order as
    // helix::ui::lazy_create_and_push_overlay(), which the Controls entry uses.
    if (!panel.are_subjects_initialized()) {
        panel.init_subjects();
    }
    panel.register_callbacks();
    bool ready = panel.get_root() != nullptr;
    if (!ready) {
        lv_obj_t* screen = lv_display_get_screen_active(nullptr);
        ready = panel.create(screen) != nullptr;
        if (!ready) {
            spdlog::error("[ToolOffsetCal] Failed to create calibration_tool_offset_panel");
        }
    }
    if (ready) {
        panel.show();
    }
    LVGL_SAFE_EVENT_CB_END();
}

void init_tool_offset_row_handler() {
    lv_xml_register_event_cb(nullptr, "on_tool_offset_row_clicked", on_tool_offset_row_clicked);
}

void ToolOffsetCalibrationPanel::refresh_from_printer() {
    auto* client = get_moonraker_client();
    if (!client) {
        return;
    }
    // One-shot read rather than a subscription: this is the only place that
    // needs these objects, and it keeps the firmware-specific object names
    // inside the module that owns the capability.
    nlohmann::json objects = nlohmann::json::object();
    objects["ff_tool_offset"] = nullptr;
    for (int i = 0; i < MAX_TOOLS; ++i) {
        objects[fmt::format("ff_tool {}", i)] = nullptr;
    }
    objects["configfile"] = nlohmann::json::array({"save_config_pending"});

    client->send_jsonrpc(
        "printer.objects.query", nlohmann::json{{"objects", objects}},
        lifetime_.bg_cb("ToolOffsetCalPanel::objects_query", [this](const nlohmann::json& resp) {
            if (!subjects_initialized_) {
                return;
            }
            const nlohmann::json& result = resp.contains("result") ? resp["result"] : resp;
            if (result.is_object() && result.contains("status")) {
                apply_printer_state(result["status"]);
            }
        }));
}

void ToolOffsetCalibrationPanel::apply_printer_state(const nlohmann::json& status) {
    auto number = [](const nlohmann::json& obj, const char* key, double& out) {
        if (obj.contains(key) && obj[key].is_number()) {
            out = obj[key].get<double>();
            return true;
        }
        return false;
    };

    station_known_ = false;
    if (status.contains("ff_tool_offset") && status["ff_tool_offset"].is_object()) {
        station_known_ = number(status["ff_tool_offset"], "station_z", station_z_);
    }
    lv_subject_copy_string(
        &station_text_,
        station_known_
            ? fmt::format(fmt::runtime(lv_tr("Station Z {:.3f}")), station_z_).c_str()
            : lv_tr("Not calibrated"));

    // Base tool's nozzle_x/y are the reference dX/dY are measured against.
    double base_x = 0, base_y = 0;
    bool base_known = false;
    const std::string base_key = fmt::format("ff_tool {}", BASE_TOOL);
    if (status.contains(base_key) && status[base_key].is_object()) {
        const auto& base = status[base_key];
        base_known = base.value("calibrated", false) && number(base, "nozzle_x", base_x) &&
                     number(base, "nozzle_y", base_y);
    }

    int uncalibrated = 0;
    for (int i = 0; i < MAX_TOOLS; ++i) {
        if (!lv_subject_get_int(&row_visible_[i])) {
            continue;
        }
        applied_valid_[i] = false;
        const std::string key = fmt::format("ff_tool {}", i);
        if (!status.contains(key) || !status[key].is_object()) {
            set_row_text(i);
            continue;
        }
        const auto& tool = status[key];
        double nozzle_x = 0, nozzle_y = 0, nozzle_z = 0, z_adjust = 0;
        const bool calibrated = tool.value("calibrated", false) &&
                                number(tool, "nozzle_x", nozzle_x) &&
                                number(tool, "nozzle_y", nozzle_y) &&
                                number(tool, "nozzle_z", nozzle_z);
        number(tool, "z_adjust", z_adjust);
        // Mirrors ff_toolchange._derive_offsets: X/Y vs the base tool, Z
        // absolute once a station reference exists. Without one there is no
        // meaningful Z to show, so the row stays uncalibrated.
        if (calibrated && base_known && station_known_) {
            applied_[i][0] = nozzle_x - base_x;
            applied_[i][1] = nozzle_y - base_y;
            applied_[i][2] = nozzle_z - station_z_ + z_adjust;
            applied_valid_[i] = true;
        }
        if (!calibrated) {
            ++uncalibrated;
        }
        set_row_text(i);
    }

    bool pending = false;
    if (status.contains("configfile") && status["configfile"].is_object()) {
        pending = status["configfile"].value("save_config_pending", false);
    }
    lv_subject_set_int(&save_pending_, pending ? 1 : 0);

    // START_PRINT refuses before heating when a tool the file uses has no
    // nozzle_z, or the station reference is missing — say so here rather than
    // letting the user discover it at print start.
    std::string warning;
    if (!station_known_ && uncalibrated > 0) {
        warning = lv_tr("No station reference and uncalibrated tools — printing will be refused.");
    } else if (!station_known_) {
        warning = lv_tr("No station reference yet — printing will be refused.");
    } else if (uncalibrated > 0) {
        warning = lv_tr("Some tools are not calibrated — printing with them will be refused.");
    }
    lv_subject_copy_string(&warning_, warning.c_str());
    // bind_flag compares ints, so the emptiness is its own subject.
    lv_subject_set_int(&has_warning_, warning.empty() ? 0 : 1);
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
            if (!subjects_initialized_) {
                return; // reply outran init_subjects(); hint_ is not a subject yet
            }
            lv_subject_copy_string(&hint_, desc.c_str());
        }));
}
