// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

namespace helix {

/**
 * @brief Whether the bypass controls should be available on this machine.
 *
 * Folds the user's force-bypass override into the firmware's own report. Four
 * places need this answer — the AmsState subject that drives the sidebar toggle,
 * the Device Operations section, the bypass node on the filament path, and the
 * backends' own enable_bypass() guards — and they must agree, or the toggle
 * appears and then refuses to act. bypass_node_visible() already exists because
 * that exact condition drifted across four render sites; this keeps the override
 * from repeating it.
 *
 * The firmware value is left untouched so the override can be switched off and
 * reality returns without a re-parse.
 *
 * @param firmware_supports_bypass AmsSystemInfo::supports_bypass, as reported
 * @param force_override User setting: offer bypass despite a firmware "no"
 * @return true when bypass UI and operations should be offered
 */
[[nodiscard]] constexpr bool bypass_available(bool firmware_supports_bypass, bool force_override) {
    return firmware_supports_bypass || force_override;
}

/// Gather bypass_available()'s override input from settings. Split from the pure
/// rule above so the rule is testable without standing up SettingsManager, which
/// is the same split bypass_node_visible() / bypass_node_visible_for() uses.
[[nodiscard]] bool bypass_available_for(bool firmware_supports_bypass);

} // namespace helix
