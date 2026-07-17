// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "touch_calibration.h"

#include <lvgl.h>

namespace helix {

/// Context for the calibrated touch read callback wrapper.
/// Owned by the display backend as a value member; the calibrated read callback
/// reaches it through a file-scope handle (NOT the indev's user_data — see
/// install_calibration_wrapper). Lifetime must match the backend object.
struct CalibrationContext {
    TouchCalibration calibration;
    lv_indev_read_cb_t original_read_cb = nullptr;
    int screen_width = 800;
    int screen_height = 480;
};

/// Read callback wrapper that applies affine touch calibration.
/// Chains to original_read_cb first, then transforms coordinates.
void calibrated_read_cb(lv_indev_t* indev, lv_indev_data_t* data);

/// Load stored touch calibration coefficients from Config.
/// Returns TouchCalibration with valid=false if none stored or invalid.
TouchCalibration load_touch_calibration();

/// Install the calibration read callback wrapper on an input device.
/// Sets up ctx with the calibration data, chains to the existing read callback,
/// and records ctx as the process-wide active calibration context (see the .cpp
/// for why user_data is deliberately not used). Safe to call even when cal.valid
/// is false (becomes a passthrough). There is only one calibrated touch indev
/// per process.
void install_calibration_wrapper(lv_indev_t* indev, CalibrationContext& ctx,
                                 const TouchCalibration& cal, int screen_w, int screen_h);

/// Tear down the wrapper installed by install_calibration_wrapper(): silence the
/// calibrated read callback on `indev` and drop the active-context handle if it
/// still refers to `ctx`. Call from the backend destructor before ctx is
/// destroyed — the indev outlives the backend (indevs are freed only at
/// lv_deinit), so its read callback must stop reaching a freed ctx.
void uninstall_calibration_wrapper(lv_indev_t* indev, CalibrationContext& ctx);

} // namespace helix
