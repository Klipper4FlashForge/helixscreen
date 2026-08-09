// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace helix {

class PrinterState;

/**
 * @brief Whether the toolhead reports all three axes homed.
 *
 * Reads the live `homed_axes` subject, which is fed off `objects.subscribe` /
 * `notify_status_update` (src/printer/printer_motion_state.cpp:126) and seeded
 * from the subscribe response (src/api/moonraker_discovery_sequence.cpp:1487).
 * No RPC is issued.
 *
 * @warning Main thread only. This reads an LVGL subject.
 */
[[nodiscard]] bool toolhead_is_homed(const PrinterState& ps);

} // namespace helix
