// SPDX-License-Identifier: GPL-3.0-or-later
//
// View-level tests for the AMS slot editor overlay redesign: identity chip
// contents (tracked vs untracked), managed-state subject, pre-selection, and
// view transitions. Uses the full-UI fixture (real XML tree) plus the
// AmsEditOverlayViewTestAccess friend shim.

#include "ui_ams_edit_overlay.h"
#include "ui_nav_manager.h"
#include "ui_update_queue.h"

#include "../lvgl_ui_test_fixture.h"

#include "../catch_amalgamated.hpp"

using namespace helix;
using namespace helix::ui;

class AmsEditOverlayViewTestAccess {
  public:
    explicit AmsEditOverlayViewTestAccess(AmsEditOverlay& overlay) : overlay_(overlay) {}

    lv_obj_t* widget(const char* name) {
        return overlay_.find_widget(name);
    }
    void set_working_info(const SlotInfo& info) {
        overlay_.working_info_ = info;
    }
    void call_update_ui() {
        overlay_.update_ui();
    }
    int view() {
        return lv_subject_get_int(&overlay_.view_mode_subject_);
    }
    int is_managed() {
        return lv_subject_get_int(&overlay_.is_managed_subject_);
    }
    void set_cached_spools(std::vector<SpoolInfo> spools) {
        overlay_.cached_spools_ = std::move(spools);
    }
    void call_render_spool_list(const std::string& filter) {
        overlay_.render_spool_list(filter);
    }

  private:
    AmsEditOverlay& overlay_;
};

namespace {

void close_editor_overlay() {
    NavigationManager::instance().go_back();
    UpdateQueue::instance().drain();
}

SlotInfo untracked_slot() {
    SlotInfo info;
    info.slot_index = 0;
    info.spoolman_id = 0;
    info.brand = "Generic";
    info.material = "PETG";
    info.color_rgb = 0xFF6600;
    info.color_name = "Orange";
    return info;
}

SlotInfo tracked_slot() {
    SlotInfo info;
    info.slot_index = 0;
    info.spoolman_id = 7;
    info.brand = "Bambu Lab";
    info.material = "ASA";
    info.spool_name = "Bambu Lab ASA";
    info.color_rgb = 0x8A949E;
    info.color_name = "Gray ASA";
    return info;
}

} // namespace

TEST_CASE_METHOD(LVGLUITestFixture, "identity chip shows Brand · Material for untracked slots",
                 "[ams_edit_overlay][chip]") {
    auto& overlay = get_ams_edit_overlay();
    AmsEditOverlayViewTestAccess access(overlay);

    REQUIRE(overlay.show_for_slot(test_screen(), 0, untracked_slot(), nullptr, nullptr));
    UpdateQueue::instance().drain();
    process_lvgl(10);

    lv_obj_t* chip_label = access.widget("chip_label");
    REQUIRE(chip_label != nullptr);
    // Locked naming (spec §3.8): "Generic · PETG" — brand · material.
    CHECK(std::string(lv_label_get_text(chip_label)) == "Generic \xC2\xB7 PETG");
    CHECK(access.is_managed() == 0);

    lv_obj_t* mark = access.widget("chip_spoolman_mark");
    REQUIRE(mark != nullptr);
    CHECK(lv_obj_has_flag(mark, LV_OBJ_FLAG_HIDDEN));

    lv_obj_t* details_row = access.widget("spool_details_row");
    REQUIRE(details_row != nullptr);
    CHECK(lv_obj_has_flag(details_row, LV_OBJ_FLAG_HIDDEN));

    close_editor_overlay();
}

