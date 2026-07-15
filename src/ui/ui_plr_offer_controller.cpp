// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "plr_offer_controller.h"

#include "ui_plr_prompt.h"

#include "ams_backend.h"
#include "ams_state.h"
#include "ams_types.h"
#include "app_globals.h"
#include "observer_factory.h"
#include "plr_offer.h"
#include "print_start_navigation.h" // is_active_print_state, PrintJobState
#include "printer_state.h"

#include <spdlog/spdlog.h>

namespace helix::ui {

PlrOfferController::PlrOfferController() {
    auto& ps = get_printer_state();

    // Seed last_conn_state_ from the live subject BEFORE registering the conn
    // observer. observe_int_sync fires once at registration with the current
    // value, so seeding here means that first firing sees prev == next and does
    // not spuriously re-arm the latch.
    last_conn_state_ = lv_subject_get_int(ps.get_printer_connection_state_subject());

    // pl_env_valid is the PRIMARY trigger. observe_int_sync fires once at
    // registration with the current value (deferred via the update queue), so a
    // pl_env_valid that is ALREADY true when we register still offers — no
    // separate explicit level-check is needed. At first app connect the subject
    // is 0 at registration and flips 0->1 after discovery, so the edge path
    // covers the common case; the registration-fire covers the reconnect /
    // late-registration case where the flag is already set.
    pl_valid_observer_ = observe_int_sync(
        ps.get_pl_env_valid_subject(), this,
        [](PlrOfferController* self, int value) { self->on_pl_env_valid_changed(value); });

    conn_observer_ = observe_int_sync(
        ps.get_printer_connection_state_subject(), this,
        [](PlrOfferController* self, int value) { self->on_connection_state_changed(value); });
}

void PlrOfferController::on_pl_env_valid_changed(int pl_env_valid) {
    auto& ps = get_printer_state();

    // idle == not actively printing/paused. Reuse the print-start-navigation
    // classifier so "active" means the same thing everywhere.
    bool idle = !helix::is_active_print_state(
        static_cast<PrintJobState>(lv_subject_get_int(ps.get_print_state_enum_subject())));

    bool is_snapmaker = false;
    if (AmsBackend* backend = AmsState::instance().get_backend()) {
        is_snapmaker = backend->get_type() == AmsType::SNAPMAKER;
    }

    PlrOfferSignals signals;
    signals.pl_env_valid = (pl_env_valid != 0);
    signals.printer_idle = idle;
    signals.is_snapmaker = is_snapmaker;
    signals.already_prompted = prompted_this_connect_;

    // Pure self-guard: false for non-Snapmaker, mid-print, or already-prompted.
    if (!helix::plr_should_offer(signals)) {
        return;
    }

    prompted_this_connect_ = true;
    spdlog::info("[PLR] Offering power-loss recovery (idle={}, snapmaker={})", idle, is_snapmaker);
    show_plr_recovery_prompt(get_moonraker_api());
}

void PlrOfferController::on_connection_state_changed(int new_conn_state) {
    if (helix::plr_should_rearm(last_conn_state_, new_conn_state)) {
        spdlog::debug("[PLR] Connection dropped — re-arming recovery offer latch");
        prompted_this_connect_ = false;
    }
    last_conn_state_ = new_conn_state;
}

} // namespace helix::ui
