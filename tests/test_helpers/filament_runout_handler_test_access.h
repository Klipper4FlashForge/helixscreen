// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ui_filament_runout_handler.h"

namespace helix::ui {

// Reaches FilamentRunoutHandler's private dispatch_load() / dispatch_unload() /
// dispatch_purge(), which production code only enters through the guidance
// modal's buttons — widgets that need a live modal, a screen, and a paused print
// to press. Declared a friend of FilamentRunoutHandler; follows the
// tests/test_helpers/ TestAccess pattern ([L088]) rather than adding
// _for_testing() accessors.
class FilamentRunoutHandlerTestAccess {
  public:
    static void dispatch_load(FilamentRunoutHandler& handler) {
        handler.dispatch_load();
    }
    static void dispatch_unload(FilamentRunoutHandler& handler) {
        handler.dispatch_unload();
    }
    static void dispatch_purge(FilamentRunoutHandler& handler) {
        handler.dispatch_purge();
    }
};

} // namespace helix::ui
