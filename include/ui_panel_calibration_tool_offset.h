// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "overlay_base.h"
#include "ui_timer_guard.h"

#include "lvgl/lvgl.h"

#include <deque>
#include <string>
#include <vector>

/**
 * @file ui_panel_calibration_tool_offset.h
 * @brief On-demand tool offset calibration overlay for tool changers
 *
 * The overlay counterpart of the wizard's Tool Offsets step
 * (ui_wizard_tool_offset.h): shown from the Controls / Advanced calibration
 * entry points when the printer exposes a klipper-toolchanger `toolchanger`
 * object AND a `CALIBRATE_TOOL_OFFSETS` macro. On such printers the macro owns
 * the whole procedure (tool pickup, probing, storing each tool's offsets), so
 * the paper-test Z-offset panel does not apply — the calibration buttons open
 * this overlay instead.
 *
 * The macro's `description:` (from printer.gcode.help) is the on-screen
 * instruction and the confirmation prompt, exactly as in the wizard. Unlike
 * the wizard — whose SAVE_CONFIG runs when setup finishes — a standalone run
 * has nobody to persist the staged result, so a successful calibration offers
 * a Save button that sends SAVE_CONFIG (which restarts Klipper).
 *
 * ## Subject Bindings:
 *
 * - tool_offset_cal_status (string) - one-line status
 * - tool_offset_cal_log (string) - last few notify_gcode_response lines
 * - tool_offset_cal_hint (string) - the macro's description
 * - tool_offset_cal_active / _complete (int) - run in progress / Save visible
 * - tool_offset_cal_row_visible_N (int, N=0..3) - row shown (tool exists)
 * - tool_offset_cal_text_N (string) - per-tool offsets line ("X --  Y --  Z --")
 * - tool_offset_cal_spin_N (int) - 1 while tool N is the one probing
 *
 * ## Macro contract
 *
 * The panel drives one tool at a time: `CALIBRATE_TOOL_OFFSETS TOOL=<n>
 * PLATE_REMOVED=1`. Between tools it is plain UI state, so Stop simply stops
 * issuing commands — no emergency stop needed (that remains the escape hatch
 * for backing out mid-probe). Each tool's result is read from the macro's
 * console output, one line per tool: `T<n>: offset = (<x>, <y>, <z>)`.
 */
class ToolOffsetCalibrationPanel : public OverlayBase {
  public:
    /// Macro the panel runs — klipper-toolchanger's documented convention.
    static constexpr const char* CALIBRATE_MACRO = "CALIBRATE_TOOL_OFFSETS";

    /// Fixed subject slots; rows beyond the printer's tool count stay hidden.
    static constexpr int MAX_TOOLS = 4;

    ToolOffsetCalibrationPanel();
    ~ToolOffsetCalibrationPanel() override;

    // Non-copyable, non-movable (lv_subject_t members hold observer lists)
    ToolOffsetCalibrationPanel(const ToolOffsetCalibrationPanel&) = delete;
    ToolOffsetCalibrationPanel& operator=(const ToolOffsetCalibrationPanel&) = delete;
    ToolOffsetCalibrationPanel(ToolOffsetCalibrationPanel&&) = delete;
    ToolOffsetCalibrationPanel& operator=(ToolOffsetCalibrationPanel&&) = delete;

    // === OverlayBase Interface ===
    void init_subjects() override;
    lv_obj_t* create(lv_obj_t* parent) override;
    const char* get_name() const override {
        return "Tool Offset Calibration";
    }
    void on_activate() override;
    void on_deactivate() override;
    void cleanup() override;

    /// Push the overlay onto the navigation stack (create() must have run)
    void show();

    /// Printer exposes a tool changer and the calibration macro
    static bool printer_supports_calibration();

    /// Calibrate every tool in sequence (no-op while a run is in flight)
    void start_calibration();

    /// Calibrate a single tool (no-op while a run is in flight)
    void start_calibration_for_tool(int tool);

    /// Stop after the tool currently probing finishes (clean, no M112)
    void request_stop();

    /**
     * @brief Abort an in-progress calibration via M112 + firmware_restart.
     *
     * The macro blocks Klipper's gcode queue, so M112 is the only reliable
     * stop (same rationale as the wizard step).
     *
     * @return true if a calibration was aborted; false if nothing was running
     */
    bool abort_in_progress_calibration();

    /// Send SAVE_CONFIG to persist a completed calibration (restarts Klipper)
    void save_calibration();

    /// Append one console line to the on-screen log (main thread only)
    void append_log_line(const std::string& line);

    // State / subject access for tests
    bool is_calibration_active() const {
        return calibration_active_;
    }
    bool is_calibration_complete() const {
        return calibration_complete_;
    }
    lv_subject_t* get_hint_subject() {
        return &hint_;
    }
    lv_subject_t* get_status_subject() {
        return &status_;
    }
    lv_subject_t* get_log_subject() {
        return &log_;
    }

  private:
    void begin_run(std::vector<int> tools);
    void send_next_tool();
    void on_tool_finished(bool ok, const std::string& error);
    void finish_run(bool ok, const std::string& error);
    void refresh_tool_rows();
    void confirm_and_run(std::vector<int> tools);
    void fetch_macro_description();
    void subscribe_console();
    void unsubscribe_console();
    void reset_ui_state();

    // XML event trampolines
    static void on_start_clicked(lv_event_t* e);
    static void on_tool_clicked(lv_event_t* e);
    static void on_cancel_clicked(lv_event_t* e);
    static void on_save_clicked(lv_event_t* e);

    bool calibration_active_ = false;
    bool calibration_complete_ = false;
    bool console_subscribed_ = false;
    bool stop_requested_ = false;
    int current_tool_ = -1;
    std::vector<int> run_queue_;

    char status_buffer_[128] = "";
    char log_buffer_[512] = "";
    char hint_buffer_[256] = "";
    lv_subject_t status_;
    lv_subject_t log_;
    lv_subject_t hint_;
    lv_subject_t started_;
    lv_subject_t active_;
    lv_subject_t complete_;
    lv_subject_t row_visible_[MAX_TOOLS];
    lv_subject_t row_text_[MAX_TOOLS];
    lv_subject_t row_spin_[MAX_TOOLS];
    char row_text_buffer_[MAX_TOOLS][64];
    /// This session's measured offsets, keyed by tool (valid iff measured_valid_)
    double measured_[MAX_TOOLS][3] = {};
    bool measured_valid_[MAX_TOOLS] = {false, false, false, false};
    SubjectManager subjects_;

    std::deque<std::string> log_lines_;
    helix::ui::ElapsedLabelTimer elapsed_;
};

/// Register the Advanced-panel row click callback ("on_tool_offset_row_clicked")
void init_tool_offset_row_handler();

/// Singleton accessor (lazily created, destroyed via StaticPanelRegistry)
ToolOffsetCalibrationPanel& get_global_tool_offset_cal_panel();
