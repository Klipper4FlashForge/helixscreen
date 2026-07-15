// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace helix {

/// Raw inputs for the connect-time Power-Loss-Recovery offer decision, all
/// sourced from PrinterState. Snapmaker-fork firmware only (see PrinterState
/// pl_env_valid docs) — pl_env_valid stays false on every other backend.
struct PlrOfferSignals {
    bool pl_env_valid;     ///< virtual_sdcard.pl_env_valid
    bool printer_idle;     ///< no active or paused print right now
    bool is_snapmaker;     ///< connected printer is a Snapmaker U1 (or PAXX)
    bool already_prompted; ///< one-shot latch: already offered this connection
};

/// Should HelixScreen offer to resume an interrupted print? Pure: no LVGL,
/// threading, or singletons.
bool plr_should_offer(const PlrOfferSignals& signals);

/// Should the one-shot "already_prompted" latch be re-armed? True only on a
/// CONNECTED -> not-CONNECTED transition, so a disconnect/reconnect cycle
/// offers again instead of staying latched from the prior session.
/// Takes raw `helix::ConnectionState` values as int to keep this header free
/// of the MoonrakerClient dependency; callers pass the real enum values.
bool plr_should_rearm(int prev_conn_state, int new_conn_state);

} // namespace helix
