// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_filament_op_dispatch.cpp
 * @brief The three-tier filament dispatch decision, shared by all three surfaces.
 *
 * FilamentPanel::execute_load() has always routed AMS backend -> configured
 * macro -> raw gcode. AmsOperationSidebar and the runout handler never learned
 * the tiering, and each grew its own partial answer:
 *
 *   - The sidebar silently returns when there is no backend, and owns the
 *     load-vs-swap rule (needs_unload_before_load + mapped_tool -> change_tool)
 *     that the panel has never had.
 *   - The sidebar also owns the already-mounted toolchanger guard, which is why
 *     the same no-op that hung the panel's Load button for 120 s (bundle
 *     9KRXZ62P) is harmless from the AMS panel.
 *   - The runout handler navigates away for a load and reaches a
 *     params-suppressed macro for unload/purge.
 *
 * These cases pin the merged rule. Two asymmetries are deliberate and are
 * asserted rather than smoothed over:
 *
 *   1. Load falls through to tier 2 when requires_slot_selection_for_load() is
 *      false — that is how a bypass spool reaches the user's LOAD_FILAMENT
 *      macro at all. Unload gates on the backend merely existing, because AFC
 *      runs the user's unload macro itself under bypass and routing it to
 *      tier 2 would run it twice.
 *   2. Load is not always load_filament(). A seated machine swaps.
 */

#include "ams_types.h"
#include "filament_op_dispatch.h"

#include <vector>

#include "../catch_amalgamated.hpp"

using helix::ui::AmsCall;
using helix::ui::BackendCaps;
using helix::ui::FilamentRefusal;
using helix::ui::FilamentTier;
using helix::ui::plan_load;
using helix::ui::plan_unload;

namespace {

/// One unit with `slot_count` slots; slot i maps to tool i unless remapped.
AmsSystemInfo make_sys(int slot_count, int current_slot, std::vector<int> mapped_tools = {}) {
    AmsSystemInfo sys;
    AmsUnit unit;
    unit.slot_count = slot_count;
    for (int i = 0; i < slot_count; ++i) {
        SlotInfo slot;
        slot.mapped_tool =
            (i < static_cast<int>(mapped_tools.size())) ? mapped_tools[static_cast<size_t>(i)] : i;
        unit.slots.push_back(slot);
    }
    sys.units.push_back(std::move(unit));
    sys.total_slots = slot_count;
    sys.current_slot = current_slot;
    return sys;
}

/// A backend that wants a slot and has nothing seated — the fresh-load shape.
BackendCaps fresh_ams() {
    return {/*present=*/true, /*requires_slot_selection_for_load=*/true,
            /*needs_unload_before_load=*/false, /*is_tool_changer=*/false};
}

} // namespace

// =============================================================================
// Tier selection
// =============================================================================

TEST_CASE("plan_load: no backend falls through to the configured macro",
          "[filament][dispatch][tier]") {
    AmsSystemInfo sys = make_sys(0, -1);
    BackendCaps none{};

    auto plan = plan_load(sys, none, /*target_slot=*/-1, /*macro_available=*/true);
    CHECK(plan.tier == FilamentTier::Macro);
    CHECK(plan.refusal == FilamentRefusal::None);
}

TEST_CASE("plan_load: no backend and no macro falls through to raw gcode",
          "[filament][dispatch][tier]") {
    AmsSystemInfo sys = make_sys(0, -1);
    BackendCaps none{};

    auto plan = plan_load(sys, none, /*target_slot=*/-1, /*macro_available=*/false);
    CHECK(plan.tier == FilamentTier::RawGcode);
}

TEST_CASE("plan_load: bypass reaches the macro even with a backend present",
          "[filament][dispatch][tier][bypass]") {
    // requires_slot_selection_for_load() is `!is_bypass_active()`. Under bypass
    // the backend is present but must NOT own the load — the user's
    // LOAD_FILAMENT macro is the whole point of a bypass spool.
    AmsSystemInfo sys = make_sys(4, 1);
    BackendCaps bypassed = fresh_ams();
    bypassed.requires_slot_selection_for_load = false;

    auto plan = plan_load(sys, bypassed, /*target_slot=*/-2, /*macro_available=*/true);
    CHECK(plan.tier == FilamentTier::Macro);
    CHECK(plan.ams_call == AmsCall::None);
}

