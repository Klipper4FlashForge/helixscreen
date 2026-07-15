// SPDX-License-Identifier: GPL-3.0-or-later
//
// Link stubs for v1-EXCLUDED UI subsystems whose symbols kept Core+AMS panels
// still reference ungated. These are the app-level analogue of the audit's
// platform stubs: the print-status / print-select panels embed the gcode
// preview, and the AMS panels embed the camera QR-scan overlay, but neither
// gates those call sites behind HELIX_HAS_GCODE_VIEWER / HELIX_HAS_CAMERA — so
// the symbols are demanded at link even though the implementations
// (ui_gcode_viewer.cpp under #if HELIX_HAS_GCODE_VIEWER, ui_overlay_qr_scanner
// .cpp under #if HELIX_HAS_CAMERA) compile to nothing here.
//
// Nothing on the idle hello-card path calls these; bodies are inert. Wiring the
// real UI (Task 6) should gate these call sites in the panels (main-tree
// #if HELIX_HAS_*), at which point this file shrinks or disappears. Documented
// in esp32p4-task-5-report.md.

#include "ui_gcode_viewer.h"
#include "ui_overlay_qr_scanner.h"

#include <cstddef>
#include <new>

// ---- gcode viewer C API (2D/3D preview; HELIX_HAS_GCODE_VIEWER=0) ----------
// extern "C" surface referenced by ui_panel_print_status / print-select views.
extern "C" {
void ui_gcode_viewer_register(void) {}
void ui_gcode_viewer_load_file(lv_obj_t*, const char*) {}
void ui_gcode_viewer_clear(lv_obj_t*) {}
bool ui_gcode_viewer_has_content(lv_obj_t*) {
    return false;
}
void ui_gcode_viewer_set_paused(lv_obj_t*, bool) {}
bool ui_gcode_viewer_is_paused(lv_obj_t*) {
    return false;
}
void ui_gcode_viewer_force_redraw(lv_obj_t*) {}
void ui_gcode_viewer_set_render_mode(lv_obj_t*, helix::GcodeViewerRenderMode) {}
bool ui_gcode_viewer_is_using_2d_mode(lv_obj_t*) {
    return true;
}
void ui_gcode_viewer_disable_streaming(lv_obj_t*) {}
void ui_gcode_viewer_reset_camera(lv_obj_t*) {}
void ui_gcode_viewer_set_load_callback(lv_obj_t*, gcode_viewer_load_callback_t, void*) {}
void ui_gcode_viewer_set_clear_callback(lv_obj_t*, ui_gcode_viewer_clear_cb_t, void*) {}
void ui_gcode_viewer_set_content_offset_y(lv_obj_t*, float) {}
int ui_gcode_viewer_get_max_layer(lv_obj_t*) {
    return 0;
}
const char* ui_gcode_viewer_get_filename(lv_obj_t*) {
    return nullptr;
}
void ui_gcode_viewer_set_print_progress(lv_obj_t*, int) {}
} // extern "C"

// C++-linkage gcode viewer surface (AMS tool-color + object-picking overlays).
const helix::gcode::ParsedGCodeFile* ui_gcode_viewer_get_parsed_file(lv_obj_t*) {
    return nullptr;
}
void ui_gcode_viewer_set_tool_colors(lv_obj_t*, const std::vector<uint32_t>&) {}
bool ui_gcode_viewer_apply_ams_tool_colors(lv_obj_t*) {
    return false;
}
void ui_gcode_viewer_set_excluded_objects(lv_obj_t*, const std::unordered_set<std::string>&) {}
void ui_gcode_viewer_set_highlighted_objects(lv_obj_t*, const std::unordered_set<std::string>&) {}
void ui_gcode_viewer_set_object_tap_callback(lv_obj_t*, void (*)(lv_obj_t*, const char*, void*),
                                             void*) {}
void ui_gcode_viewer_set_object_long_press_callback(lv_obj_t*,
                                                    void (*)(lv_obj_t*, const char*, void*),
                                                    void*) {}

// ---- camera QR-scan overlay (HELIX_HAS_CAMERA=0) --------------------------
// The AMS panels reach it through get_qr_scanner_overlay().show(...). The
// overlay is never constructed on the idle path, so return a reference to raw
// storage rather than construct a QrScannerOverlay (its members pull the
// excluded UsbScannerMonitor / camera stack). show()/show_for_active_spool are
// non-virtual no-ops — no vtable, no construction cascade.
namespace helix::ui {

void QrScannerOverlay::show(lv_obj_t*, int, ResultCallback, CancelCallback) {}
void QrScannerOverlay::show_for_active_spool(lv_obj_t*, ResultCallback, CancelCallback) {}

QrScannerOverlay& get_qr_scanner_overlay() {
    alignas(QrScannerOverlay) static unsigned char storage[sizeof(QrScannerOverlay)];
    return *reinterpret_cast<QrScannerOverlay*>(storage); // never dereferenced (idle path)
}

} // namespace helix::ui
