// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ui_observer_guard.h"

namespace helix::ui {

/// App-lifetime controller that offers the Snapmaker power-loss-recovery prompt
/// at connect time. Owned by SubjectInitializer (constructed in init_observers,
/// destroyed with it — the two ObserverGuard members auto-reset via RAII).
///
/// Two observers, both on STATIC subjects (singleton lifetime, no
/// SubjectLifetime token needed):
///   - pl_env_valid (PRIMARY trigger): flips 0->1 after discovery on a
///     Snapmaker with a recoverable interrupted print.
///   - connection state: re-arms the one-shot latch on a CONNECTED->not-CONNECTED
///     edge so a disconnect/reconnect cycle offers again.
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
    void on_pl_env_valid_changed(int pl_env_valid);
    void on_connection_state_changed(int new_conn_state);

    ObserverGuard pl_valid_observer_;
    ObserverGuard conn_observer_;
    bool prompted_this_connect_ = false;
    int last_conn_state_ = 0;
};

} // namespace helix::ui