// =============================================================================
// Tier 1: what the backend is actually asked to do
// =============================================================================

TEST_CASE("plan_load: nothing seated dispatches a plain load", "[filament][dispatch]") {
    AmsSystemInfo sys = make_sys(4, -1);

    auto plan = plan_load(sys, fresh_ams(), /*target_slot=*/2, /*macro_available=*/true);
    CHECK(plan.tier == FilamentTier::AmsBackend);
    CHECK(plan.ams_call == AmsCall::Load);
    CHECK(plan.ams_arg == 2);
}

TEST_CASE("plan_load: a seated machine swaps via change_tool on the MAPPED tool",
          "[filament][dispatch][swap]") {
    // The rule AmsOperationSidebar has always applied and FilamentPanel never
    // has: with filament seated, feeding another lane is a tool change, not a
    // load. The argument is a TOOL number, so a remap must be honoured.
    AmsSystemInfo sys = make_sys(4, /*current_slot=*/0, /*mapped_tools=*/{0, 3, 2, 1});
    BackendCaps seated = fresh_ams();
    seated.needs_unload_before_load = true;

    auto plan = plan_load(sys, seated, /*target_slot=*/1, /*macro_available=*/true);
    CHECK(plan.tier == FilamentTier::AmsBackend);
    CHECK(plan.ams_call == AmsCall::ChangeTool);
    CHECK(plan.ams_arg == 3); // slot 1's mapped_tool, NOT slot 1
}

TEST_CASE("plan_load: a seated machine with no tool mapping loads the slot anyway",
          "[filament][dispatch][swap]") {
    // There is no tool number to change to, so the swap arm cannot fire. This
    // used to emit unload_active_filament() and stop — filament came out, the
    // stepper showed a swap, and nothing ever loaded. One command now goes to
    // the backend and the firmware decides: ACE's change_tool() IS
    // load_filament(), QIDI's load_filament() retracts the seated slot itself,
    // and AFC's is `CHANGE_TOOL LANE={n}`. Happy Hare's `MMU_LOAD GATE={n}` will
    // refuse, which is what allows_implicit_chaining()==false asks for (#1229).
    AmsSystemInfo sys = make_sys(4, /*current_slot=*/0, /*mapped_tools=*/{0, -1, -1, -1});
    BackendCaps seated = fresh_ams();
    seated.needs_unload_before_load = true;

    auto plan = plan_load(sys, seated, /*target_slot=*/1, /*macro_available=*/true);
    CHECK(plan.tier == FilamentTier::AmsBackend);
    CHECK(plan.ams_call == AmsCall::Load);
    CHECK(plan.ams_arg == 1); // the SLOT the user tapped, not a tool number
}

TEST_CASE("plan_load: an unresolvable target slot still dispatches a plain load",
          "[filament][dispatch][swap]") {
    // get_slot_global() returns nullptr for an index no unit covers. The old
    // code treated that as "unload whatever is active"; the backend's own
    // validate_slot_index() is the right place to refuse it.
    AmsSystemInfo sys = make_sys(4, /*current_slot=*/0);
    BackendCaps seated = fresh_ams();
    seated.needs_unload_before_load = true;

    auto plan = plan_load(sys, seated, /*target_slot=*/9, /*macro_available=*/true);
    CHECK(plan.tier == FilamentTier::AmsBackend);
    CHECK(plan.ams_call == AmsCall::Load);
    CHECK(plan.ams_arg == 9);
}

TEST_CASE("plan_load: reloading the seated slot itself is a plain load, not a swap",
          "[filament][dispatch][swap]") {
    // current_slot == target: the swap arm must not fire, or a top-up on the
    // loaded lane would dispatch a pointless tool change.
    AmsSystemInfo sys = make_sys(4, /*current_slot=*/2);
    BackendCaps seated = fresh_ams();
    seated.needs_unload_before_load = true;

    auto plan = plan_load(sys, seated, /*target_slot=*/2, /*macro_available=*/true);
    CHECK(plan.ams_call == AmsCall::Load);
    CHECK(plan.ams_arg == 2);
}

