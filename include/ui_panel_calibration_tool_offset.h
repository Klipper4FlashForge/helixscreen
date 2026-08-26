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
 * Every row — the reference and each tool — is the same shape, and its whole
 * appearance follows one int state subject (RowState below). That is what lets
 * a run happen in place instead of on a second screen: the measuring row
 * highlights, later rows read Queued, finished rows show their numbers.
 *
 * - tool_offset_cal_status (string) - one-line status
 * - tool_offset_cal_log (string) - last few notify_gcode_response lines
 * - tool_offset_cal_hint (string) - the macro's description
 * - tool_offset_cal_active / _complete (int) - run in progress / run succeeded
 * - tool_offset_cal_row_visible_N (int, N=0..3) - row shown (tool exists)
 * - tool_offset_cal_state_N (int) - RowState for tool N
 * - tool_offset_cal_state_text_N (string) - "Not calibrated" / "Queued" / ...
 * - tool_offset_cal_sub_N (string) - second line under that ("probing... 12s")
 * - tool_offset_cal_x_N / _y_N / _z_N (string) - the three measured numbers
 * - tool_offset_cal_z_odd_N (int) - 1 when Z is outside the expected gap
 * - tool_offset_cal_station_state (int) - RowState for the reference row
 * - tool_offset_cal_station_state_text / _sub (string) - same, for the reference
 * - tool_offset_cal_station_x / _y / _z (string) - the station bore's position
 * - tool_offset_cal_save_pending (int) - Klipper has unsaved calibration
 * - tool_offset_cal_error (int) - 1 while the refusal card is up
 * - tool_offset_cal_error_title / _text / _fix (string) - that card's content
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
 * Results come from `ff_tool <n>` / `ff_tool_offset`, re-read after every step,
 * rather than from parsing the console block the firmware prints. What the
 * rows show is each nozzle's own machine position (`nozzle_x`, `nozzle_y`) and
 * its height over the station (`nozzle_z - station_z + z_adjust`), against the
 * reference row's `station_x/y/z` directly above. The differences a toolchange
 * applies are `nozzle_x/y` minus the base tool's, which would put an
 * unexplained pair of zeroes on T0's row; showing the positions themselves
 * says the same thing without that trap.
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

    /// Everything a row looks like follows from this one int.
    enum RowState : int {
        ROW_NONE = 0,      ///< never measured, or the last attempt was refused
        ROW_QUEUED = 1,    ///< part of the current run, not reached yet
        ROW_MEASURING = 2, ///< the machine is probing this row right now
        ROW_OK = 3         ///< measured; the row shows its three numbers
    };

    /// The gap guard the firmware itself applies, mirrored so a Z that passed
    /// but sits at the edge still reads as suspicious. A healthy machine
    /// measures about 3.15 mm.
    static constexpr double GAP_MIN_MM = 1.5;
    static constexpr double GAP_MAX_MM = 5.0;

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
    /// Paint one row's state text, sub-line and colour from a RowState
    void set_row_state(int step, RowState state, const std::string& sub = "");
    /// Copy a tool's stored numbers into its three value subjects
    void set_row_values(int tool);
    void set_station_values();
    /// True while `step` still belongs to the run in flight — refreshed
    /// printer state must not overwrite a Queued or Measuring row.
    bool is_step_pending(int step) const;
    void show_error(int step, const std::string& message);
    void clear_error();
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
    /// The step the refusal card is about (set when a step errors).
    int last_failed_step_ = -2;
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
    lv_subject_t save_pending_;

    // Per-tool rows
    lv_subject_t row_visible_[MAX_TOOLS];
    lv_subject_t row_state_[MAX_TOOLS];
    lv_subject_t row_state_text_[MAX_TOOLS];
    lv_subject_t row_sub_[MAX_TOOLS];
    lv_subject_t row_x_[MAX_TOOLS];
    lv_subject_t row_y_[MAX_TOOLS];
    lv_subject_t row_z_[MAX_TOOLS];
    lv_subject_t row_z_odd_[MAX_TOOLS];
    char row_state_text_buffer_[MAX_TOOLS][48];
    char row_sub_buffer_[MAX_TOOLS][80];
    char row_x_buffer_[MAX_TOOLS][16];
    char row_y_buffer_[MAX_TOOLS][16];
    char row_z_buffer_[MAX_TOOLS][16];

    // Reference row — same shape, one instance
    lv_subject_t station_state_;
    lv_subject_t station_state_text_;
    lv_subject_t station_sub_;
    lv_subject_t station_x_;
    lv_subject_t station_y_;
    lv_subject_t station_z_;
    char station_state_text_buffer_[48] = "";
    char station_sub_buffer_[80] = "";
    char station_x_buffer_[16] = "";
    char station_y_buffer_[16] = "";
    char station_z_buffer_[16] = "";

    // Refusal card
    lv_subject_t error_;
    lv_subject_t error_title_;
    lv_subject_t error_text_;
    lv_subject_t error_fix_;
    char error_title_buffer_[96] = "";
    char error_text_buffer_[384] = "";
    char error_fix_buffer_[128] = "";

    /// Per tool: nozzle_x, nozzle_y, and the Z gap over the station. Read from
    /// ff_tool/ff_tool_offset after every step. Valid iff values_valid_.
    double values_[MAX_TOOLS][3] = {};
    bool values_valid_[MAX_TOOLS] = {false, false, false, false};
    /// station_x/y/z from ff_tool_offset; the reference pass has run when set.
    bool station_known_ = false;
    double station_pos_[3] = {};
    SubjectManager subjects_;

    std::deque<std::string> log_lines_;
    helix::ui::ElapsedLabelTimer elapsed_;
};

/// Register the Advanced-panel row click callback ("on_tool_offset_row_clicked")
void init_tool_offset_row_handler();

/// Singleton accessor (lazily created, destroyed via StaticPanelRegistry)
ToolOffsetCalibrationPanel& get_global_tool_offset_cal_panel();
