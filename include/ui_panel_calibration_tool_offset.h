// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui_timer_guard.h"

#include "lvgl/lvgl.h"
#include "overlay_base.h"

#include <deque>
#include <string>
#include <vector>

#include "hv/json.hpp"

/**
 * @file ui_panel_calibration_tool_offset.h
 * @brief On-demand tool offset calibration overlay for tool changers
 *
 * Shown from the Controls / Advanced calibration entry points on a printer
 * that can MEASURE its own tool offsets - helix::tool_offset_calibration
 * decides which those are. The paper-test Z-offset panel does not apply to a
 * tool changer, so the calibration buttons open this overlay instead.
 *
 * This file names no firmware. The commands to send, the objects to read, the
 * numbers those objects yield, and the words describing them all come from
 * helix::tool_offset_calibration; adding a second firmware must not touch this
 * panel. What the numbers MEAN is likewise not this panel's business - it
 * renders them under the captions the provider supplies and interprets
 * neither.
 *
 * ## Why the panel drives the steps
 *
 * The provider hands back one pass at a time (a reference pass, then a pass
 * per tool) rather than an all-in-one command, and the panel runs them in
 * sequence. That is what keeps Stop clean: between passes this is plain UI
 * state, so stopping means simply not sending the next one. The M112 abort
 * remains the escape hatch for backing out mid-probe, because a probing macro
 * blocks Klipper's gcode queue.
 *
 * Results are re-read from the printer after every pass rather than parsed out
 * of the console text the firmware prints.
 *
 * ## Subject Bindings
 *
 * Every row - the reference and each tool - is the same shape, and its whole
 * appearance follows one int state subject (RowState below). That is what lets
 * a run happen in place instead of on a second screen: the measuring row
 * highlights, later rows read Queued, finished rows show their numbers.
 *
 * - tool_offset_cal_status (string) - one-line status
 * - tool_offset_cal_log (string) - last few notify_gcode_response lines
 * - tool_offset_cal_hint (string) - the firmware's own instruction, when it
 *   publishes one (see tool_offset_calibration::hint_command)
 * - tool_offset_cal_active / _complete (int) - run in progress / run succeeded
 * - tool_offset_cal_caption (string) - what the numbers mean, from the provider
 * - tool_offset_cal_has_reference (int) - 1 when this firmware shows a
 *   reference row; 0 hides that whole section
 * - tool_offset_cal_row_visible_N (int, N=0..3) - row shown (tool exists)
 * - tool_offset_cal_state_N (int) - RowState for tool N
 * - tool_offset_cal_state_text_N (string) - "Not calibrated" / "Queued" / ...
 * - tool_offset_cal_sub_N (string) - second line under that ("probing... 12s")
 * - tool_offset_cal_x_N / _y_N / _z_N (string) - the three measured numbers
 * - tool_offset_cal_col_x / _col_y / _col_z (string) - column captions
 * - tool_offset_cal_station_state (int) - RowState for the reference row
 * - tool_offset_cal_station_state_text / _sub (string) - same, for the reference
 * - tool_offset_cal_station_x / _y / _z (string) - the reference's position
 * - tool_offset_cal_save_pending (int) - Klipper has unsaved calibration
 *
 * A refusal has no subject: it is a one-time event shown in a dismissible
 * alert modal, not a panel state.
 */
class ToolOffsetCalibrationPanel : public OverlayBase {
  public:
    // The command names, the objects to read, and what the numbers mean all
    // live in helix::tool_offset_calibration. Nothing in this panel names a
    // firmware - adding one must not touch this file.

    /// Fixed subject slots; rows beyond the printer's tool count stay hidden.
    static constexpr int MAX_TOOLS = 4;

    /// Queue sentinel for the reference pass, on firmwares that show one as a
    /// row of its own (see Presentation::has_reference_row).
    static constexpr int STATION_STEP = -1;

    /// Queue sentinel for "the whole machine". The calibration is one firmware
    /// command covering every tool, so a run is a single step rather than a
    /// pass per tool.
    static constexpr int RUN_STEP = -3;

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

    /// Halt a run. There is no graceful form - the run is one firmware
    /// command that blocks Klipper's queue - so this IS the emergency stop,
    /// and it restarts the firmware. See the definition.
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

    /// Confirm, then persist a completed calibration (restarts Klipper)
    void save_calibration();

    /// Tools that currently hold an offset worth persisting
    [[nodiscard]] std::vector<int> calibrated_tools() const;

    /// The durable write itself, once the user has accepted the restart
    void send_save_config();

    /// Re-read the printer's offsets and configfile, and repaint the rows
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
    /// Put a refusal in a dismissible modal (see the doc comment on show_error)
    void show_error(int step, const std::string& message);
    void confirm_and_run(std::vector<int> steps);
    void fetch_macro_description();
    void subscribe_console();
    void unsubscribe_console();
    void reset_ui_state();

    // XML event trampolines
    static void on_start_clicked(lv_event_t* e);
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

    /// Per tool: nozzle_x, nozzle_y, and the Z gap over the station. Read from
    /// the printer after every step. Valid iff values_valid_.
    double values_[MAX_TOOLS][3] = {};
    bool values_valid_[MAX_TOOLS] = {false, false, false, false};
    /// The reference fixture's position; the reference pass has run when set.
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
