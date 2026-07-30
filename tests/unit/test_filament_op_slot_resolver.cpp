// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_filament_op_slot_resolver.cpp
 * @brief Tests for resolve_op_button_slot() — which slot's load-state gates the
 *        FilamentPanel Load/Unload/Purge buttons for the selected tool.
 *
 * Run with: ./build/bin/helix-tests "[filament][op_slot]"
 *
 * Regression guard for prestonbrown/helixscreen#1065: on a single-extruder
 * multi-lane AMS (AD5X native ZMOD IFS) the backend never populates
 * tool_to_slot_map, so the old fallback collapsed the slot to the tool index
 * (0) and read slot 0's load-state — wrongly greying Unload while a *different*
 * lane was loaded to the toolhead. The loaded lane must come from current_slot.
 */

#include <functional>

#include "../catch_amalgamated.hpp"

#include "filament_op_slot_resolver.h"

using helix::ui::resolve_op_button_slot;

namespace {

// Minimal AmsSystemInfo tailored for a resolution case.
AmsSystemInfo make_sys(std::vector<int> tool_map, int current_slot) {
    AmsSystemInfo sys;
    sys.tool_to_slot_map = std::move(tool_map);
    sys.current_slot = current_slot;
    return sys;
}

// Mirror of FilamentPanel::execute_load's target decision (single source of
// truth == resolve_op_button_slot on the dropdown-selected tool). A result
// >= 0 is loaded directly; < 0 means the panel redirects to the AMS slot
// picker instead. FilamentPanel is LVGL/Moonraker-coupled and not
// unit-instantiated (cf. test_filament_op_button_state_char.cpp), so these
// wrappers pin the per-op branching the panel applies on top of the resolver.
int panel_load_target(const AmsSystemInfo& sys, int selected_tool, int tool_count) {
    return resolve_op_button_slot(sys, selected_tool, tool_count);
}

// Mirror of execute_unload's gate: only unload when the dropdown-selected slot
// is actually loaded (slot_is_actively_loaded || slot_has_filament_at_toolhead).
bool panel_unload_allowed(const AmsSystemInfo& sys, int selected_tool, int tool_count,
                          const std::function<bool(int)>& slot_loaded) {
    int slot = resolve_op_button_slot(sys, selected_tool, tool_count);
    return slot >= 0 && slot_loaded(slot);
}

} // namespace

TEST_CASE("AD5X IFS: single tool, no map, loaded lane 1 → slot 1 (Unload enabled)",
          "[filament][op_slot]") {
    // Native ZMOD: tool_to_slot_map is empty, tool 0 fed by lane 1 (current_slot=1).
    AmsSystemInfo sys = make_sys(/*tool_map=*/{}, /*current_slot=*/1);
    // The bug returned 0 here (tool index) → slot 0 not loaded → Unload greyed.
    CHECK(resolve_op_button_slot(sys, /*selected_tool=*/0, /*tool_count=*/1) == 1);
}

TEST_CASE("AD5X IFS: single tool, all-unmapped map (-1 fill) still uses current_slot",
          "[filament][op_slot]") {
    // Some backends size the vector but leave entries at -1 (unmapped).
    AmsSystemInfo sys = make_sys(/*tool_map=*/{-1, -1, -1, -1}, /*current_slot=*/2);
    CHECK(resolve_op_button_slot(sys, /*selected_tool=*/0, /*tool_count=*/1) == 2);
}

TEST_CASE("Single tool, nothing loaded → -1 (Unload stays disabled)", "[filament][op_slot]") {
    AmsSystemInfo sys = make_sys(/*tool_map=*/{}, /*current_slot=*/-1);
    CHECK(resolve_op_button_slot(sys, /*selected_tool=*/0, /*tool_count=*/1) == -1);
}

