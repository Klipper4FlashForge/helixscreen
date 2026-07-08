// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_ams_edit_overlay.h"
#include "ui_nav_manager.h"
#include "ui_update_queue.h"

#include "../lvgl_test_fixture.h"
#include "../lvgl_ui_test_fixture.h"
#include "moonraker_api_mock.h"
#include "moonraker_client_mock.h"
#include "printer_state.h"
#include "spoolman_slot_saver.h"

#include "../catch_amalgamated.hpp"

using namespace helix;
using namespace helix::ui;

// ============================================================================
// TestAccess helper — exposes AmsEditOverlay internals for white-box testing
// ============================================================================

class AmsEditOverlayTestAccess {
  public:
    explicit AmsEditOverlayTestAccess(AmsEditOverlay& overlay) : overlay_(overlay) {}

    void set_original_info(const SlotInfo& info) {
        overlay_.original_info_ = info;
    }
    void set_working_info(const SlotInfo& info) {
        overlay_.working_info_ = info;
    }
    void set_api(MoonrakerAPI* api) {
        overlay_.api_ = api;
    }
    void set_slot_index(int idx) {
        overlay_.slot_index_ = idx;
    }
    void set_filament_user_edited(bool edited) {
        overlay_.filament_user_edited_ = edited;
    }
    void set_completion_callback(AmsEditOverlay::CompletionCallback cb) {
        overlay_.completion_callback_ = std::move(cb);
        overlay_.completion_fired_ = false;
    }

    void init_subjects() {
        overlay_.init_subjects();
    }

    // View subject: 0 = overview/form, 1 = Spoolman picker (kView* constants).
    int get_view_mode() {
        return lv_subject_get_int(&overlay_.view_mode_subject_);
    }
    void call_handle_save() {
        overlay_.handle_save();
    }
    void call_handle_back() {
        overlay_.handle_back();
    }
    bool completion_fired() const {
        return overlay_.completion_fired_;
    }

    // Forward the private static create-gate predicate (friend access).
    static bool should_create_new_spool(const SlotInfo& working_info, bool filament_user_edited) {
        return AmsEditOverlay::should_create_new_spool(working_info, filament_user_edited);
    }
    static bool is_material_identity_change(const SlotInfo& original, const SlotInfo& edited) {
        return AmsEditOverlay::is_material_identity_change(original, edited);
    }

  private:
    AmsEditOverlay& overlay_;
};

// ============================================================================
// Tests: handle_save syncs active spool with Moonraker
// ============================================================================

TEST_CASE_METHOD(LVGLTestFixture, "handle_save sets active spool when spool assigned (0 -> N)",
                 "[ams_edit_overlay][spoolman][active_spool]") {
    PrinterState state;
    MoonrakerClientMock client;
    MoonrakerAPIMock api(client, state);

    api.spoolman_mock().set_active_spool(0, nullptr, nullptr);
    REQUIRE(api.spoolman_mock().get_mock_active_spool_id() == 0);

    AmsEditOverlay overlay;
    AmsEditOverlayTestAccess access(overlay);
    access.init_subjects();

    SlotInfo original;
    original.spoolman_id = 0;

    SlotInfo working;
    working.spoolman_id = 42;

    access.set_original_info(original);
    access.set_working_info(working);
    access.set_api(&api);
    access.set_slot_index(-2);

    bool completion_fired = false;
    access.set_completion_callback([&](const AmsEditOverlay::EditResult& result) {
        completion_fired = true;
        REQUIRE(result.saved);
    });

    access.call_handle_save();
    UpdateQueue::instance().drain();

    REQUIRE(completion_fired);
    REQUIRE(api.spoolman_mock().get_mock_active_spool_id() == 42);
}

TEST_CASE_METHOD(LVGLTestFixture, "handle_save sets active spool when spool changed (N -> M)",
                 "[ams_edit_overlay][spoolman][active_spool]") {
    PrinterState state;
    MoonrakerClientMock client;
    MoonrakerAPIMock api(client, state);

    api.spoolman_mock().set_active_spool(42, nullptr, nullptr);

    AmsEditOverlay overlay;
    AmsEditOverlayTestAccess access(overlay);
    access.init_subjects();

    SlotInfo original;
    original.spoolman_id = 42;

    SlotInfo working;
    working.spoolman_id = 99;

    access.set_original_info(original);
    access.set_working_info(working);
    access.set_api(&api);
    access.set_slot_index(-2);

    bool completion_fired = false;
    access.set_completion_callback(
        [&](const AmsEditOverlay::EditResult&) { completion_fired = true; });

    access.call_handle_save();
    UpdateQueue::instance().drain();

    REQUIRE(completion_fired);
    REQUIRE(api.spoolman_mock().get_mock_active_spool_id() == 99);
}

