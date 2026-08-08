// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025-2026 356C LLC

#include "touch_calibration_session.h"

#include <spdlog/spdlog.h>

namespace helix {

void TouchCalibrationSession::begin_capture(ICalibrationSink& sink) {
    backup_ = sink.current_calibration();
    has_backup_ = true;
    sink.disable_affine();
    spdlog::debug("[TouchCalSession] begin_capture: backup snapshotted (valid={}), affine disabled",
                  backup_.valid);
}

void TouchCalibrationSession::revert_for_retry(ICalibrationSink& sink) {
    bool reverted = has_backup_ && backup_.valid;
    if (reverted) {
        sink.apply_calibration(backup_);
    } else if (has_backup_) {
        // The session began on an uncalibrated device, so there is no backup to
        // re-apply — but the device is no longer uncalibrated: VERIFY installed
        // the candidate matrix in the stored slot. disable_affine() below only
        // suppresses it, so the next enable_affine() would install a matrix the
        // user never accepted. Drop it instead.
        sink.clear_calibration();
    }
    sink.disable_affine();
    spdlog::debug("[TouchCalSession] revert_for_retry: restored backup={}, affine disabled",
                  reverted);
}

void TouchCalibrationSession::commit() {
    has_backup_ = false;
    backup_ = {};
    spdlog::debug("[TouchCalSession] commit: backup dropped (new calibration kept)");
}

void TouchCalibrationSession::restore(ICalibrationSink& sink) {
    bool reverted = has_backup_ && backup_.valid;
    if (reverted) {
        sink.apply_calibration(backup_);
    } else if (has_backup_) {
        // No prior calibration existed, so "restore" means "make the device
        // uncalibrated again", not "leave whatever is stored". VERIFY puts the
        // candidate matrix in the stored slot so the user can test it; without
        // this the enable_affine() below would make that unaccepted matrix live
        // for the rest of the session — the same shape of bug as the original
        // #943 report, where a failed recalibration changed touch until reboot.
        sink.clear_calibration();
    }
    // Always re-enable the affine transform, even when no valid backup was held
    // (first-run wizard) or no session was active — touch must never be left in
    // the disabled state a capture session puts it in (#943). With the stored
    // calibration cleared above, "enabled" resolves to the uncalibrated
    // pass-through the device started from.
    sink.enable_affine();
    has_backup_ = false;
    backup_ = {};
    spdlog::debug("[TouchCalSession] restore: restored backup={}, affine re-enabled", reverted);
}

} // namespace helix
