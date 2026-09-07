// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file filament_op_execute.h
 * @brief Shared EXECUTION of a Load/Unload/Purge, once filament_op_dispatch.h
 *        has already decided which tier to take.
 *
 * filament_op_dispatch.h (plan_load(), plan_unload()) centralizes the DECISION
 * of which tier a filament operation takes. The act of actually running that
 * tier — building BackendCaps, switching over FilamentTier, dispatching the
 * configured macro or falling back to raw gcode — had begun to diverge the
 * same way across surfaces before this file existed. Extracted from
 * PrintStatusWidget::dispatch_load(), which was the first duplicate of
 * FilamentRunoutHandler's version. FilamentRunoutHandler was converted onto
 * this file too, so both callers now share this execution; FilamentPanel and
 * AmsOperationSidebar still answer the same plan_load()/plan_unload()
 * decision but each run their own independent execution ladder — unconverted
 * follow-up.
 */

#pragma once

#include "filament_op_dispatch.h"
#include "standard_macros.h"

class AmsBackend;

namespace helix::ui {

// ============================================================================
// Live-state half of the decision
// ============================================================================
//
// filament_op_dispatch.h states the tier rules over plain values, so they stay
// testable with no printer and no display. Getting from a live AmsBackend and
// the StandardMacros registry TO those values is mechanical, and identical for
// every surface — which is what makes a hand-written copy of it at each call
// site a place for the surfaces to drift apart. Written once, here.

/**
 * @brief Read the planner's backend answers off a live AmsBackend.
 *
 * @param backend     May be null; every field then keeps its "no backend" default.
 * @param info_out    Filled with get_system_info() when a backend exists, left
 *                    untouched otherwise. plan_load() reads it, and callers need
 *                    it afterwards to resolve the slot the op is about.
 * @param target_slot The lane the plan targets. needs_unload_before_load() is a
 *                    per-lane question, so it must be the SAME slot.
 */
[[nodiscard]] BackendCaps read_backend_caps(AmsBackend* backend, AmsSystemInfo& info_out,
                                            int target_slot);

/// plan_load() with the StandardMacros LoadFilament slot read off the registry.
[[nodiscard]] FilamentOpPlan plan_live_load(const AmsSystemInfo& info, const BackendCaps& caps,
                                            int target_slot);

/// plan_unload() with the StandardMacros UnloadFilament slot read off the registry.
/// @param target_is_loaded From read_unload_target_loaded() — never answered inline.
[[nodiscard]] FilamentOpPlan plan_live_unload(const BackendCaps& caps, int target_slot,
                                              bool target_is_loaded);

/// unload_target_is_loaded() with the four per-lane answers read off a live
/// backend. False when @p backend is null: with no backend there is no lane to
/// ask about, and plan_unload() gates its tier 1 on the backend anyway.
[[nodiscard]] bool read_unload_target_loaded(AmsBackend* backend, const AmsSystemInfo& info,
                                             int target_slot);

// ============================================================================
// Preheat
// ============================================================================

/**
 * @brief Why heating the hotend ourselves before a filament op would be redundant.
 *
 * Three independent reasons, and a surface that knows only one of them imposes a
 * preheat the other two would have skipped. Ask preheat_skip_reason() rather
 * than any single term.
 */
enum class PreheatSkip {
    None,             ///< Preheat as usual
    UserOverride,     ///< "Allow cold load/unload" — the user's macros do their own heating
    BackendSelfHeats, ///< AmsBackend::supports_auto_heat_on_load()
    MacroSelfHeats,   ///< helix::filament_macros::macro_heats_hotend()
};

/**
 * @brief Should this surface skip its own preheat before dispatching @p plan?
 *
 * Reads SafetySettingsManager and StandardMacros, so it answers about the state
 * the op is about to run against.
 *
 * The tier matters: a macro that heats itself is only a reason to skip when the
 * macro is what will actually run, and the same for the backend. Pass the plan
 * the caller is about to dispatch, not one from before a slot changed.
 *
 * @param plan    The plan about to be dispatched.
 * @param slot    Which StandardMacros slot @p plan resolves against.
 * @param backend May be null.
 */
[[nodiscard]] PreheatSkip preheat_skip_reason(const FilamentOpPlan& plan, StandardMacroSlot slot,
                                              AmsBackend* backend);

/// Short name for @p reason, for log lines. Never null.
[[nodiscard]] const char* preheat_skip_name(PreheatSkip reason);

/// @note **`log_tag` must have static storage duration.** All three functions
/// capture the raw pointer in lambdas that outlive the call — the macro-tier
/// and raw-gcode paths hand their success/error callbacks to Moonraker and
/// return immediately. A string literal (what every caller passes: a bracketed
/// class name) satisfies this; a `std::string::c_str()` or a stack buffer would
/// dangle by the time the reply lands. Deliberately `const char*` rather than
/// `std::string` so that constraint is visible instead of being paid for on
/// every dispatch.

/// Execute a load for `slot` on `backend` (which may be null), resolving the
/// tier through plan_load() and running it. `log_tag` prefixes the spdlog lines
/// so a reader can still tell which surface asked.
///
/// Extracted from PrintStatusWidget::dispatch_load(). filament_op_dispatch.h
/// already centralizes the DECISION; this centralizes the EXECUTION, which had
/// begun to diverge the same way.
void execute_filament_load(AmsBackend* backend, int slot, const char* log_tag);

/// Unload counterpart. `target_is_loaded` comes from unload_target_is_loaded()
/// in filament_op_dispatch.h - do not answer that question inline, that
/// divergence is what the helper exists to prevent.
void execute_filament_unload(AmsBackend* backend, int slot, bool target_is_loaded,
                             const char* log_tag);

/// Purge counterpart. Two tiers only (the configured macro, then
/// filament_purge_fallback_gcode() from filament_op_router.h) — no AmsBackend
/// exposes a purge entry point, so there is no plan_purge() to route through.
/// Extracted from FilamentRunoutHandler::dispatch_purge(), the one existing
/// purge dispatch that was NOT entangled with panel UI state; that method now
/// calls this. FilamentPanel::execute_purge() was deliberately not the source:
/// it drives that panel's operation_guard_ spinner and a macro-parameter modal
/// with active-material temperature prefill, none of which apply here — and it
/// remains an unconverted third copy for that reason.
void execute_filament_purge(const char* log_tag);

} // namespace helix::ui
