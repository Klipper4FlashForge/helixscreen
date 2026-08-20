// Copyright (C) 2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ui_panel_power.h"

#include "moonraker_types.h"

#include <vector>

/**
 * @brief Friend access to PowerPanel's device-row builder.
 *
 * populate_device_list() is normally reached from the Moonraker device fetch.
 * A test drives it directly so the row's locked state can be asserted without
 * standing up an RPC round trip - the lock decision is the unit, not the fetch.
 */
class PowerPanelTestAccess {
  public:
    static void populate(PowerPanel& panel, const std::vector<PowerDevice>& devices) {
        panel.populate_device_list(devices);
    }
};
