// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ui_observer_guard.h"

namespace helix::ui {

/// App-lifetime controller that offers the Snapmaker power-loss-recovery prompt
/// at connect time. Owned by SubjectInitializer (constructed in init_observers,
/// destroyed with it — the ObserverGuard members auto-reset via RAII).
///
/// Three observers, all on STATIC subjects (singleton lifetime, no
/// SubjectLifetime token needed). The authoritative account of WHEN the offer
/// fires and re-fires lives at the plr_should_offer decision site in
/// on_pl_env_valid_changed / evaluate_offer (ui_plr_offer_controller.cpp); the
/// observers below are just the edges that drive it:
///   - pl_env_valid (PRIMARY trigger): a genuine 0->1 edge offers.
///   - connection state: on a CONNECTED->not-CONNECTED edge, re-arms the
///     one-shot latch AND forces pl_env_valid back to 0 (see
///     on_connection_state_changed) so the next reconnect produces a real
///     0->1 edge rather than a same-value write the subject would swallow.
///   - wizard active: on a 1->0 edge (wizard closed) re-evaluates the offer,
///     so an offer that was suppressed only because the wizard owned the
///     screen now fires. This is what makes wizard suppression temporary
///     instead of permanent.
///
/// All callbacks run on the main thread — observe_int_sync defers via the update
/// queue and plr_should_offer/plr_should_rearm are pure — so no
/// AsyncLifetimeGuard is required here. The gcode dispatch (which does cross the
/// thread boundary) lives in show_plr_recovery_prompt, not this class.
class PlrOfferController {
  public:
    PlrOfferController();
    ~PlrOfferController() = default;

    PlrOfferController(const PlrOfferController&) = delete;
    PlrOfferController& operator=(const PlrOfferController&) = delete;

  private:
    // Single evaluation point: reads pl_env_valid + printer/backend/wizard state
    // live and offers when plr_should_offer() agrees. Both the pl_env_valid and
    // wizard observers route here so the decision lives in exactly one place.
    void evaluate_offer();
    void on_pl_env_valid_changed(int pl_env_valid);
    void on_connection_state_changed(int new_conn_state);
    void on_wizard_active_changed(int wizard_active);

    ObserverGuard pl_valid_observer_;
    ObserverGuard conn_observer_;
    ObserverGuard wizard_observer_;
    bool prompted_this_connect_ = false;
    int last_conn_state_ = 0;
};

} // namespace helix::ui
