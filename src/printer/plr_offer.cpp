// SPDX-License-Identifier: GPL-3.0-or-later
#include "plr_offer.h"

#include "moonraker_client.h" // ConnectionState

namespace helix {

bool plr_should_offer(const PlrOfferSignals& s) {
    return s.pl_env_valid && s.printer_idle && s.is_snapmaker && !s.already_prompted;
}

bool plr_should_rearm(int prev_conn_state, int new_conn_state) {
    auto prev = static_cast<ConnectionState>(prev_conn_state);
    auto next = static_cast<ConnectionState>(new_conn_state);
    return prev == ConnectionState::CONNECTED && next != ConnectionState::CONNECTED;
}

} // namespace helix
