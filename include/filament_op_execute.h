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
 * FilamentRunoutHandler's version.
 */

#pragma once

class AmsBackend;

namespace helix::ui {

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

/// Purge counterpart. Two tiers only (the configured macro, then a fixed
/// raw-gcode extrude) — no AmsBackend exposes a purge entry point, so there is
/// no plan_purge() to route through. Mirrors FilamentRunoutHandler::dispatch_purge(),
/// the one existing purge dispatch that is NOT entangled with panel UI state.
/// FilamentPanel::execute_purge() was deliberately not the source: it drives
/// that panel's operation_guard_ spinner and a macro-parameter modal with
/// active-material temperature prefill, none of which apply here. See
/// filament_op_execute.cpp for the follow-up this split leaves.
void execute_filament_purge(const char* log_tag);

} // namespace helix::ui
