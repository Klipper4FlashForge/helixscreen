// Copyright (C) 2025-2026 356C LLC
// tests/test_helpers/toolchanger_test_access.h
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ams_backend_toolchanger.h"
#include "ams_error.h"

#include <mutex>
#include <string>
#include <utility>

// Friend-class shim for AmsBackendToolChanger -- declared as friend in the
// backend header (`friend class ToolChangerTestAccess;`). Gives tests direct
// access to the private optimistic-dispatch surface (dispatch_operation) that
// AmsBackendToolChanger::change_tool() normally guards behind
// check_preconditions()/validate_slot_index(), which need a fully configured
// tool topology this shim lets a bare backend skip. Same pattern as
// CfsTestAccess (tests/test_helpers/cfs_test_access.h).
class ToolChangerTestAccess {
  public:
    /// Call the REAL dispatch_operation implementation directly: sets the
    /// AmsAction optimistically (begin_dispatch_locked), routes @p gcode
    /// through ensure_homed_then(), and resolves on the macro's ack -- or
    /// undoes the optimistic set if the gcode never left (dispatch_operation's
    /// own `if (!result) abandon_dispatch(...)` net).
    static AmsError call_dispatch_operation(AmsBackendToolChanger& b, std::string gcode,
                                            AmsAction action) {
        return b.dispatch_operation(std::move(gcode), action);
    }

    /// Whether an optimistic dispatch is still armed and awaiting resolution.
    /// A cancelled home confirmation must clear this -- otherwise the next
    /// macro ack (or a superseding dispatch) resolves against a generation
    /// that no longer describes anything in flight.
    static bool has_pending_dispatch(const AmsBackendToolChanger& b) {
        std::lock_guard<std::mutex> lock(b.mutex_);
        return b.pending_dispatch_action_.has_value();
    }
};
