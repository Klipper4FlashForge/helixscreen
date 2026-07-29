// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ams_types.h"

namespace helix::ui {

/**
 * @brief Resolve which global AMS slot's load-state gates the Load/Unload/Purge
 *        buttons for the currently-selected tool.
 *
 * Resolution order:
 *   1. An explicit tool→slot map entry, when the backend publishes one
 *      (Happy Hare / AFC / properly-mapped toolchangers).
 *   2. No map entry: the meaning of "the tool index" depends on the topology.
 *      - Multi-tool toolchanger (one lane per tool): tool index == slot index.
 *      - Single-extruder multi-lane AMS (e.g. AD5X IFS): one tool is fed by any
 *        of N lanes, so the loaded lane is tracked by current_slot, NOT the tool
 *        index. Using the tool index there collapses to slot 0 and wrongly greys
 *        Unload while another lane is loaded (prestonbrown/helixscreen#1065).
 *
 * @param sys           Backend system info (tool_to_slot_map, current_slot).
 * @param selected_tool Dropdown/active tool index (>= 0).
 * @param tool_count    Number of tools the printer exposes (>1 == toolchanger).
 * @return Global slot index whose load-state gates the buttons, or -1 if none.
 */
[[nodiscard]] inline int resolve_op_button_slot(const AmsSystemInfo& sys, int selected_tool,
                                                int tool_count) {
    int slot = -1;
    if (selected_tool >= 0 && selected_tool < static_cast<int>(sys.tool_to_slot_map.size())) {
        slot = sys.tool_to_slot_map[selected_tool];
    }
    if (slot < 0) {
        if (tool_count > 1) {
            slot = selected_tool;    // toolchanger: tool index == slot index
        } else {
            slot = sys.current_slot; // single-tool multi-lane AMS: loaded lane
        }
    }
    return slot;
}

/// Enabled/disabled state of the filament panel's Load and Unload/Purge buttons.
struct OpButtonGating {
    bool load_disabled = false;
    bool unload_disabled = false;
};

/**
 * @brief Gate the panel's Load / Unload buttons on load state AND print state.
 *
 * Load state alone is not enough: both operations run through
 * AmsSubscriptionBackend::check_preconditions(true), which refuses while a print
 * is PRINTING or PAUSED because the load/unload macros home the toolhead. A
 * button offered in that window is a guaranteed-failure dead end — which is
 * exactly what a runout-paused user hits, since Klipper's own message tells them
 * to load filament (bundle JX2FVRB9).
 *
 * @param is_loaded    Selected slot has filament at the toolhead.
 * @param print_active A print job owns the toolhead (see print_occupies_toolhead).
 */
[[nodiscard]] inline OpButtonGating compute_op_button_gating(bool is_loaded, bool print_active) {
    return {/*load_disabled=*/is_loaded || print_active,
            /*unload_disabled=*/!is_loaded || print_active};
}

} // namespace helix::ui