TEST_CASE_METHOD(LVGLUITestFixture, "identity chip shows spool name + mark for tracked slots",
                 "[ams_edit_overlay][chip]") {
    auto& overlay = get_ams_edit_overlay();
    AmsEditOverlayViewTestAccess access(overlay);

    // api=nullptr keeps the async Spoolman re-fetch out of the picture.
    REQUIRE(overlay.show_for_slot(test_screen(), 0, tracked_slot(), nullptr, nullptr));
    UpdateQueue::instance().drain();
    process_lvgl(10);

    lv_obj_t* chip_label = access.widget("chip_label");
    REQUIRE(chip_label != nullptr);
    CHECK(std::string(lv_label_get_text(chip_label)) == "Bambu Lab ASA");
    CHECK(access.is_managed() == 1);

    lv_obj_t* mark = access.widget("chip_spoolman_mark");
    REQUIRE(mark != nullptr);
    CHECK_FALSE(lv_obj_has_flag(mark, LV_OBJ_FLAG_HIDDEN));

    lv_obj_t* details_row = access.widget("spool_details_row");
    REQUIRE(details_row != nullptr);
    CHECK_FALSE(lv_obj_has_flag(details_row, LV_OBJ_FLAG_HIDDEN));

    // No "(Spoolman #N)" label anywhere anymore.
    CHECK(access.widget("spoolman_id_label") == nullptr);
    // Dropdowns are gone.
    CHECK(access.widget("vendor_dropdown") == nullptr);
    CHECK(access.widget("material_dropdown") == nullptr);
    CHECK(access.widget("btn_change_spool") == nullptr);

    close_editor_overlay();
}

namespace {
std::vector<SpoolInfo> two_spools() {
    SpoolInfo a;
    a.id = 11;
    a.vendor = "Polymaker";
    a.material = "PLA";
    a.color_hex = "1A1A2E";
    SpoolInfo b;
    b.id = 22;
    b.vendor = "eSUN";
    b.material = "PETG";
    b.color_hex = "00FF00";
    return {a, b};
}
} // namespace

TEST_CASE_METHOD(LVGLUITestFixture, "picker pre-selects the first row for unlinked slots",
                 "[ams_edit_overlay][picker][preselect]") {
    auto& overlay = get_ams_edit_overlay();
    AmsEditOverlayViewTestAccess access(overlay);

    REQUIRE(overlay.show_for_slot(test_screen(), 0, untracked_slot(), nullptr, nullptr));
    UpdateQueue::instance().drain();
    process_lvgl(10);

    access.set_cached_spools(two_spools());
    access.call_render_spool_list("");
    UpdateQueue::instance().drain();
    process_lvgl(10);

    lv_obj_t* list = access.widget("picker_spool_list");
    REQUIRE(list != nullptr);
    REQUIRE(lv_obj_get_child_count(list) == 2);
    CHECK(lv_obj_has_state(lv_obj_get_child(list, 0), LV_STATE_CHECKED));
    CHECK_FALSE(lv_obj_has_state(lv_obj_get_child(list, 1), LV_STATE_CHECKED));

    close_editor_overlay();
}

TEST_CASE_METHOD(LVGLUITestFixture, "picker always offers the setup entry",
                 "[ams_edit_overlay][picker][setup_entry]") {
    auto& overlay = get_ams_edit_overlay();
    AmsEditOverlayViewTestAccess access(overlay);

    REQUIRE(overlay.show_for_slot(test_screen(), 0, untracked_slot(), nullptr, nullptr,
                                  /*open_on_picker=*/true));
    UpdateQueue::instance().drain();
    process_lvgl(10);

    lv_obj_t* entry = access.widget("picker_setup_entry");
    REQUIRE(entry != nullptr);
    // Present regardless of picker fetch state (loading/empty/content) —
    // it is the only path forward when the spool list is empty.
    CHECK_FALSE(lv_obj_has_flag(entry, LV_OBJ_FLAG_HIDDEN));
    CHECK(lv_obj_has_flag(entry, LV_OBJ_FLAG_CLICKABLE));

    close_editor_overlay();
}

TEST_CASE_METHOD(LVGLUITestFixture, "picker pre-selects the current spool when linked",
                 "[ams_edit_overlay][picker][preselect]") {
    auto& overlay = get_ams_edit_overlay();
    AmsEditOverlayViewTestAccess access(overlay);

    SlotInfo linked = tracked_slot();
    linked.spoolman_id = 22;
    REQUIRE(overlay.show_for_slot(test_screen(), 0, linked, nullptr, nullptr));
    UpdateQueue::instance().drain();
    process_lvgl(10);

    access.set_cached_spools(two_spools());
    access.call_render_spool_list("");
    UpdateQueue::instance().drain();
    process_lvgl(10);

    lv_obj_t* list = access.widget("picker_spool_list");
    REQUIRE(list != nullptr);
    REQUIRE(lv_obj_get_child_count(list) == 2);
    CHECK_FALSE(lv_obj_has_state(lv_obj_get_child(list, 0), LV_STATE_CHECKED));
    CHECK(lv_obj_has_state(lv_obj_get_child(list, 1), LV_STATE_CHECKED));

    close_editor_overlay();
}