TEST_CASE("Toolchanger: multi-tool, no map → tool index == slot index", "[filament][op_slot]") {
    // A real toolchanger must keep per-tool selection: selecting tool 2 gates on
    // slot 2, NOT on current_slot (which is the *active* tool's lane).
    AmsSystemInfo sys = make_sys(/*tool_map=*/{}, /*current_slot=*/0);
    CHECK(resolve_op_button_slot(sys, /*selected_tool=*/2, /*tool_count=*/4) == 2);
}

TEST_CASE("Explicit tool→slot map wins over both fallbacks", "[filament][op_slot]") {
    AmsSystemInfo sys = make_sys(/*tool_map=*/{3, 1, 0}, /*current_slot=*/1);
    CHECK(resolve_op_button_slot(sys, /*selected_tool=*/0, /*tool_count=*/1) == 3);
    CHECK(resolve_op_button_slot(sys, /*selected_tool=*/2, /*tool_count=*/4) == 0);
}

TEST_CASE("Mapped entry of -1 falls through to topology fallback", "[filament][op_slot]") {
    // tool 1 explicitly unmapped (-1) on a single-tool system → current_slot.
    AmsSystemInfo sys = make_sys(/*tool_map=*/{-1}, /*current_slot=*/3);
    CHECK(resolve_op_button_slot(sys, /*selected_tool=*/0, /*tool_count=*/1) == 3);
}

TEST_CASE("BoxTurtle AFC: selected tool's lane wins over the loaded current_slot",
          "[filament][op_slot]") {
    // Live-captured .112 BoxTurtle: 4 lanes, identity tool->slot map, lane4 (slot 3)
    // loaded to the toolhead, dropdown defaulted to T0. The op slot must follow the
    // SELECTED tool (T0 -> slot 0), never the loaded current_slot (3). That divergence
    // made Load act on the already-loaded lane instead of the selected one.
    AmsSystemInfo sys = make_sys(/*tool_map=*/{0, 1, 2, 3}, /*current_slot=*/3);
    CHECK(resolve_op_button_slot(sys, /*selected_tool=*/0, /*tool_count=*/4) == 0);
    CHECK(resolve_op_button_slot(sys, /*selected_tool=*/3, /*tool_count=*/4) == 3);

    // AFC "map" remap (non-identity): T0 -> lane3. Still follows the selection.
    AmsSystemInfo remapped = make_sys(/*tool_map=*/{3, 2, 1, 0}, /*current_slot=*/1);
    CHECK(resolve_op_button_slot(remapped, /*selected_tool=*/0, /*tool_count=*/4) == 3);
}

// Snapmaker U1 regression guard (commit 504905a2 "Unload visits T0 first").
// U1 = 4 virtual tools T0..T3 with an identity tool->slot map
// (ams_backend_snapmaker.cpp), current_slot == current_tool == the picked-up
// toolhead. The original bug came from a stuck current_slot == -1 forcing a
// bare-default (T0) unload. Resolving the op slot from the dropdown-selected
// tool through the identity map must (a) match current_slot in steady state
// and (b) STILL yield the selected tool even when current_slot is stuck at -1,
// so a wrong-tool unload can't recur.
TEST_CASE("Snapmaker U1: identity map resolves the selected tool, immune to stuck current_slot",
          "[filament][op_slot]") {
    // Bart's scenario: T3 picked up, dropdown synced to the active tool T3.
    AmsSystemInfo u1 = make_sys(/*tool_map=*/{0, 1, 2, 3}, /*current_slot=*/3);
    CHECK(resolve_op_button_slot(u1, /*selected_tool=*/3, /*tool_count=*/4) == 3); // NOT 0

    // Divergence (user picks a different tool during the async change): follow dropdown.
    CHECK(resolve_op_button_slot(u1, /*selected_tool=*/1, /*tool_count=*/4) == 1);

    // Original root-cause state: current_slot stuck at -1. Identity map still
    // yields the selected tool — no bare-default T0 unload.
    AmsSystemInfo u1_stuck = make_sys(/*tool_map=*/{0, 1, 2, 3}, /*current_slot=*/-1);
    CHECK(resolve_op_button_slot(u1_stuck, /*selected_tool=*/3, /*tool_count=*/4) == 3);
}

