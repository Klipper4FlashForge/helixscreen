// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_wizard_tool_offset.h"

#include "ui_emergency_stop.h"
#include "ui_event_safety.h"
#include "ui_modal.h"
#include "ui_wizard_helpers.h"

#include "app_globals.h"
#include "i_moonraker_api.h"
#include "i_moonraker_client.h"
#include "lvgl/lvgl.h"
#include "lvgl/src/others/translation/lv_translation.h"
#include "printer_state.h"
#include "static_panel_registry.h"
#include "tool_state.h"

#include <spdlog/fmt/fmt.h>
#include <spdlog/spdlog.h>

#include <cstring>
#include <memory>
#include <string>

// External wizard subjects (defined in ui_wizard.cpp)
extern lv_subject_t wizard_show_skip;
extern lv_subject_t connection_test_passed;

namespace {
constexpr const char* CONSOLE_HANDLER = "WizardToolOffsetStep";
constexpr size_t LOG_LINES = 6;
} // namespace

// ============================================================================
// Global Instance
// ============================================================================

static std::unique_ptr<WizardToolOffsetStep> g_wizard_tool_offset_step;

WizardToolOffsetStep* get_wizard_tool_offset_step() {
    if (!g_wizard_tool_offset_step) {
        g_wizard_tool_offset_step = std::make_unique<WizardToolOffsetStep>();
        StaticPanelRegistry::instance().register_destroy(
            "WizardToolOffsetStep", []() { g_wizard_tool_offset_step.reset(); });
    }
    return g_wizard_tool_offset_step.get();
}

// ============================================================================
// Constructor / Destructor
// ============================================================================

WizardToolOffsetStep::WizardToolOffsetStep() {
    spdlog::debug("[{}] Instance created", get_name());
}

WizardToolOffsetStep::~WizardToolOffsetStep() {
    elapsed_.cancel();
    unsubscribe_console();
    if (subjects_initialized_) {
        lv_subject_deinit(&status_);
        lv_subject_deinit(&log_);
        lv_subject_deinit(&hint_);
        lv_subject_deinit(&started_);
        lv_subject_deinit(&active_);
        lv_subject_deinit(&complete_);
        lv_subject_deinit(&saved_);
        subjects_initialized_ = false;
    }
    // NOTE: Do NOT log here - spdlog may be destroyed first
    screen_root_ = nullptr;
}

// ============================================================================
// Subjects
// ============================================================================

void WizardToolOffsetStep::init_subjects() {
    if (subjects_initialized_) {
        spdlog::debug("[{}] Subjects already initialized", get_name());
        return;
    }

    lv_subject_init_string(&status_, status_buffer_, nullptr, sizeof(status_buffer_),
                           status_buffer_);
    lv_xml_register_subject(nullptr, "wizard_tool_offset_status", &status_);
    lv_subject_init_string(&log_, log_buffer_, nullptr, sizeof(log_buffer_), log_buffer_);
    lv_xml_register_subject(nullptr, "wizard_tool_offset_log", &log_);
    lv_subject_init_string(&hint_, hint_buffer_, nullptr, sizeof(hint_buffer_), hint_buffer_);
    lv_xml_register_subject(nullptr, "wizard_tool_offset_hint", &hint_);

    helix::ui::wizard::init_int_subject(&started_, 0, "wizard_tool_offset_started");
    helix::ui::wizard::init_int_subject(&active_, 0, "wizard_tool_offset_active");
    helix::ui::wizard::init_int_subject(&complete_, 0, "wizard_tool_offset_complete");
    helix::ui::wizard::init_int_subject(&saved_, 0, "wizard_tool_offset_saved");

    subjects_initialized_ = true;
    spdlog::debug("[{}] Subjects initialized", get_name());
}

// ============================================================================
// Callbacks
// ============================================================================

static void on_start_tool_offset_clicked(lv_event_t* e) {
    (void)e;
    spdlog::debug("[Wizard Tool Offset] Start calibration clicked");
    auto* step = get_wizard_tool_offset_step();
    if (!step) {
        return;
    }
    // The macro's description carries the printer's preconditions (remove the
    // build plate, clean the nozzles, ...) — moves that can crash a nozzle
    // deserve an explicit confirmation, not a muted hint line.
    const char* hint = lv_subject_get_string(step->get_hint_subject());
    if (!hint || !*hint) {
        step->start_calibration();
        return;
    }
    helix::ui::modal_show_confirmation(
        lv_tr("Before calibrating"), hint, ModalSeverity::Warning, lv_tr("Start"),
        [](lv_event_t* ev) {
            LVGL_SAFE_EVENT_CB_BEGIN("[Wizard Tool Offset] confirm_start");
            Modal::hide(Modal::get_top());
            if (auto* s = get_wizard_tool_offset_step()) {
                s->start_calibration();
            }
            LVGL_SAFE_EVENT_CB_END();
        },
        [](lv_event_t* ev) {
            LVGL_SAFE_EVENT_CB_BEGIN("[Wizard Tool Offset] cancel_start");
            Modal::hide(Modal::get_top());
            LVGL_SAFE_EVENT_CB_END();
        },
        step);
}