TEST_CASE_METHOD(LVGLTestFixture, "handle_save clears active spool when spool unlinked (N -> 0)",
                 "[ams_edit_overlay][spoolman][active_spool]") {
    PrinterState state;
    MoonrakerClientMock client;
    MoonrakerAPIMock api(client, state);

    api.spoolman_mock().set_active_spool(42, nullptr, nullptr);

    AmsEditOverlay overlay;
    AmsEditOverlayTestAccess access(overlay);
    access.init_subjects();

    SlotInfo original;
    original.spoolman_id = 42;

    SlotInfo working;
    working.spoolman_id = 0;

    access.set_original_info(original);
    access.set_working_info(working);
    access.set_api(&api);
    access.set_slot_index(-2);

    bool completion_fired = false;
    access.set_completion_callback(
        [&](const AmsEditOverlay::EditResult&) { completion_fired = true; });

    access.call_handle_save();
    UpdateQueue::instance().drain();

    REQUIRE(completion_fired);
    REQUIRE(api.spoolman_mock().get_mock_active_spool_id() == 0);
}

TEST_CASE_METHOD(LVGLTestFixture, "handle_save re-syncs active spool on unchanged linked save",
                 "[ams_edit_overlay][spoolman][active_spool]") {
    PrinterState state;
    MoonrakerClientMock client;
    MoonrakerAPIMock api(client, state);

    // Simulate Moonraker having lost the active-spool state (e.g. after restart).
    api.spoolman_mock().set_active_spool(7, nullptr, nullptr);

    AmsEditOverlay overlay;
    AmsEditOverlayTestAccess access(overlay);
    access.init_subjects();

    SlotInfo original;
    original.spoolman_id = 42;

    SlotInfo working;
    working.spoolman_id = 42; // Same spool — re-save, not a change

    access.set_original_info(original);
    access.set_working_info(working);
    access.set_api(&api);
    access.set_slot_index(-2);

    bool completion_fired = false;
    access.set_completion_callback(
        [&](const AmsEditOverlay::EditResult&) { completion_fired = true; });

    access.call_handle_save();
    UpdateQueue::instance().drain();

    REQUIRE(completion_fired);
    // Re-save always re-syncs so Moonraker recovers lost state.
    REQUIRE(api.spoolman_mock().get_mock_active_spool_id() == 42);
}

TEST_CASE_METHOD(LVGLTestFixture, "handle_save does NOT crash when no API available",
                 "[ams_edit_overlay][spoolman][active_spool]") {
    AmsEditOverlay overlay;
    AmsEditOverlayTestAccess access(overlay);
    access.init_subjects();

    SlotInfo original;
    original.spoolman_id = 0;

    SlotInfo working;
    working.spoolman_id = 42;

    access.set_original_info(original);
    access.set_working_info(working);
    access.set_api(nullptr);
    access.set_slot_index(-2);

    bool completion_fired = false;
    access.set_completion_callback([&](const AmsEditOverlay::EditResult& result) {
        completion_fired = true;
        REQUIRE(result.saved);
    });

    access.call_handle_save();
    UpdateQueue::instance().drain();
    REQUIRE(completion_fired);
}

// ============================================================================
// Tests: completion is fired exactly once (covered-vs-dismissed correctness)
// ============================================================================

TEST_CASE_METHOD(LVGLTestFixture, "fire_completion is idempotent via completion_fired_",
                 "[ams_edit_overlay][lifecycle]") {
    AmsEditOverlay overlay;
    AmsEditOverlayTestAccess access(overlay);
    access.init_subjects();

    SlotInfo info;
    access.set_original_info(info);
    access.set_working_info(info);
    access.set_api(nullptr);
    access.set_slot_index(0);

    int fire_count = 0;
    access.set_completion_callback([&](const AmsEditOverlay::EditResult&) { fire_count++; });

    // Save fires completion; the overlay-close safety net firing afterwards
    // (backdrop tap path) must be a no-op.
    access.call_handle_save();
    UpdateQueue::instance().drain();
    REQUIRE(fire_count == 1);
    REQUIRE(access.completion_fired());

    access.call_handle_back(); // back after completion: must not re-fire
    UpdateQueue::instance().drain();
    REQUIRE(fire_count == 1);
}

