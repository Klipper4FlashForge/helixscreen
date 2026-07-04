// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ui_observer_guard.h"

#include "lvgl/lvgl.h"
#include "page_scroll_controller.h"

#include <cstddef>
#include <memory>
#include <unordered_map>

namespace helix::ui {

/** Global policy that, when the display setting is on, walks each just-shown
 *  panel/overlay root and attaches a PageScrollController to every overflowing,
 *  vertically-scrollable, non-nested container. Main-thread only. */
class PageScrollAutoInject {
  public:
    static PageScrollAutoInject& instance();

    void init();     ///< register setting observer (idempotent)
    void shutdown(); ///< detach all + drop observer
    void on_root_shown(lv_obj_t* root);
    void detach_all();
    void prune_dead(); ///< erase controllers whose container died

    bool enabled() const {
        return enabled_;
    }
    std::size_t managed_count() const {
        return controllers_.size();
    }

  private:
    PageScrollAutoInject() = default;
    void walk_and_attach(lv_obj_t* obj, bool ancestor_managed);
    static bool qualifies(lv_obj_t* obj);

    std::unordered_map<lv_obj_t*, std::unique_ptr<PageScrollController>> controllers_;
    ObserverGuard setting_observer_;
    bool enabled_ = false;
    bool initialized_ = false;
};

} // namespace helix::ui
