// SPDX-License-Identifier: GPL-3.0-or-later
#include "page_scroll_auto_inject.h"

#include "display_settings_manager.h"
#include "observer_factory.h"

#include <spdlog/spdlog.h>

namespace helix::ui {

PageScrollAutoInject& PageScrollAutoInject::instance() {
    static PageScrollAutoInject inst;
    return inst;
}

void PageScrollAutoInject::init() {
    if (initialized_) {
        return;
    }
    initialized_ = true;
    auto& dsm = helix::DisplaySettingsManager::instance();
    enabled_ = dsm.get_page_scroll_buttons();
    // observe_int_sync defers to the main thread (safe for widget mutation).
    setting_observer_ = observe_int_sync<PageScrollAutoInject>(
        dsm.subject_page_scroll_buttons(), this, [](PageScrollAutoInject* self, int value) {
            self->enabled_ = (value != 0);
            if (self->enabled_) {
                self->on_root_shown(lv_screen_active()); // inject into current screen
            } else {
                self->detach_all();
            }
        });
}

void PageScrollAutoInject::shutdown() {
    setting_observer_.reset();
    detach_all();
    initialized_ = false;
}

bool PageScrollAutoInject::qualifies(lv_obj_t* obj) {
    if (!lv_obj_has_flag(obj, LV_OBJ_FLAG_SCROLLABLE)) {
        return false;
    }
    if ((lv_obj_get_scroll_dir(obj) & LV_DIR_VER) == 0) {
        return false; // horizontal-only (e.g. carousel)
    }
    return lv_obj_get_scroll_bottom(obj) > 0 || lv_obj_get_scroll_y(obj) > 0;
}

void PageScrollAutoInject::walk_and_attach(lv_obj_t* obj, bool ancestor_managed) {
    if (lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN)) {
        return; // don't inject into hidden/stacked panels
    }
    bool claimed = ancestor_managed;
    if (!ancestor_managed && controllers_.find(obj) == controllers_.end() && qualifies(obj)) {
        auto ctl = std::make_unique<PageScrollController>();
        if (ctl->attach(obj)) {
            lv_obj_t* key = obj;
            // Synchronous erase is safe ONLY because PageScrollController's
            // on_container_deleted() nulls container_ BEFORE invoking this callback
            // (so the destroyed controller's dtor is a no-op) and invokes it as its
            // provably-last statement with no `self` access afterward (Task 3
            // hardening). This is a `delete this`-adjacent pattern — do not add code
            // that touches the controller after erase, and keep that invariant if you
            // edit the controller.
            ctl->set_on_container_deleted([this, key]() {
                controllers_.erase(key); // container gone; drop controller
            });
            controllers_.emplace(key, std::move(ctl));
            claimed = true;
        }
    }
    uint32_t n = lv_obj_get_child_count(obj);
    for (uint32_t i = 0; i < n; ++i) {
        walk_and_attach(lv_obj_get_child(obj, static_cast<int32_t>(i)), claimed);
    }
}

void PageScrollAutoInject::on_root_shown(lv_obj_t* root) {
    // Read the setting live rather than trusting the cached enabled_ flag:
    // observe_int_sync defers its callback via queue_update, so enabled_ can
    // lag the subject's synchronous value by one process_lvgl tick (e.g. a
    // caller that flips the setting and immediately shows a panel in the same
    // frame). This class is main-thread-only, so a direct subject read here
    // is safe and keeps on_root_shown() correct without waiting for the
    // deferred observer to catch up.
    enabled_ = helix::DisplaySettingsManager::instance().get_page_scroll_buttons();
    if (!enabled_ || root == nullptr) {
        return;
    }
    prune_dead();
    lv_obj_update_layout(root); // overflow must be measurable
    walk_and_attach(root, /*ancestor_managed=*/false);
}

void PageScrollAutoInject::detach_all() {
    for (auto& [key, ctl] : controllers_) {
        ctl->set_on_container_deleted(nullptr); // avoid erase-during-iteration
        ctl->detach();
    }
    controllers_.clear();
}

void PageScrollAutoInject::prune_dead() {
    for (auto it = controllers_.begin(); it != controllers_.end();) {
        if (!it->second->alive()) {
            it = controllers_.erase(it);
        } else {
            ++it;
        }
    }
}

} // namespace helix::ui
