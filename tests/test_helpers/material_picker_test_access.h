// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ui_material_picker_menu.h"

#include <string>

namespace helix::ui {

// Test-only accessor for MaterialPickerMenu's private dispatch methods.
class MaterialPickerTestAccess {
  public:
    static void dispatch_select(MaterialPickerMenu& m, const std::string& s) {
        m.dispatch_select(s);
    }
    static void dispatch_reset(MaterialPickerMenu& m) {
        m.dispatch_reset();
    }
};

} // namespace helix::ui
