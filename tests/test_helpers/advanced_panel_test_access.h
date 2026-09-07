// Copyright (C) 2025-2026 356C LLC
// tests/test_helpers/advanced_panel_test_access.h
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ui_panel_advanced.h"

#include <utility>

namespace helix::ui {

// Test-only access to AdvancedPanel's HelixPrint plugin uninstall flow.
//
// The uninstall row's confirm step forks the bundled install.sh and blocks
// until it exits, so a test drives the ACTUAL row handler and swaps only the
// final step for a recorder: the dialog, its answers, and the state/toast
// follow-through stay the production code.
struct AdvancedPanelTestAccess {
    using UninstallRunner = AdvancedPanel::UninstallRunner;

    /// What a tap on row_helix_plugin_uninstall dispatches to.
    static void tap_uninstall_row(AdvancedPanel& panel) {
        panel.handle_helix_plugin_uninstall_clicked();
    }

    static void set_uninstall_runner(AdvancedPanel& panel, UninstallRunner runner) {
        panel.uninstall_runner_ = std::move(runner);
    }
};

} // namespace helix::ui
