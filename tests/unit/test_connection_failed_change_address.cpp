// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_connection_failed_change_address.cpp
 * @brief The "can't reach the printer" modal must offer a way to fix it.
 *
 * Context (debug bundle XRK8KPTF, K2 Plus): the configured host was stale, so
 * the WebSocket never opened. With the initial-connect escalation in place the
 * user now gets a modal naming the address — but an OK-only modal on a wrong
 * address is still a dead end, and the address itself lives four levels deep
 * under Settings > System > Printer Host with nothing pointing there.
 *
 * So the connection-failed prompt carries a "Change Address" action that opens
 * the existing ChangeHostModal directly. This test drives the real prompt, then
 * clicks the real button, and asserts the host modal actually comes up — the
 * button existing is not the same as the button working.
 */

#include "ui_change_host_modal.h"
#include "ui_modal.h"
#include "ui_update_queue.h"

#include "../test_fixtures.h"

#include <lvgl.h>
#include <string>

#include "../catch_amalgamated.hpp"

using helix::ui::UpdateQueue;

namespace {

class ConnFailedFixture : public XMLTestFixture {
  public:
    ConnFailedFixture() {
        // modal_configure() silently no-ops without these, leaving the button
        // captions at their defaults — the app does this at startup.
        helix::ui::modal_init_subjects();
        REQUIRE(register_component("modal_dialog"));
        REQUIRE(register_component("change_host_modal"));
    }
    ~ConnFailedFixture() override {
        while (lv_obj_t* top = Modal::get_top()) {
            Modal::hide(top);
            UpdateQueue::instance().drain();
        }
        UpdateQueue::instance().drain();
    }
};

} // namespace

TEST_CASE_METHOD(ConnFailedFixture, "Connection-failed prompt offers Change Address",
                 "[modal][connection][change_host]") {
    helix::ui::show_connection_failed_modal("Connection Failed",
                                            "Unable to reach printer at 192.168.1.171:7125.");
    // The real caller is the libhv thread, so the prompt marshals itself to the
    // main thread. Nothing exists until the queue drains — asserting before this
    // would pass against a version that never marshalled at all.
    UpdateQueue::instance().drain();

    lv_obj_t* dialog = Modal::get_top();
    REQUIRE(dialog != nullptr);

    // The address the user has to correct must be in front of them.
    lv_obj_t* msg = lv_obj_find_by_name(dialog, "dialog_message");
    if (msg) {
        CHECK(std::string(lv_label_get_text(msg)).find("192.168.1.171:7125") != std::string::npos);
    }

    lv_obj_t* primary = lv_obj_find_by_name(dialog, "btn_primary");
    REQUIRE(primary != nullptr);

    // btn_primary is a C++-registered ui_button whose caption comes from the
    // shared dialog subject, not a child label — so assert the subject, which
    // is the actual contract modal_configure() writes.
    const char* primary_text = static_cast<const char*>(
        lv_subject_get_pointer(helix::ui::modal_get_primary_text_subject()));
    REQUIRE(primary_text != nullptr);
    CHECK(std::string(primary_text).find("Change Address") != std::string::npos);

    // Press it. This is the half that a "button exists" assertion would miss.
    lv_obj_send_event(primary, LV_EVENT_CLICKED, nullptr);
    UpdateQueue::instance().drain();

    lv_obj_t* now_top = Modal::get_top();
    REQUIRE(now_top != nullptr);
    CHECK(now_top != dialog);
    // The change-host modal owns a host input; finding it proves we landed on
    // the right dialog rather than merely dismissing the first one.
    CHECK(lv_obj_find_by_name(now_top, "host_input") != nullptr);
}

TEST_CASE_METHOD(ConnFailedFixture, "Connection-failed prompt can be dismissed without changes",
                 "[modal][connection][change_host]") {
    helix::ui::show_connection_failed_modal("Connection Failed",
                                            "Unable to reach printer at 10.0.0.5:7125.");
    UpdateQueue::instance().drain();

    lv_obj_t* dialog = Modal::get_top();
    REQUIRE(dialog != nullptr);

    lv_obj_t* secondary = lv_obj_find_by_name(dialog, "btn_secondary");
    REQUIRE(secondary != nullptr);

    lv_obj_send_event(secondary, LV_EVENT_CLICKED, nullptr);
    UpdateQueue::instance().drain();

    // Dismissing must not leave the host modal (or anything else) behind.
    CHECK(Modal::get_top() == nullptr);
}