static void on_cancel_tool_offset_clicked(lv_event_t* e) {
    (void)e;
    spdlog::info("[Wizard Tool Offset] Cancel clicked");
    if (auto* step = get_wizard_tool_offset_step()) {
        step->abort_in_progress_calibration();
    }
}

static void on_save_tool_offset_clicked(lv_event_t* e) {
    (void)e;
    spdlog::info("[Wizard Tool Offset] Save clicked");
    if (auto* step = get_wizard_tool_offset_step()) {
        step->save_config();
    }
}

void WizardToolOffsetStep::register_callbacks() {
    spdlog::debug("[{}] Registering callbacks", get_name());
    lv_xml_register_event_cb(nullptr, "on_start_tool_offset", on_start_tool_offset_clicked);
    lv_xml_register_event_cb(nullptr, "on_cancel_tool_offset", on_cancel_tool_offset_clicked);
    lv_xml_register_event_cb(nullptr, "on_save_tool_offset", on_save_tool_offset_clicked);
}

// ============================================================================
// Screen Creation / Cleanup
// ============================================================================

lv_obj_t* WizardToolOffsetStep::create(lv_obj_t* parent) {
    spdlog::debug("[{}] Creating tool offset screen", get_name());

    if (screen_root_) {
        spdlog::warn("[{}] Screen pointer not null - cleanup may not have been called properly",
                     get_name());
        screen_root_ = nullptr; // wizard framework owns the widget tree
    }

    screen_root_ = static_cast<lv_obj_t*>(lv_xml_create(parent, "wizard_tool_offset", nullptr));
    if (!screen_root_) {
        spdlog::error("[{}] Failed to create screen from XML", get_name());
        return nullptr;
    }

    // Footer: Skip until the macro has succeeded, Next afterwards (re-entry)
    lv_subject_set_int(&wizard_show_skip, calibration_complete_ ? 0 : 1);
    lv_subject_set_int(&connection_test_passed, calibration_complete_ ? 1 : 0);

    fetch_macro_description();

    spdlog::debug("[{}] Screen created successfully", get_name());
    return screen_root_;
}

void WizardToolOffsetStep::reset_ui_state() {
    if (!subjects_initialized_) {
        return;
    }
    lv_subject_set_int(&started_, 0);
    lv_subject_set_int(&active_, 0);
    lv_subject_set_int(&complete_, 0);
    lv_subject_set_int(&saved_, 0);
    log_lines_.clear();
    lv_subject_copy_string(&log_, "");
    lv_subject_copy_string(&status_, "Ready to calibrate");
}

void WizardToolOffsetStep::cleanup() {
    spdlog::debug("[{}] Cleaning up resources", get_name());

    // Backing out mid-calibration: the macro keeps running the printer
    // otherwise (it blocks the gcode queue), so stop it like Input Shaper does.
    if (calibration_active_) {
        abort_in_progress_calibration();
    }

    lifetime_.invalidate();
    elapsed_.cancel();
    unsubscribe_console();

    // Keep the completion state across back → forward so re-entry shows the
    // result; anything unfinished starts fresh.
    if (!calibration_complete_) {
        reset_ui_state();
    } else if (subjects_initialized_) {
        lv_subject_set_int(&active_, 0);
    }
    calibration_active_ = false;

    // Reset footer subjects for next step
    lv_subject_set_int(&wizard_show_skip, 0);
    lv_subject_set_int(&connection_test_passed, 1);

    screen_root_ = nullptr;
    spdlog::debug("[{}] Cleanup complete", get_name());
}

// ============================================================================
// Skip Logic
// ============================================================================

bool WizardToolOffsetStep::printer_supports_calibration() {
    const auto& hw = get_printer_state().get_discovery();
    return hw.has_tool_changer() && hw.has_macro(CALIBRATE_MACRO);
}

bool WizardToolOffsetStep::tools_already_calibrated() {
    // klipper-toolchanger semantics: the first tool is the reference (all
    // zeros) and an uncalibrated printer reports zeros on every other tool
    // too. Any non-zero offset on T1+ means a calibration was saved before —
    // by this UI, the console, or the printer's previous software.
    const auto& tools = helix::ToolState::instance().tools();
    for (size_t i = 1; i < tools.size(); ++i) {
        const auto& t = tools[i];
        if (t.gcode_x_offset != 0.0f || t.gcode_y_offset != 0.0f || t.gcode_z_offset != 0.0f) {
            return true;
        }
    }
    return false;
}

bool WizardToolOffsetStep::should_skip(const helix::wizard::StepContext& ctx) const {
    (void)ctx; // capability-gated: the macro is the printer's declaration
    if (!printer_supports_calibration()) {
        spdlog::debug("[{}] No toolchanger + {} macro, skipping step", get_name(),
                      CALIBRATE_MACRO);
        return true;
    }
    if (!calibration_complete_ && tools_already_calibrated()) {
        spdlog::info("[{}] Tools already carry offsets — skipping step", get_name());
        return true;
    }
    return false;
}

