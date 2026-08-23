// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui_timer_guard.h"

#include "async_lifetime_guard.h"
#include "lvgl/lvgl.h"
#include "wizard_step.h"

#include <deque>
#include <string>

/**
 * @file ui_wizard_tool_offset.h
 * @brief Wizard tool offset calibration step for tool changers
 *
 * Shown when the printer exposes a klipper-toolchanger `toolchanger` object AND
 * a `CALIBRATE_TOOL_OFFSETS` macro — the convention from klipper-toolchanger's
 * examples/calibrate-offsets.cfg. The macro owns the whole procedure (heating,
 * tool pickup, probing, storing each tool's gcode_x/y/z_offset); this step only
 * runs it, mirrors Klipper's console output while it runs, and offers
 * SAVE_CONFIG when it finishes. No printer-specific knowledge lives here: the
 * macro's `description:` (from printer.gcode.help) is the on-screen instruction.
 *
 * ## Subject Bindings:
 *
 * - wizard_tool_offset_status (string) - one-line status
 * - wizard_tool_offset_log (string) - last few notify_gcode_response lines
 * - wizard_tool_offset_hint (string) - the macro's description
 * - wizard_tool_offset_started / _active / _complete / _saved (int) - visibility
 *
 * ## Validation:
 *
 * Footer shows "Skip" until the macro succeeds, then "Next".
 */
class WizardToolOffsetStep : public helix::wizard::Step {
  public:
    /// Macro the step runs — klipper-toolchanger's documented convention.
    static constexpr const char* CALIBRATE_MACRO = "CALIBRATE_TOOL_OFFSETS";

    // helix::wizard::Step interface
    helix::wizard::StepId id() const override {
        return helix::wizard::StepId::ToolOffset;
    }
    const char* component_name() const override {
        return "wizard_tool_offset";
    }
    const char* log_name() const override {
        return "Wizard Tool Offset";
    }
    bool should_skip(const helix::wizard::StepContext& ctx) const override;

    WizardToolOffsetStep();
    ~WizardToolOffsetStep();

    // Non-copyable, non-movable (lv_subject_t members hold observer lists)
    WizardToolOffsetStep(const WizardToolOffsetStep&) = delete;
    WizardToolOffsetStep& operator=(const WizardToolOffsetStep&) = delete;
    WizardToolOffsetStep(WizardToolOffsetStep&&) = delete;
    WizardToolOffsetStep& operator=(WizardToolOffsetStep&&) = delete;

    void init_subjects() override;
    void register_callbacks() override;
    lv_obj_t* create(lv_obj_t* parent) override;
    void cleanup() override;
    bool is_validated() const override {
        return calibration_complete_;
    }

    /// Printer exposes a tool changer and the calibration macro
    static bool printer_supports_calibration();

    /// Some tool other than the first already reports a non-zero gcode offset
    /// (ToolState) — a calibration was saved before, by whatever software.
    static bool tools_already_calibrated();

    /// Run CALIBRATE_TOOL_OFFSETS (no-op while one is in flight)
    void start_calibration();

    /**
     * @brief Abort an in-progress calibration via M112 + firmware_restart.
     *
     * The macro blocks Klipper's gcode queue, so M112 is the only reliable
     * stop. Suppresses the recovery / disconnect modals, invalidates async
     * tokens and resets the UI to its Start-able state.
     *
     * @return true if a calibration was aborted; false if nothing was running
     */
    bool abort_in_progress_calibration();

    /// Persist the new offsets (SAVE_CONFIG restarts Klipper)
    void save_config();

    /// Append one console line to the on-screen log (main thread only)
    void append_log_line(const std::string& line);

    const char* get_name() const {
        return "Wizard Tool Offset";
    }

    // State / subject access for tests
    bool is_calibration_complete() const {
        return calibration_complete_;
    }
    bool is_calibration_active() const {
        return calibration_active_;
    }
    lv_subject_t* get_status_subject() {
        return &status_;
    }
    lv_subject_t* get_log_subject() {
        return &log_;
    }
    lv_subject_t* get_hint_subject() {
        return &hint_;
    }
    lv_subject_t* get_started_subject() {
        return &started_;
    }
    lv_subject_t* get_active_subject() {
        return &active_;
    }
    lv_subject_t* get_complete_subject() {
        return &complete_;
    }
    lv_obj_t* get_screen_root() const {
        return screen_root_;
    }

  private:
    void on_calibration_finished(bool ok, const std::string& error);
    void fetch_macro_description();
    void subscribe_console();
    void unsubscribe_console();
    void reset_ui_state();

    lv_obj_t* screen_root_ = nullptr;
    bool subjects_initialized_ = false;
    bool calibration_complete_ = false;
    bool calibration_active_ = false;
    bool console_subscribed_ = false;

    char status_buffer_[128] = "Ready to calibrate";
    char log_buffer_[512] = "";
    char hint_buffer_[256] = "";
    lv_subject_t status_;
    lv_subject_t log_;
    lv_subject_t hint_;
    lv_subject_t started_;
    lv_subject_t active_;
    lv_subject_t complete_;
    lv_subject_t saved_;

    std::deque<std::string> log_lines_;
    helix::ui::ElapsedLabelTimer elapsed_;
    helix::AsyncLifetimeGuard lifetime_;
};

/// Singleton accessor (lazily created, destroyed via StaticPanelRegistry)
WizardToolOffsetStep* get_wizard_tool_offset_step();
