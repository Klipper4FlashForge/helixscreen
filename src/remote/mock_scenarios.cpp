// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mock_scenarios.h"

#include "http_executor.h"

#include <spdlog/spdlog.h>

#include <chrono>
#include <lvgl.h>
#include <thread>

// LVGL XML subject lookup
#include "helix-xml/src/xml/lv_xml.h"

namespace helix {

// Helper: set an INT subject by name. Logs warning if not found.
static void set_int(const char* name, int value) {
    lv_subject_t* s = lv_xml_get_subject(nullptr, name);
    if (s) {
        lv_subject_set_int(s, value);
    } else {
        spdlog::warn("[Scenario] Subject not found: {}", name);
    }
}

// Helper: set a STRING subject by name. Logs warning if not found.
static void set_string(const char* name, const char* value) {
    lv_subject_t* s = lv_xml_get_subject(nullptr, name);
    if (s) {
        lv_subject_copy_string(s, value);
    } else {
        spdlog::warn("[Scenario] Subject not found: {}", name);
    }
}

static std::vector<MockScenario> build_scenarios() {
    std::vector<MockScenario> scenarios;

    // --- idle ---
    scenarios.push_back({"idle", "Default idle state — connected, no print", []() {
                             set_int("printer_connection_state", 2); // connected
                             set_int("klippy_state", 0);             // ready
                             set_int("print_state_enum", 0);         // standby
                             set_string("print_state", "standby");
                             set_int("print_progress", 0);
                             set_int("print_active", 0);
                             set_int("extruder_temp", 250); // 25.0°C (ambient)
                             set_int("extruder_target", 0);
                             set_int("bed_temp", 230); // 23.0°C (ambient)
                             set_int("bed_target", 0);
                             set_int("fan_speed", 0);
                             set_int("print_start_phase", 0); // IDLE
                         }});

    // --- printing ---
    scenarios.push_back({"printing", "Mid-print with temps, progress, filename", []() {
                             set_int("printer_connection_state", 2);
                             set_int("klippy_state", 0);
                             set_int("print_state_enum", 1); // printing
                             set_string("print_state", "printing");
                             set_int("print_active", 1);
                             set_int("print_progress", 42);
                             set_string("print_filename", "benchy.gcode");
                             set_string("print_display_filename", "benchy.gcode");
                             set_int("print_layer_current", 84);
                             set_int("print_layer_total", 200);
                             set_int("print_elapsed", 3600);   // 1 hour
                             set_int("print_time_left", 4800); // 80 min left
                             set_int("print_duration", 3500);
                             set_int("extruder_temp", 2100); // 210.0°C
                             set_int("extruder_target", 2100);
                             set_int("bed_temp", 600); // 60.0°C
                             set_int("bed_target", 600);
                             set_int("fan_speed", 255);
                             set_int("print_start_phase", 10); // COMPLETE (past start)
                             set_int("print_show_progress", 1);
                         }});

    // --- paused ---
    scenarios.push_back({"paused", "Paused mid-print", []() {
                             set_int("printer_connection_state", 2);
                             set_int("klippy_state", 0);
                             set_int("print_state_enum", 2); // paused
                             set_string("print_state", "paused");
                             set_int("print_active", 1);
                             set_int("print_progress", 42);
                             set_string("print_filename", "benchy.gcode");
                             set_string("print_display_filename", "benchy.gcode");
                             set_int("print_layer_current", 84);
                             set_int("print_layer_total", 200);
                             set_int("extruder_temp", 2100);
                             set_int("extruder_target", 2100);
                             set_int("bed_temp", 600);
                             set_int("bed_target", 600);
                             set_int("fan_speed", 0);
                             set_int("print_start_phase", 10);
                         }});

    // --- error ---
    scenarios.push_back({"error", "Klippy error state", []() {
                             set_int("printer_connection_state", 2);
                             set_int("klippy_state", 3);     // error
                             set_int("print_state_enum", 5); // error
                             set_string("print_state", "error");
                             set_int("print_active", 0);
                             set_int("print_progress", 0);
                             set_int("extruder_temp", 0);
                             set_int("extruder_target", 0);
                             set_int("bed_temp", 0);
                             set_int("bed_target", 0);
                         }});

    // --- disconnected ---
    scenarios.push_back({"disconnected", "No printer connection", []() {
                             set_int("printer_connection_state", 0); // disconnected
                             set_int("klippy_state", 0);
                             set_int("print_state_enum", 0);
                             set_string("print_state", "standby");
                             set_int("print_active", 0);
                             set_int("nav_buttons_enabled", 0);
                         }});

    // --- heating ---
    scenarios.push_back({"heating", "Print start — heating nozzle phase", []() {
                             set_int("printer_connection_state", 2);
                             set_int("klippy_state", 0);
                             set_int("print_state_enum", 1); // printing
                             set_string("print_state", "printing");
                             set_int("print_active", 1);
                             set_int("print_progress", 0);
                             set_int("print_show_progress", 0);
                             set_int("print_start_phase", 4); // HEATING_NOZZLE
                             set_int("print_start_progress", 65);
                             set_string("print_start_message", "Heating Nozzle...");
                             set_string("print_start_time_left", "~2 min left");
                             set_int("extruder_temp", 1650); // 165.0°C (rising)
                             set_int("extruder_target", 2100);
                             set_int("bed_temp", 600);
                             set_int("bed_target", 600);
                             set_string("print_filename", "benchy.gcode");
                             set_string("print_display_filename", "benchy.gcode");
                         }});

    // --- ams_hh_4gate ---
    scenarios.push_back({"ams_hh_4gate", "Happy Hare 4-gate setup with filament loaded", []() {
                             set_int("printer_connection_state", 2);
                             set_int("klippy_state", 0);
                             set_int("print_state_enum", 0);
                             set_int("ams_filament_loaded", 1);
                             set_string("ams_system_name", "Happy Hare");
                             set_string("ams_action_detail", "Idle");
                             set_string("ams_current_material_text", "PLA");
                             set_string("ams_current_slot_text", "Gate 0");
                             set_int("ams_supports_bypass", 1);
                             set_int("ams_bypass_active", 0);
                         }});

    // --- ams_afc_8lane ---
    scenarios.push_back({"ams_afc_8lane", "AFC 8-lane with mixed sensor states", []() {
                             set_int("printer_connection_state", 2);
                             set_int("klippy_state", 0);
                             set_int("print_state_enum", 0);
                             set_int("ams_filament_loaded", 1);
                             set_string("ams_system_name", "AFC");
                             set_string("ams_action_detail", "Idle");
                             set_string("ams_current_material_text", "PETG");
                             set_string("ams_current_slot_text", "Lane 2");
                             set_int("ams_supports_bypass", 0);
                         }});

    // --- ams_loading ---
    scenarios.push_back({"ams_loading", "AMS mid-load operation", []() {
                             set_int("printer_connection_state", 2);
                             set_int("klippy_state", 0);
                             set_int("ams_filament_loaded", 0);
                             set_string("ams_system_name", "AFC");
                             set_string("ams_action_detail", "Loading Lane 3...");
                             set_string("ams_current_material_text", "ABS");
                             set_string("ams_current_slot_text", "Lane 3");
                         }});

    // --- ams_error ---
    scenarios.push_back({"ams_error", "AMS filament jam error", []() {
                             set_int("printer_connection_state", 2);
                             set_int("klippy_state", 0);
                             set_string("ams_system_name", "Happy Hare");
                             set_string("ams_action_detail",
                                        "ERROR: Filament jam detected on Gate 1");
                             set_int("ams_filament_loaded", 0);
                         }});

    // --- http_busy ---
    // Not a printer state — a synthetic HTTP-lane busy condition for testing
    // wait_idle()'s "which counter was busy" reporting deterministically.
    // A burst of subject changes makes UpdateQueue's `pending_` nonzero for
    // at most one LVGL tick (~16ms): process_pending() drains the entire
    // batch every tick, so there's no way to widen that window from the
    // subject side. HttpExecutor::inflight() instead stays incremented for
    // the full wall-clock duration of the submitted job body (see
    // http_executor.h's `submit()` — incremented there, decremented only on
    // completion), so a deliberately slow job gives wait_idle(timeout=0.0) a
    // multi-hundred-millisecond window to land in instead of a sub-tick race.
    // 2s comfortably exceeds subprocess-spawn latency even under heavy load
    // (confirmed via this test's own investigation: ~100-200ms round trips
    // at load average 60+, nowhere near 2s).
    scenarios.push_back({"http_busy",
                         "Synthetic HttpExecutor busy state (test-only, not a printer condition)",
                         []() {
                             helix::http::HttpExecutor::fast().submit([]() {
                                 std::this_thread::sleep_for(std::chrono::milliseconds(2000));
                             });
                         }});

    return scenarios;
}

const std::vector<MockScenario>& get_mock_scenarios() {
    static auto scenarios = build_scenarios();
    return scenarios;
}

const MockScenario* find_scenario(const std::string& name) {
    for (const auto& s : get_mock_scenarios()) {
        if (s.name == name) {
            return &s;
        }
    }
    return nullptr;
}

} // namespace helix
