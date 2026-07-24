// tests/test_helpers/filament_panel_test_access.h
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ui_panel_filament.h"

#include <lvgl.h>

namespace helix::ui {

// Test-only access to FilamentPanel's private op-slot resolution and the Load /
// Unload executors. Lets an integration test drive the ACTUAL production methods
// (never a mirror) and observe which slot argument reaches the AMS backend, so
// the single-source-of-truth fix (execute_load/execute_unload act on the
// dropdown-selected slot, not a divergent current_slot) is regression-guarded.
struct FilamentPanelTestAccess {
    static int selected_op_slot(const FilamentPanel& p) {
        return p.selected_op_slot();
    }

    static void execute_load(FilamentPanel& p) {
        p.execute_load();
    }

    static void execute_unload(FilamentPanel& p) {
        p.execute_unload();
    }

    static void populate_extruder_dropdown(FilamentPanel& p) {
        p.populate_extruder_dropdown();
    }

    static lv_obj_t* extruder_dropdown(FilamentPanel& p) {
        return p.extruder_dropdown_;
    }
};

} // namespace helix::ui
