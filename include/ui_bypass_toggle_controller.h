// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui_observer_guard.h"

#include "ams_types.h"

#include <functional>

class AmsBackend;

namespace helix::ui {

/// Widgets-free bypass toggle policy shared by the AMS sidebar and the home
/// BypassWidget. Owns the pending-enable state machine (unload-first
/// chaining, #1229 discipline) and the print-active refusal.
///
/// The controller observes the ams_action subject ITSELF while a pending
/// unload->enable chain is armed (the sidebar used to be the only feeder, so
/// a chain started from the home BypassWidget tile never saw UNLOADING->IDLE
/// and bypass never enabled). The observer is armed when toggle() starts the
/// unload and torn down when the chain settles (IDLE enables, ERROR disarms)
/// or is cancelled — a settled controller stays detached.
class BypassToggleController {
  public:
    BypassToggleController() = default;
    ~BypassToggleController();

    BypassToggleController(const BypassToggleController&) = delete;
    BypassToggleController& operator=(const BypassToggleController&) = delete;

    /// User asked to flip bypass. Runs every guard, performs the backend
    /// call (or arms the unload→enable chain), toasts outcomes.
    void toggle();

    /// Engage bypass if it is not already engaged, then run @p on_ready.
    ///
    /// Takes toggle()'s enable path verbatim — the print guard, the hardware
    /// sensor refusal and the #1229 unload-first chain — so a caller that needs
    /// bypass engaged before dispatching an op never grows a second copy of that
    /// discipline. An already-engaged backend runs @p on_ready inline.
    ///
    /// @p on_ready runs ONLY on a successful engage; every refusal and every
    /// failure has already told the user why, and dispatching an op behind one
    /// would act on a path the filament is not on. It may run after an async
    /// unload, so a caller that can be torn down must wrap it in its own
    /// lifetime guard.
    void ensure_engaged_then(std::function<void()> on_ready);

    /// Feed an ams_action subject change (UNLOADING→IDLE/ERROR chain step).
    /// Still public for direct unit-driving; production feed is the
    /// controller's own ams_action observer, which computes prev from
    /// prev_action_. Returns true if the event was consumed for the pending
    /// chain.
    bool on_ams_action_changed(AmsAction prev, AmsAction next);

    /// Abort any pending chain (owner is going away / context reset).
    void cancel_pending();

    [[nodiscard]] bool pending_enable() const {
        return pending_bypass_enable_;
    }

  private:
    /// @return true when bypass is engaged afterwards.
    bool enable_now(AmsBackend* backend);

    /// Run and clear the pending ensure_engaged_then() continuation.
    void fire_on_ready();

    /// Shared by toggle() and ensure_engaged_then(): the guards, then either the
    /// unload→enable chain or an immediate enable. @return true when bypass is
    /// engaged by the time this returns (so an inline continuation may run).
    bool begin_engage();
    /// Subscribe to the ams_action subject for the pending chain (idempotent).
    void arm_action_observer();
    /// Detach the ams_action observer (chain settled or cancelled).
    void disarm_action_observer();

    bool pending_bypass_enable_ = false;
    /// Continuation armed by ensure_engaged_then(), cleared as soon as it runs
    /// or the chain gives up — never left armed for the next unrelated toggle().
    std::function<void()> on_ready_;
    /// Last action seen by our own ams_action observer; re-seeded from the
    /// live subject each time the observer arms so a mid-action subscribe
    /// still computes the right edge.
    AmsAction prev_action_ = AmsAction::IDLE;
    ObserverGuard action_observer_;
};

} // namespace helix::ui
