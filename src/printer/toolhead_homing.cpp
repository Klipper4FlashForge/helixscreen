// SPDX-License-Identifier: GPL-3.0-or-later
#include "toolhead_homing.h"

#include "printer_state.h"

#include <cstring>
#include <string>

namespace helix {

bool toolhead_is_homed(const PrinterState& ps) {
    // Klipper reports homed_axes as a subset of "xyz". Anything short of all
    // three is not homed for our purposes: every caller needs full XYZ before
    // it can move the toolhead safely.
    const char* axes =
        lv_subject_get_string(const_cast<PrinterState&>(ps).get_homed_axes_subject());
    if (axes == nullptr) {
        return false;
    }
    const std::string s(axes);
    return s.find('x') != std::string::npos && s.find('y') != std::string::npos &&
           s.find('z') != std::string::npos;
}

} // namespace helix
