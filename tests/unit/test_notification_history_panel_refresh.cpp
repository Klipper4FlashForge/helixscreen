// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_notification_history_panel_refresh.cpp
 * @brief The open notification panel must show notifications that arrive while
 *        it is visible (prestonbrown/helixscreen#1525).
 *
 * The list was a one-shot snapshot built when the overlay opened. The store
 * fired nothing on add(), so a notification arriving while the panel was open
 * incremented the bell badge while the list underneath stayed stale until a
 * close-and-reopen destroyed and recreated the whole XML component.
 *
 * The seam this pins: NotificationHistory keeps a revision counter bumped on
 * add()/clear(), NotificationManager publishes it into the
 * notification_history_version subject whenever the badge refreshes, and the
 * panel observes that subject and rebuilds its list. Killing either half of
 * that chain fails the arrival test below.
 */

#include "ui_notification_history.h"
#include "ui_notification_manager.h"
#include "ui_panel_notification_history.h"
#include "ui_update_queue.h"

#include "../lvgl_ui_test_fixture.h"
#include "helix-xml/src/xml/lv_xml.h"
#include "printer_state.h"

#include <cstring>
#include <memory>

#include "../catch_amalgamated.hpp"

using helix::ui::UpdateQueue;

namespace {

NotificationHistoryEntry make_entry(const char* title, const char* message) {
    NotificationHistoryEntry entry = {};
    entry.timestamp_ms = lv_tick_get();
    entry.severity = ToastSeverity::INFO;
    entry.was_modal = false;
    entry.was_read = false;
    strncpy(entry.title, title, sizeof(entry.title) - 1);
    strncpy(entry.message, message, sizeof(entry.message) - 1);
    return entry;
}

/// Owns the NotificationHistoryPanel built from the production XML component.
struct NotificationHistoryPanelFixture : public LVGLUITestFixture {
    NotificationHistoryPanelFixture() {
        helix::ui::notification_init_subjects();

        auto& history = NotificationHistory::instance();
        history.clear();

        panel_ = std::make_unique<NotificationHistoryPanel>(state(), nullptr);
        panel_->init_subjects();

        // The panel resolves its severity_card / header_bar dependency chain
        // through the production tree this fixture registers.
        root_ = static_cast<lv_obj_t*>(
            lv_xml_create(lv_screen_active(), "notification_history_panel", nullptr));
        if (root_) {
            panel_->setup(root_, lv_screen_active());
        }
    }

    ~NotificationHistoryPanelFixture() override {
        if (root_ && lv_obj_is_valid(root_)) {
            lv_obj_delete(root_);
        }
        root_ = nullptr;
        UpdateQueue::instance().drain();
        panel_.reset();
        UpdateQueue::instance().drain();
    }

    /// The live item container, or nullptr if the tree did not build.
    lv_obj_t* content() const {
        return root_ ? lv_obj_find_by_name(root_, "overlay_content") : nullptr;
    }

    std::unique_ptr<NotificationHistoryPanel> panel_;
    lv_obj_t* root_ = nullptr;
};

} // namespace

TEST_CASE_METHOD(NotificationHistoryPanelFixture,
                 "NotificationHistoryPanel: arriving notification appears while the panel is open",
                 "[ui][notifications][1525]") {
    REQUIRE(root_ != nullptr);
    lv_obj_t* overlay = content();
    REQUIRE(overlay != nullptr);

    NotificationHistory::instance().add(make_entry("Entry One", "before the panel opened"));
    panel_->refresh();
    REQUIRE(lv_obj_get_child_count(overlay) == 1);

    // What production entry points do after writing the store.
    NotificationHistory::instance().add(make_entry("Entry Two", "arrived while open"));
    helix::ui::notification_refresh_from_history();
    UpdateQueue::instance().drain();

    REQUIRE(lv_obj_get_child_count(overlay) == 2);

    // The list is newest-first, so the freshly arrived entry is the first item.
    lv_obj_t* title = lv_obj_find_by_name(overlay, "item_title");
    REQUIRE(title != nullptr);
    REQUIRE(std::string(lv_label_get_text(title)) == "Entry Two");
}

TEST_CASE_METHOD(NotificationHistoryPanelFixture,
                 "NotificationHistoryPanel: badge refresh without a new entry does not rebuild",
                 "[ui][notifications][1525]") {
    REQUIRE(root_ != nullptr);
    lv_obj_t* overlay = content();
    REQUIRE(overlay != nullptr);

    NotificationHistory::instance().add(make_entry("Entry One", "only entry"));
    panel_->refresh();
    REQUIRE(lv_obj_get_child_count(overlay) == 1);

    helix::ui::notification_refresh_from_history();
    UpdateQueue::instance().drain();

    REQUIRE(lv_obj_get_child_count(overlay) == 1);
}

TEST_CASE_METHOD(NotificationHistoryPanelFixture,
                 "NotificationHistoryPanel: destroying the panel withdraws its version observer",
                 "[ui][notifications][1525]") {
    REQUIRE(root_ != nullptr);

    lv_subject_t* subject = helix::ui::notification_history_version_subject();
    REQUIRE(subject != nullptr);

    // setup() attached exactly one observer on the manager's version subject.
    REQUIRE(lv_ll_get_len(&subject->subs_ll) == 1);

    // The observer defers through UpdateQueue; its removal must be immediate
    // so a notification arriving after teardown queues nothing into freed
    // state. Destroy in the same order as production: widgets first, then the
    // panel (whose dtor runs deinit_subjects()).
    lv_obj_delete(root_);
    root_ = nullptr;
    UpdateQueue::instance().drain();
    panel_.reset();
    UpdateQueue::instance().drain();

    REQUIRE(lv_ll_get_len(&subject->subs_ll) == 0);
}