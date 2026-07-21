// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// ZMOD persistent z-offset enablement.
//
// ZMOD is a Klipper add-on (common on some vendor firmwares) that can persist a
// live z-offset adjustment across prints and reboots. It exposes the gcode macro
// SAVE_ZMOD_DATA. ZMOD's SET_GCODE_OFFSET override already SAVES any z-offset the
// user dials in, but reloading that saved value on the next print is OFF by
// default. Sending `SAVE_ZMOD_DATA LOAD_ZOFFSET=1` flips reload on so z-offset
// adjustments made in HelixScreen actually survive across prints/reboots.
//
// We want to send this exactly once per app session, only when the printer is
// idle (never mid-print, where a gcode injection could disturb an active job).
// This header exposes the send decision as a pure predicate so it can be
// unit-tested without constructing an Application.

namespace helix {
namespace zmod {

// Returns true if we should send `SAVE_ZMOD_DATA LOAD_ZOFFSET=1` now.
//
//   has_zmod_macro - printer exposes the SAVE_ZMOD_DATA macro (detection gate)
//   print_active   - a print is currently running (idle gate; never mid-print)
//   already_sent   - we already enabled it this session (once-per-session guard)
inline bool should_enable_persistent_zoffset(bool has_zmod_macro, bool print_active,
                                             bool already_sent) {
    return has_zmod_macro && !print_active && !already_sent;
}

} // namespace zmod
} // namespace helix
