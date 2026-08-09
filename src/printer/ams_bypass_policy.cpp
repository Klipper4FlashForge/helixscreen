// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ams_bypass_policy.h"

#include "settings_manager.h"

namespace helix {

bool bypass_available_for(bool firmware_supports_bypass) {
    if (firmware_supports_bypass) {
        return true; // no need to touch settings when the firmware already says yes
    }
    return bypass_available(firmware_supports_bypass,
                            SettingsManager::instance().get_ams_force_bypass_controls());
}

} // namespace helix
