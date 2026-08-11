// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_home_edit_mode_lock_guard.cpp
 * @brief #1245 — a long press must not open home-grid edit mode behind the lock screen.
 *
 * On Android the wake touch reaches the home panel (the sleep overlay is not
 * clickable and the sleep-aware input wrapper is compiled out under
 * HELIX_DISPLAY_SDL). wake_display() shows the lock screen, but nothing on that
 * path stopped a LONG_PRESSED already in flight from reaching
 * HomePanel::on_home_grid_long_press — so edit mode activated underneath the
 * PIN pad and the user found it once the PIN cleared.
 *
 * DisplayManager::disable_input_briefly() is the root-cause fix
 * (tests/unit/application/test_display_wake_input_gate.cpp). This is the second
 * layer: while the screen is locked, edit mode is never a legitimate outcome of
 * a hold, whatever produced the event.
 *
 * The handler only does anything once the panel owns a page container, so the
 * container list is seeded through HomePanelTestAccess rather than standing up
 * the whole carousel; everything below that — should_suppress_edit_mode(), the
 * drift check, GridEditMode::enter() — is the production path.
 */

#include "ui_panel_home.h"

#include "../test_helpers/home_panel_test_access.h"
#include "config.h"
#include "input_settings_manager.h"
#include "lock_manager.h"
#include "lvgl_test_fixture.h"

#include "../catch_amalgamated.hpp"

namespace {

/// Stands up the InputSettingsManager subjects so the long-press handler reads
/// the documented defaults (home_edit_mode_enabled = true) instead of a zeroed
/// subject. LVGLTestFixture does not initialize this manager, so without this
/// guard the test would depend on a co-tenant test having leaked an init — it
/// passed only by shard-ordering luck and failed in isolation.
class ScopedInputSettings {
  public:
    ScopedInputSettings() {
        helix::Config::get_instance();
        helix::InputSettingsManager::instance().init_subjects();
    }
    ~ScopedInputSettings() {
        helix::InputSettingsManager::instance().deinit_subjects();
    }
};

/// Restores the LockManager to "no PIN, unlocked" however the test exits — it
/// is a process-wide singleton that persists its PIN to Config, so a leaked
/// lock would follow every later test in the binary.
class ScopedLockState {
  public:
    ~ScopedLockState() {
        helix::LockManager::instance().remove_pin();
    }
};

/// Detaches the seeded container and leaves edit mode, so the global HomePanel
/// does not outlive this test holding a pointer to a fixture-owned widget.
class ScopedHomePanelPage {
  public:
    explicit ScopedHomePanelPage(HomePanel& panel, lv_obj_t* container) : panel_(panel) {
        HomePanelTestAccess::set_single_page_container(panel_, container);
    }
    ~ScopedHomePanelPage() {
        panel_.exit_grid_edit_mode();
        HomePanelTestAccess::clear_page_containers(panel_);
    }
    ScopedHomePanelPage(const ScopedHomePanelPage&) = delete;
    ScopedHomePanelPage& operator=(const ScopedHomePanelPage&) = delete;

  private:
    HomePanel& panel_;
};

} // namespace

TEST_CASE_METHOD(LVGLTestFixture, "home-grid long press is ignored while the screen is locked",
                 "[home][grid_edit][edit_mode][lock][1245]") {
    ScopedInputSettings input_settings;
    ScopedLockState lock_state;
    auto& lock = helix::LockManager::instance();
    lock.remove_pin(); // known-clean starting point

    HomePanel& panel = get_global_home_panel();

    // Stand-in for the carousel page container the real handler operates on.
    lv_obj_t* container = lv_obj_create(test_screen());
    lv_obj_remove_flag(container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(container, 400, 300);
    lv_obj_update_layout(container);

    ScopedHomePanelPage page(panel, container);

    // The XML wires this exact callback onto carousel_host_ for LONG_PRESSED.
    lv_obj_add_event_cb(container, HomePanelTestAccess::long_press_cb(), LV_EVENT_LONG_PRESSED,
                        nullptr);

    REQUIRE_FALSE(HomePanelTestAccess::edit_mode_active(panel));

    SECTION("unlocked: the hold enters edit mode (control case)") {
        REQUIRE_FALSE(lock.is_locked());

        lv_obj_send_event(container, LV_EVENT_LONG_PRESSED, nullptr);

        CHECK(HomePanelTestAccess::edit_mode_active(panel));
    }

    SECTION("locked: the identical hold is suppressed") {
        REQUIRE(lock.set_pin("1234"));
        lock.lock();
        REQUIRE(lock.is_locked());

        lv_obj_send_event(container, LV_EVENT_LONG_PRESSED, nullptr);

        CHECK_FALSE(HomePanelTestAccess::edit_mode_active(panel));

        // And once the PIN clears, the same hold works again — the guard keys
        // off lock state, not some sticky one-way latch.
        REQUIRE(lock.try_unlock("1234"));
        lv_obj_send_event(container, LV_EVENT_LONG_PRESSED, nullptr);
        CHECK(HomePanelTestAccess::edit_mode_active(panel));
    }
}