// Characterization of the FilamentPanel op-target contract: gating, Load, and
// Unload all resolve their slot the SAME way (selected_op_slot ==
// resolve_op_button_slot on the dropdown tool). Load acts on the resolved slot
// or redirects when < 0; Unload only fires when that slot is loaded.
TEST_CASE("Panel op-target contract: Load follows the dropdown, Unload gated on that slot",
          "[filament][op_slot][char]") {
    auto only_slot3_loaded = [](int s) { return s == 3; };

    // BoxTurtle: T0 selected while lane4 (slot 3) is loaded. Load targets slot 0
    // (the selection), NOT the loaded slot 3 — the exact bug this fix closes.
    AmsSystemInfo bt = make_sys(/*tool_map=*/{0, 1, 2, 3}, /*current_slot=*/3);
    CHECK(panel_load_target(bt, /*T0=*/0, /*tool_count=*/4) == 0);
    // Unload of T0 is refused (slot 0 not loaded → button greyed); Unload of T3
    // is allowed and targets the loaded slot 3.
    CHECK_FALSE(panel_unload_allowed(bt, /*T0=*/0, 4, only_slot3_loaded));
    CHECK(panel_unload_allowed(bt, /*T3=*/3, 4, only_slot3_loaded));

    // U1: T3 selected == loaded. Unload targets 3, never the bare-default 0.
    AmsSystemInfo u1 = make_sys(/*tool_map=*/{0, 1, 2, 3}, /*current_slot=*/3);
    CHECK(panel_load_target(u1, /*T3=*/3, 4) == 3);
    CHECK(panel_unload_allowed(u1, /*T3=*/3, 4, only_slot3_loaded));

    // AD5X IFS single-tool multi-lane, nothing loaded (no map, current_slot -1):
    // Load target is -1 → the panel redirects to the AMS slot picker.
    CHECK(panel_load_target(make_sys(/*tool_map=*/{}, /*current_slot=*/-1), /*T0=*/0, 1) == -1);
}

// ---------------------------------------------------------------------------
// Print-state gating (bundle JX2FVRB9)
// ---------------------------------------------------------------------------
//
// Load/Unload both run through AmsSubscriptionBackend::check_preconditions(true),
// which refuses while a print is PRINTING *or* PAUSED because the macros home the
// toolhead. The panel gated on load state alone, so during a runout pause the
// Load button stayed lit and every tap produced a "Cannot run filament operation
// while printing" toast.
//
// Mutation check: drop `|| print_active` from compute_op_button_gating's
// load_disabled term and "a print owning the toolhead disables both" fails.
TEST_CASE("compute_op_button_gating: print state gates Load and Unload",
          "[filament][op_slot][print_guard]") {
    using helix::ui::compute_op_button_gating;

    SECTION("no print: load state alone decides, as before") {
        auto empty = compute_op_button_gating(/*is_loaded=*/false, /*print_active=*/false);
        CHECK_FALSE(empty.load_disabled); // can load
        CHECK(empty.unload_disabled);     // nothing to unload

        auto loaded = compute_op_button_gating(/*is_loaded=*/true, /*print_active=*/false);
        CHECK(loaded.load_disabled);         // already loaded
        CHECK_FALSE(loaded.unload_disabled); // can unload
    }

    SECTION("a print owning the toolhead disables both") {
        auto empty = compute_op_button_gating(/*is_loaded=*/false, /*print_active=*/true);
        CHECK(empty.load_disabled);
        CHECK(empty.unload_disabled);

        auto loaded = compute_op_button_gating(/*is_loaded=*/true, /*print_active=*/true);
        CHECK(loaded.load_disabled);
        CHECK(loaded.unload_disabled);
    }
}