// ============================================================================
// Tests: #1071 create-gate + identity-change predicates (unchanged semantics
// in this phase — Phase 5 rewrites the gate to the Save-to-Spoolman toggle)
// ============================================================================

TEST_CASE("AmsEditOverlay::should_create_new_spool gates new-spool creation on a real edit (#1071)",
          "[ams_edit_overlay][spoolman][1071]") {
    SlotInfo working;
    working.spoolman_id = 0;
    working.brand = "Generic";
    working.material = "PLA";
    working.color_rgb = 0xFF0000;
    REQUIRE(helix::SpoolmanSlotSaver::is_filament_complete(working));

    CHECK_FALSE(AmsEditOverlayTestAccess::should_create_new_spool(working, /*edited=*/false));
    CHECK(AmsEditOverlayTestAccess::should_create_new_spool(working, /*edited=*/true));

    SlotInfo linked = working;
    linked.spoolman_id = 99;
    CHECK_FALSE(AmsEditOverlayTestAccess::should_create_new_spool(linked, /*edited=*/true));

    SlotInfo incomplete;
    incomplete.spoolman_id = 0;
    incomplete.material = "PLA"; // no brand, default color
    REQUIRE_FALSE(helix::SpoolmanSlotSaver::is_filament_complete(incomplete));
    CHECK_FALSE(AmsEditOverlayTestAccess::should_create_new_spool(incomplete, /*edited=*/true));
}

TEST_CASE("AmsEditOverlay::is_material_identity_change flags different-spool edits (#1071)",
          "[ams_edit_overlay][spoolman][1071]") {
    SlotInfo original;
    original.material = "PLA";
    original.color_rgb = 0xFF0000;

    SlotInfo same = original;
    CHECK_FALSE(AmsEditOverlayTestAccess::is_material_identity_change(original, same));

    SlotInfo nudged = original;
    nudged.color_rgb = 0xFE0101;
    CHECK_FALSE(AmsEditOverlayTestAccess::is_material_identity_change(original, nudged));

    SlotInfo recased = original;
    recased.material = "pla";
    CHECK_FALSE(AmsEditOverlayTestAccess::is_material_identity_change(original, recased));

    SlotInfo diff_mat = original;
    diff_mat.material = "PETG";
    CHECK(AmsEditOverlayTestAccess::is_material_identity_change(original, diff_mat));

    SlotInfo diff_color = original;
    diff_color.color_rgb = 0x0000FF;
    CHECK(AmsEditOverlayTestAccess::is_material_identity_change(original, diff_color));
}

// ============================================================================
// Tests: #1071 initial-view routing on the singleton overlay. show_for_slot
// pushes via NavigationManager (async) — drain before asserting, and pop the
// overlay afterwards so the next test starts clean.
// ============================================================================

TEST_CASE_METHOD(LVGLUITestFixture,
                 "show_for_slot opens on the Spoolman picker when requested (#1071)",
                 "[ams_edit_overlay][spoolman][ui_integration][1071]") {
    auto& overlay = get_ams_edit_overlay();
    AmsEditOverlayTestAccess access(overlay);

    SlotInfo info;
    info.slot_index = 0;

    REQUIRE(overlay.show_for_slot(test_screen(), 0, info, api(), nullptr,
                                  /*open_on_picker=*/true));
    UpdateQueue::instance().drain();
    process_lvgl(10);

    CHECK(access.get_view_mode() == AmsEditOverlay::kViewSpoolPicker);

    NavigationManager::instance().go_back();
    UpdateQueue::instance().drain();
    process_lvgl(10);
}

TEST_CASE_METHOD(LVGLUITestFixture, "show_for_slot opens on the overview by default (#1071)",
                 "[ams_edit_overlay][spoolman][ui_integration][1071]") {
    auto& overlay = get_ams_edit_overlay();
    AmsEditOverlayTestAccess access(overlay);

    SlotInfo info;
    info.slot_index = 0;

    REQUIRE(overlay.show_for_slot(test_screen(), 0, info, api(), nullptr));
    UpdateQueue::instance().drain();
    process_lvgl(10);

    CHECK(access.get_view_mode() == AmsEditOverlay::kViewOverview);

    NavigationManager::instance().go_back();
    UpdateQueue::instance().drain();
    process_lvgl(10);
}
