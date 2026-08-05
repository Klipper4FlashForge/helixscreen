// tests/test_helpers/bed_mesh_panel_test_access.h
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ui_panel_bed_mesh.h"

namespace helix {
namespace ui {

// Test-only access to BedMeshPanel's private canvas pointer.
//
// Exists specifically for test_bed_mesh_orientation_flip.cpp, which drives a
// real ui_is_portrait flip through a live BedMeshPanel to prove
// canvas_/SIZE_CHANGED survive bed_mesh_panel.xml's reactive <if> rebuilding
// overlay_content in place — the fix for the canvas_ use-after-free
// (reactive <if> teardown condemns the old overlay_content/bed_mesh_canvas;
// nothing else nulled the cached raw pointer).
struct BedMeshPanelTestAccess {
    static lv_obj_t* canvas(const BedMeshPanel& p) {
        return p.canvas_;
    }

    /// Invokes the private wiring method under test directly, without going
    /// through create() (which needs a fully XML-registered app + Moonraker).
    static bool wire(BedMeshPanel& p, lv_obj_t* overlay_content) {
        return p.wire_canvas_and_content(overlay_content);
    }
};

} // namespace ui
} // namespace helix
