// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ui_emergency_stop.h"

/**
 * Reaches EmergencyStopOverlay's recovery state so tests can assert *when* it
 * changes, not just that a dialog eventually appears. Declared a friend in
 * ui_emergency_stop.h, following the existing TestAccess pattern
 * (tests/test_helpers/, [L088]) rather than adding a production accessor.
 *
 * The distinction matters for show_recovery_for(): the dialog is built via an
 * async hop either way, so "a dialog appeared" cannot tell a caller-thread
 * mutation apart from a marshalled one. recovery_reason_ can.
 */
class EmergencyStopOverlayTestAccess {
  public:
    static RecoveryReason recovery_reason(const EmergencyStopOverlay& o) {
        return o.recovery_reason_;
    }

    /// Reset to a known baseline so a test can observe the transition itself.
    static void reset_recovery_reason(EmergencyStopOverlay& o) {
        o.recovery_reason_ = RecoveryReason::NONE;
    }

    /// Clear the suppression deadline. The overlay is a process-wide singleton
    /// and Catch2 runs the suite in one process, so a test that suppresses must
    /// clear up after itself or every later test inherits the window.
    static void reset_suppression(EmergencyStopOverlay& o) {
        o.suppress_recovery_until_.store(0, std::memory_order_relaxed);
    }
};
