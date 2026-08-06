// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ui_filament_runout_handler.h"

namespace helix::ui {

// Reaches FilamentRunoutHandler's private dispatch_load(), which production
// code only enters through the guidance modal's "Load filament" button — a
// widget that needs a live modal, a screen, and a paused print to press.
// Declared a friend of FilamentRunoutHandler; follows the tests/test_helpers/
// TestAccess pattern ([L088]) rather than adding a _for_testing() accessor.
class FilamentRunoutHandlerTestAccess {
  public:
    static void dispatch_load(FilamentRunoutHandler& handler) {
        handler.dispatch_load();
    }
};

} // namespace helix::ui