// ============================================================================
// Calibration flow
// ============================================================================

void WizardToolOffsetStep::start_calibration() {
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
    lv_subject_set_int(&saved_, 0);

    // Footer: neither Skip nor Next while the printer is busy
    lv_subject_set_int(&wizard_show_skip, 0);
    lv_subject_set_int(&connection_test_passed, 0);

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
        lifetime_.bg_cb("WizardToolOffsetStep::calibrate_done",
                        [this]() { on_calibration_finished(true, ""); }),
        lifetime_.bg_cb("WizardToolOffsetStep::calibrate_error",
                        [this](const MoonrakerError& err) {
                            on_calibration_finished(false, err.message);
                        }),
        IMoonrakerAPI::PRE_START_MACRO_TIMEOUT_MS);
}

void WizardToolOffsetStep::on_calibration_finished(bool ok, const std::string& error) {
    elapsed_.cancel();
    unsubscribe_console();
    calibration_active_ = false;
    lv_subject_set_int(&active_, 0);

    if (ok) {
        spdlog::info("[{}] {} finished", get_name(), CALIBRATE_MACRO);
        calibration_complete_ = true;
        lv_subject_set_int(&complete_, 1);
        lv_subject_copy_string(&status_, lv_tr("Calibration complete!"));
        lv_subject_set_int(&wizard_show_skip, 0);
        lv_subject_set_int(&connection_test_passed, 1);
        return;
    }

    spdlog::error("[{}] {} failed: {}", get_name(), CALIBRATE_MACRO, error);
    lv_subject_set_int(&started_, 0);
    lv_subject_copy_string(&status_, error.empty() ? lv_tr("Calibration failed") : error.c_str());
    // Let the user retry (Start is visible again) or move on
    lv_subject_set_int(&wizard_show_skip, 1);
    lv_subject_set_int(&connection_test_passed, 1);
}

bool WizardToolOffsetStep::abort_in_progress_calibration() {
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
                spdlog::debug("[Wizard Tool Offset] M112 sent, restarting firmware");
                api->restart_firmware([]() {}, [](const MoonrakerError& err) {
                    spdlog::error("[Wizard Tool Offset] Firmware restart failed: {}",
                                  err.message);
                });
            },
            [](const MoonrakerError& err) {
                spdlog::error("[Wizard Tool Offset] Emergency stop failed: {}", err.message);
            });
    }

    reset_ui_state();
    lv_subject_copy_string(&status_, lv_tr("Cancelled"));
    lv_subject_set_int(&wizard_show_skip, 1);
    lv_subject_set_int(&connection_test_passed, 0);
    return true;
}

void WizardToolOffsetStep::save_config() {
    auto* api = get_moonraker_api();
    if (!api || !calibration_complete_) {
        return;
    }
    spdlog::info("[{}] Saving config (will restart Klipper)", get_name());
    helix::ui::begin_expected_klippy_restart("Configuration saved - restarting");
    lv_subject_set_int(&saved_, 1);
    api->execute_gcode(
        "SAVE_CONFIG",
        lifetime_.bg_cb("WizardToolOffsetStep::save_config_done",
                        [this]() {
                            lv_subject_copy_string(&status_,
                                                   lv_tr("Offsets saved - Klipper is restarting"));
                        }),
        lifetime_.bg_cb("WizardToolOffsetStep::save_config_error",
                        [this](const MoonrakerError& err) {
                            spdlog::error("[{}] SAVE_CONFIG failed: {}", get_name(), err.message);
                            lv_subject_set_int(&saved_, 0);
                            lv_subject_copy_string(&status_, err.message.c_str());
                        }));
}

// ============================================================================
// Console mirror + macro description
// ============================================================================

void WizardToolOffsetStep::append_log_line(const std::string& raw) {
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

void WizardToolOffsetStep::subscribe_console() {
    auto* client = get_moonraker_client();
    if (!client || console_subscribed_) {
        return;
    }
    // WS thread → bg_cb queues the body to the main thread (threading rule 1)
    client->register_method_callback(
        "notify_gcode_response", CONSOLE_HANDLER,
        lifetime_.bg_cb("WizardToolOffsetStep::console", [this](const nlohmann::json& msg) {
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

void WizardToolOffsetStep::unsubscribe_console() {
    if (!console_subscribed_) {
        return;
    }
    if (auto* client = get_moonraker_client()) {
        client->unregister_method_callback("notify_gcode_response", CONSOLE_HANDLER);
    }
    console_subscribed_ = false;
}

void WizardToolOffsetStep::fetch_macro_description() {
    auto* client = get_moonraker_client();
    if (!client) {
        return;
    }
    // printer.gcode.help → {"CMD": "description", ...}; the macro's own
    // `description:` is the instruction text ("remove the build plate", ...).
    client->send_jsonrpc(
        "printer.gcode.help", nlohmann::json::object(),
        lifetime_.bg_cb("WizardToolOffsetStep::gcode_help", [this](const nlohmann::json& resp) {
            const nlohmann::json& result =
                resp.contains("result") ? resp["result"] : resp;
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
