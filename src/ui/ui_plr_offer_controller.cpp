// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "plr_offer_controller.h"

#include "ui_plr_prompt.h"

#include "ams_backend.h"
#include "ams_state.h"
#include "ams_types.h"
#include "app_globals.h" // is_wizard_active
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
    // pl_env_valid that is ALREADY true when we register still offers via this
    // registration-fire; thereafter a genuine 0->1 edge offers. See
    // on_connection_state_changed for how reconnect manufactures that edge.
    pl_valid_observer_ = observe_int_sync(
        ps.get_pl_env_valid_subject(), this,
        [](PlrOfferController* self, int value) { self->on_pl_env_valid_changed(value); });

    conn_observer_ = observe_int_sync(
        ps.get_printer_connection_state_subject(), this,
        [](PlrOfferController* self, int value) { self->on_connection_state_changed(value); });

    // Wizard-active edge: re-evaluate when the wizard closes so a
    // wizard-suppressed offer fires. See evaluate_offer for the full rationale.
    wizard_observer_ = observe_int_sync(
        &get_wizard_active_subject(), this,
        [](PlrOfferController* self, int value) { self->on_wizard_active_changed(value); });
}

void PlrOfferController::evaluate_offer() {
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
    signals.pl_env_valid = lv_subject_get_int(ps.get_pl_env_valid_subject()) != 0;
    signals.printer_idle = idle;
    signals.is_snapmaker = is_snapmaker;
    signals.already_prompted = prompted_this_connect_;
    signals.wizard_active = is_wizard_active();

    // AUTHORITATIVE explanation of the offer's one-shot + re-fire behavior
    // (referenced from plr_offer.h, plr_offer_controller.h, and the observers):
    //
    // plr_should_offer is a pure self-guard: false for non-Snapmaker, mid-print,
    // already-prompted, or while the setup wizard owns the screen. Two
    // suppression cases resolve on their own because the latch below is set
    // ONLY on the success path:
    //   - Wizard active: prompted_this_connect_ stays unset, and the
    //     wizard-active subject's 1->0 edge routes back here the moment the
    //     wizard closes (on_wizard_active_changed), so the deferred offer fires.
    //   - Reconnect: on_connection_state_changed re-arms the latch AND forces
    //     pl_env_valid to 0, so the reconnect's status re-dispatch drives a real
    //     0->1 edge back into on_pl_env_valid_changed -> here. (Without the
    //     forced 0, pl_env_valid stays 1 across the reconnect and
    //     lv_subject_set_int's changed-guard swallows the same-value write, so
    //     no edge would ever arrive.)
    if (!helix::plr_should_offer(signals)) {
        return;
    }

    prompted_this_connect_ = true;
    spdlog::info("[PLR] Offering power-loss recovery (idle={}, snapmaker={})", idle, is_snapmaker);
    show_plr_recovery_prompt(get_moonraker_api());
}

void PlrOfferController::on_pl_env_valid_changed(int /*pl_env_valid*/) {
    // evaluate_offer reads pl_env_valid straight from the subject, so the
    // notified value is not needed here — kept for the observer signature.
    evaluate_offer();
}

void PlrOfferController::on_connection_state_changed(int new_conn_state) {
    if (helix::plr_should_rearm(last_conn_state_, new_conn_state)) {
        spdlog::debug("[PLR] Connection dropped — re-arming recovery offer latch");
        prompted_this_connect_ = false;

        // Force pl_env_valid back to 0 (and drop the stale recovery file) so the
        // reconnect's full status re-dispatch produces a genuine 0->1 edge that
        // re-fires on_pl_env_valid_changed. Safe on the main thread — observer
        // callbacks are queue-deferred, so this runs in a later batch than any
        // AMS backend re-creation, keeping the is_snapmaker gate race-immune.
        auto& ps = get_printer_state();
        lv_subject_set_int(ps.get_pl_env_valid_subject(), 0);
        ps.clear_pl_recovery_file();
    }
    last_conn_state_ = new_conn_state;
}

void PlrOfferController::on_wizard_active_changed(int wizard_active) {
    // Only the wizard-closed edge matters: an offer suppressed solely because
    // the wizard owned the screen can now fire. evaluate_offer re-applies every
    // other guard (already-prompted, idle, snapmaker), so a spurious re-eval is
    // harmless.
    if (wizard_active == 0) {
        evaluate_offer();
    }
}

} // namespace helix::ui