// =============================================================================
// Refusals
// =============================================================================

TEST_CASE("plan_load: toolchanger refuses a load on the tool already mounted",
          "[filament][dispatch][refusal][1183]") {
    // Bundle 9KRXZ62P. SELECT_TOOL on the carriage tool is a firmware no-op;
    // dispatching it left the Load button spinning for 120 s and locked out
    // every later operation via is_busy(). The sidebar has always refused here.
    AmsSystemInfo sys = make_sys(5, /*current_slot=*/4);
    BackendCaps tc = fresh_ams();
    tc.is_tool_changer = true;
    tc.needs_unload_before_load = true;

    auto plan = plan_load(sys, tc, /*target_slot=*/4, /*macro_available=*/true);
    CHECK(plan.tier == FilamentTier::Refused);
    CHECK(plan.refusal == FilamentRefusal::AlreadyMounted);
    CHECK(plan.ams_call == AmsCall::None);
}

TEST_CASE("plan_load: toolchanger still swaps to a DIFFERENT tool",
          "[filament][dispatch][refusal][1183]") {
    // Guard rail on the refusal above: it must key on the mounted tool, not on
    // being a toolchanger.
    AmsSystemInfo sys = make_sys(5, /*current_slot=*/4);
    BackendCaps tc = fresh_ams();
    tc.is_tool_changer = true;
    tc.needs_unload_before_load = true;

    auto plan = plan_load(sys, tc, /*target_slot=*/1, /*macro_available=*/true);
    CHECK(plan.tier == FilamentTier::AmsBackend);
    CHECK(plan.ams_call == AmsCall::ChangeTool);
    CHECK(plan.ams_arg == 1);
}

TEST_CASE("plan_load: unresolved slot refuses with SelectSlot", "[filament][dispatch][refusal]") {
    AmsSystemInfo sys = make_sys(4, -1);

    auto plan = plan_load(sys, fresh_ams(), /*target_slot=*/-1, /*macro_available=*/true);
    CHECK(plan.tier == FilamentTier::Refused);
    CHECK(plan.refusal == FilamentRefusal::SelectSlot);
}

// =============================================================================
// Unload — deliberately asymmetric with load
// =============================================================================

TEST_CASE("plan_unload: a present backend owns the unload even under bypass",
          "[filament][dispatch][bypass]") {
    // AFC calls the user's unload macro itself when bypass is enabled, so
    // routing bypass unload to tier 2 would run that macro twice. Unload gates
    // on the backend existing, NOT on requires_slot_selection_for_load().
    BackendCaps bypassed = fresh_ams();
    bypassed.requires_slot_selection_for_load = false;

    auto plan = plan_unload(bypassed, /*target_slot=*/0, /*target_is_loaded=*/true,
                            /*macro_available=*/true);
    CHECK(plan.tier == FilamentTier::AmsBackend);
    CHECK(plan.ams_call == AmsCall::Unload);
}

TEST_CASE("plan_unload: nothing loaded refuses instead of dispatching",
          "[filament][dispatch][refusal]") {
    auto plan = plan_unload(fresh_ams(), /*target_slot=*/2, /*target_is_loaded=*/false,
                            /*macro_available=*/true);
    CHECK(plan.tier == FilamentTier::Refused);
    CHECK(plan.refusal == FilamentRefusal::NothingLoaded);
}

TEST_CASE("plan_unload: no backend falls through to macro then raw gcode",
          "[filament][dispatch][tier]") {
    BackendCaps none{};

    auto with_macro = plan_unload(none, /*target_slot=*/-1, /*target_is_loaded=*/false,
                                  /*macro_available=*/true);
    CHECK(with_macro.tier == FilamentTier::Macro);

    auto without = plan_unload(none, /*target_slot=*/-1, /*target_is_loaded=*/false,
                               /*macro_available=*/false);
    CHECK(without.tier == FilamentTier::RawGcode);
}
