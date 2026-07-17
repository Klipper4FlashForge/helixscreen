// SPDX-License-Identifier: GPL-3.0-or-later
#include "plr_offer.h"

#include "moonraker_client.h" // ConnectionState

namespace helix {

bool plr_should_offer(const PlrOfferSignals& s) {
    // pl_env_valid is self-gating — only Snapmaker's forked virtual_sdcard emits
    // it (see PlrOfferSignals docs), so it already implies "Snapmaker firmware
    // with a valid recovery snapshot." No separate printer-type/AMS-backend gate
    // is required; that gate was redundant on stock U1s and wrong for an
    // AFC-modded U1 (bundle UDZJQVQZ) whose AMS backend is not the Snapmaker one.
    return s.pl_env_valid && s.printer_idle && !s.already_prompted && !s.wizard_active;
}

bool plr_should_rearm(int prev_conn_state, int new_conn_state) {
    auto prev = static_cast<ConnectionState>(prev_conn_state);
    auto next = static_cast<ConnectionState>(new_conn_state);
    return prev == ConnectionState::CONNECTED && next != ConnectionState::CONNECTED;
}

} // namespace helix
