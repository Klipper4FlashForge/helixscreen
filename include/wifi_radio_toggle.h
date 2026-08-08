// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

namespace helix::wifi {

/**
 * @brief What a UI surface must do once a radio toggle's real outcome is known.
 *
 * The WiFi switch flips optimistically: the subject moves the instant the user
 * touches it, because driving the radio can block for tens of seconds
 * (wpa_ctrl retries a send for up to 5s and then waits up to 10s for a reply,
 * twice per direction). Reality arrives later, and this describes how to fold
 * it back in.
 */
struct RadioToggleOutcome {
    /// Value the wifi_enabled subject must be reconciled to. Always the
    /// radio's real state, never the request — persisting the request would
    /// feed a false "off" (or "on") into the startup reassert.
    bool enabled = false;

    /// The optimistic flip disagreed with reality and has to be undone.
    bool reverted = false;

    /// Reverted even though the backend claimed the change succeeded. Nothing
    /// has told the user anything in that case (the failure toast only fires
    /// on a reported error), so the caller must say something itself.
    bool silent_revert = false;
};

/**
 * @brief Fold a radio toggle's real outcome into a UI decision.
 *
 * @param requested What the user asked for (the optimistic value already shown)
 * @param success   Whether the backend reported the change as applied
 * @param actual    The radio state read back from the backend afterwards
 */
inline RadioToggleOutcome reconcile_radio_toggle(bool requested, bool success, bool actual) {
    RadioToggleOutcome out;
    out.enabled = actual;
    out.reverted = (requested != actual);
    out.silent_revert = out.reverted && success;
    return out;
}

} // namespace helix::wifi
