// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ui_observer_guard.h"

#include "lvgl/lvgl.h"

#include <functional>
#include <string>
#include <vector>

namespace helix {
namespace ui {

/// Lightweight sparkline widget that renders a PerformanceState ring buffer
/// as a line graph. Observes "perf_history_tick" and redraws on each tick.
///
/// Usage (C++):
///   auto* obj = HelixSparkline::create(parent, "host_cpu_pct");
///
/// Usage (XML):
///   <helix_sparkline source="host_cpu_pct" style_line_color="#accent" />
class HelixSparkline {
  public:
    /// The most recent minute of recorded heater temperatures, in Celsius.
    static std::vector<float> temperature_history(const std::string& heater);

    /// XML-facing global heater chart; chamber resolution also supports sensors.
    static lv_obj_t* create_heater(lv_obj_t* parent, bool chamber);

    using HistoryReader = std::function<std::vector<float>()>;

    /// Create a sparkline over caller-provided history. The caller invalidates
    /// the returned object when samples change. Reader runs on the UI thread.
    static lv_obj_t* create(lv_obj_t* parent, HistoryReader reader);

    /// Replace the data provider of an existing sparkline (main thread only).
    static void set_history_reader(lv_obj_t* obj, HistoryReader reader);

    /// Create a sparkline bound to a PerformanceState ring buffer.
    /// `source` is the ring-buffer name (e.g. "host_cpu_pct").
    /// Returns the LVGL object (caller may set size/style on it).
    static lv_obj_t* create(lv_obj_t* parent, const std::string& source);

  private:
    explicit HelixSparkline(HistoryReader reader);
    ~HelixSparkline() = default;
    HelixSparkline(const HelixSparkline&) = delete;
    HelixSparkline& operator=(const HelixSparkline&) = delete;

    static void on_draw(lv_event_t* e);
    static void on_delete(lv_event_t* e);
    void invalidate_self();

    lv_obj_t* obj_ = nullptr;
    HistoryReader history_reader_;
    ObserverGuard tick_observer_; // dtor calls reset() automatically (L085)
};

/// Register the helix_sparkline custom widget with the helix-xml engine.
/// Called once from Application::register_widgets().
void register_helix_sparkline_widget();

} // namespace ui
} // namespace helix
