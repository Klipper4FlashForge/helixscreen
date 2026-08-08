// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_widget_size_camera.cpp
 * @brief camera picks compact (icon-only) vs. live-stream layout from
 * physical pixels, not colspan/rowspan — and unlike every other migrated
 * widget, the transition is edge-triggered around a network resource: it
 * starts/stops an MJPEG stream on the compact<->non-compact boundary
 * (camera_widget.cpp on_size_changed(), :256-287).
 *
 * No network I/O happens in this file. CameraWidget::start_stream() builds a
 * CameraStream and calls CameraStream::configure_from_printer(), which reads
 * PrinterState's webcam stream/snapshot URLs and returns false when both are
 * empty (src/system/camera_stream.cpp:86-107) — start_stream() then resets
 * stream_ and returns *before* CameraStream::start() (the call that opens a
 * socket) is ever reached. The global PrinterState singleton
 * (get_printer_state(), shared process-wide across the test binary) has no
 * webcam configured by default, and the one other test file that touches
 * PrinterCapabilitiesState::set_webcam_available()
 * (test_queue_update_lifetime.cpp) does so on a locally-constructed
 * PrinterCapabilitiesState, never the global singleton — so this precondition
 * cannot be polluted by test order. The REQUIRE at the top of the test case
 * pins that guarantee rather than trusting it silently.
 *
 * Because start_stream()/stop_stream() are private and produce no externally
 * observable effect when no camera is configured (the interesting branch
 * bodies below the URL check never run), the edge-trigger property is
 * verified through the OTHER state on_size_changed() mutates in the same
 * branch: camera_image_'s source (cleared on entering compact) and
 * camera_status_'s hidden flag (cleared on leaving compact). Both are
 * artificially set to a sentinel value between two same-target resizes so a
 * spurious re-run of the branch is directly observable — this is the closest
 * available proxy for "the stream lifecycle only fires on the edge" without
 * touching the network.
 */

#include "lvgl.h"
#include "panel_widget.h"
#include "panel_widget_registry.h"

#include "../catch_amalgamated.hpp"

#if HELIX_HAS_CAMERA

#include "../lvgl_ui_test_fixture.h"
#include "../test_helpers/panel_widget_size_harness.h"
#include "app_globals.h"
#include "panel_widget_manager.h"
#include "panel_widget_size.h"
#include "printer_state.h"
#include "src/ui/panel_widgets/camera_widget.h"

using namespace helix;
using namespace helix::widget_size;

TEST_CASE_METHOD(LVGLUITestFixture,
                 "camera compact/live layout follows pixels, not spans, and stream "
                 "start/stop stays edge-triggered",
                 "[widget_size][camera]") {
    // No webcam configured on the shared global PrinterState -> start_stream()
    // bails out inside CameraStream::configure_from_printer() before any
    // socket is opened. See the file comment above for why this cannot be
    // polluted by test order.
    REQUIRE(get_printer_state().get_webcam_stream_url().empty());
    REQUIRE(get_printer_state().get_webcam_snapshot_url().empty());

    // Widget-owned subjects (camera_status_text) are registered lazily; the
    // harness alone does not trigger it, and panel_widget_camera.xml's
    // camera_status label binds to it.
    PanelWidgetManager::instance().init_widget_subjects();

    PanelWidgetHarness<CameraWidget> h(test_screen());

    lv_obj_t* camera_image = h.child("camera_image");
    lv_obj_t* camera_overlay = h.child("camera_overlay");
    lv_obj_t* camera_status = h.child("camera_status");
    REQUIRE(camera_image != nullptr);
    REQUIRE(camera_overlay != nullptr);
    REQUIRE(camera_status != nullptr);

    // --- Entering compact: width AND height both below floor. Contradicting
    // span: 4x4 (old predicate: colspan<=1 && rowspan<=1 -> false, not
    // compact). Seed a non-null image source first (a real, decodable asset —
    // lv_image_set_src() rejects a fabricated path via
    // lv_image_decoder_get_info() and silently leaves src null, see
    // lv_image.c:180-201) so clearing it on this transition is a real
    // assertion, not a check against an already-null default.
    lv_image_set_src(camera_image, "A:assets/images/ams/spoolman_64.png");
    REQUIRE(lv_image_get_src(camera_image) != nullptr);

    h.resize(4, 4, W_NORMAL - 1, H_TALL - 1);
    process_lvgl(30);

    CHECK(lv_image_get_src(camera_image) == nullptr);
    CHECK_FALSE(lv_obj_has_flag(camera_overlay, LV_OBJ_FLAG_HIDDEN)); // icon overlay shown
    CHECK(lv_obj_has_flag(camera_status, LV_OBJ_FLAG_HIDDEN));        // status text hidden

    // --- Edge-trigger check (entering-compact direction): resize to the
    // EXACT SAME compact target again (no transition — compact_ was already
    // true). Re-seed the sentinel source; a correct edge-triggered
    // implementation must NOT re-enter the "compact && !was_compact" branch,
    // so the sentinel must survive.
    lv_image_set_src(camera_image, "A:assets/images/ams/spoolman_24.png");
    h.resize(3, 3, W_NORMAL - 1, H_TALL - 1); // spans vary, pixels identical
    process_lvgl(30);

    CHECK(lv_image_get_src(camera_image) != nullptr); // NOT re-cleared

    // --- Leaving compact: width AND height both at/over their floor.
    // Contradicting span: 1x1 (old predicate -> compact stays true).
    h.resize(1, 1, W_WIDE, H_TALLER);
    process_lvgl(30);

    CHECK_FALSE(lv_obj_has_flag(camera_status, LV_OBJ_FLAG_HIDDEN)); // status text shown again

    // --- Edge-trigger check (leaving-compact direction): manually re-hide
    // camera_status, then resize to the EXACT SAME non-compact target again
    // (no transition — compact_ was already false). A correct edge-triggered
    // implementation must NOT re-enter the "!compact_ && was_compact" branch,
    // so the manually-set hidden flag must survive.
    lv_obj_add_flag(camera_status, LV_OBJ_FLAG_HIDDEN);
    h.resize(2, 2, W_WIDE, H_TALLER); // spans vary, pixels identical
    process_lvgl(30);

    CHECK(lv_obj_has_flag(camera_status, LV_OBJ_FLAG_HIDDEN)); // NOT re-shown
}

#endif // HELIX_HAS_CAMERA
