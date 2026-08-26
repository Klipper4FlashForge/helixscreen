// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "overlay_base.h"
#include "ui_timer_guard.h"

#include "hv/json.hpp"
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
 * - tool_offset_cal_station_text (string) - reference row's state line
 * - tool_offset_cal_station_spin (int) - 1 while the reference is probing
 * - tool_offset_cal_save_pending (int) - Klipper has unsaved calibration
 * - tool_offset_cal_warning (string) - incomplete-calibration note
 * - tool_offset_cal_has_warning (int) - 1 when that note is non-empty
 *
 * ## Command contract
 *
 * The firmware exposes klipper-toolchanger's command names, so the panel
 * drives the sequence itself one step at a time rather than running the
 * all-in-one macro:
 *
 * - the reference pass is `TOOL_LOCATE_SENSOR`, which needs an EMPTY
 *   carriage and must have run at least once — the gap guard that catches a
 *   mis-triggered Z compares against `station_z`;
 * - a tool pass is `SELECT_TOOL T=<n>` then `TOOL_CALIBRATE_TOOL_OFFSET`,
 *   which takes no arguments and measures whatever is mounted.
 *
 * Driving it stepwise is what makes Stop clean: between steps this is plain
 * UI state, so stopping just means not sending the next command (the M112
 * abort remains the escape hatch for backing out mid-probe).
 *
 * Results are read from the console block the firmware prints at the end of
 * each pass — `T<n>: dX <x>  dY <y>  Z <z>`, the offsets a toolchange
 * actually applies. The earlier `T<n>: offset = (...)` line is the raw
 * station-bore centre in machine coordinates, NOT an offset, and is only
 * mirrored into the log. dX/dY are differences against the base tool (T0),
 * so its own are zero by definition; Z is absolute
 * (`nozzle_z - station_z + z_adjust`) and is not zero for the base tool.
 */
class ToolOffsetCalibrationPanel : public OverlayBase {
  public:
    /// All-in-one macro. The panel does NOT run it (it drives the steps
    /// itself, for a clean Stop), but its presence is the capability gate and
    /// its `description:` is the confirmation prompt.
    static constexpr const char* CALIBRATE_MACRO = "CALIBRATE_TOOL_OFFSETS";
    /// Reference pass — empty carriage, establishes station_x/y/z.
    static constexpr const char* LOCATE_CMD = "TOOL_LOCATE_SENSOR";
    /// One tool — no arguments, measures whatever is mounted.
    static constexpr const char* CALIBRATE_TOOL_CMD = "TOOL_CALIBRATE_TOOL_OFFSET";

    /// Fixed subject slots; rows beyond the printer's tool count stay hidden.
    static constexpr int MAX_TOOLS = 4;

    /// Queue sentinel for the reference pass (it is not a tool index).
    static constexpr int STATION_STEP = -1;

    /// Toolchanger's `offset_base` — the tool dX/dY are measured against.
    /// Config-only in the firmware and defaulted to 0, so not queryable.
    static constexpr int BASE_TOOL = 0;

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

    /// Run the reference pass on its own (TOOL_LOCATE_SENSOR)
    void start_locate_sensor();

    /// Re-read ff_tool/ff_tool_offset/configfile and repaint the rows
    void refresh_from_printer();

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
    void begin_run(std::vector<int> steps);
    void send_next_step();
    void on_step_finished(bool ok, const std::string& error);
    void finish_run(bool ok, const std::string& error);
    void refresh_tool_rows();
    void apply_printer_state(const nlohmann::json& status);
    void set_row_text(int tool);
    void confirm_and_run(std::vector<int> steps);
    void fetch_macro_description();
    void subscribe_console();
    void unsubscribe_console();
    void reset_ui_state();

    // XML event trampolines
    static void on_start_clicked(lv_event_t* e);
    static void on_tool_clicked(lv_event_t* e);
    static void on_locate_clicked(lv_event_t* e);
    static void on_cancel_clicked(lv_event_t* e);
    static void on_save_clicked(lv_event_t* e);

    bool calibration_active_ = false;
    bool calibration_complete_ = false;
    bool console_subscribed_ = false;
    bool stop_requested_ = false;
    /// Step being executed: a tool index, or STATION_STEP, or -2 for none.
    int current_step_ = -2;
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
    lv_subject_t station_spin_;
    lv_subject_t station_text_;
    lv_subject_t save_pending_;
    lv_subject_t warning_;
    lv_subject_t has_warning_;
    char row_text_buffer_[MAX_TOOLS][64];
    char station_text_buffer_[96] = "";
    char warning_buffer_[128] = "";
    /// Offsets a toolchange applies, keyed by tool: dX, dY, absolute Z.
    /// Sourced from the firmware's report block during a run, and from
    /// ff_tool/ff_tool_offset on open. Valid iff applied_valid_.
    double applied_[MAX_TOOLS][3] = {};
    bool applied_valid_[MAX_TOOLS] = {false, false, false, false};
    /// station_z from ff_tool_offset; the reference pass has run when set.
    bool station_known_ = false;
    double station_z_ = 0.0;
    SubjectManager subjects_;

    std::deque<std::string> log_lines_;
    helix::ui::ElapsedLabelTimer elapsed_;
};

/// Register the Advanced-panel row click callback ("on_tool_offset_row_clicked")
void init_tool_offset_row_handler();

/// Singleton accessor (lazily created, destroyed via StaticPanelRegistry)
ToolOffsetCalibrationPanel& get_global_tool_offset_cal_panel();
