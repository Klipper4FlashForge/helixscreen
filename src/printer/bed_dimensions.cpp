// SPDX-License-Identifier: GPL-3.0-or-later

#include "bed_dimensions.h"

#include "i_moonraker_api.h"
#include "printer_state.h"

namespace helix {

BedDimensions bed_dimensions_from_volume(float x_min, float x_max, float y_min, float y_max) {
    const float w = x_max - x_min;
    const float h = y_max - y_min;
    if (w > 0.0f && h > 0.0f) {
        return BedDimensions{w, h, x_min, y_min};
    }
    return BedDimensions{kDefaultBedSizeMm, kDefaultBedSizeMm, 0.0f, 0.0f};
}

BedDimensions bed_dimensions(IMoonrakerAPI* api, const PrinterState* ps) {
    if (api) {
        const auto& vol = api->hardware().build_volume();
        auto d = bed_dimensions_from_volume(vol.x_min, vol.x_max, vol.y_min, vol.y_max);
        if (d.w_mm != kDefaultBedSizeMm || d.h_mm != kDefaultBedSizeMm) {
            return d;
        }
    }
    if (ps) {
        const auto b = ps->get_axis_bounds();
        if (b.has_x && b.has_y) {
            return bed_dimensions_from_volume(b.x_min, b.x_max, b.y_min, b.y_max);
        }
    }
    return BedDimensions{kDefaultBedSizeMm, kDefaultBedSizeMm, 0.0f, 0.0f};
}

} // namespace helix
