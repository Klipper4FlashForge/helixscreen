// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui_update_queue.h"

#include "helix_test_fixture.h"
#include "lvgl/lvgl.h"

#include <mutex>

/**
 * @brief Test display dimensions (standard 800x480 touchscreen)
 */
constexpr int TEST_DISPLAY_WIDTH = 800;
constexpr int TEST_DISPLAY_HEIGHT = 480;

/**
 * @brief Drive a display to a size for the duration of a scope, then put it back
 *
 * A resize is the only production path that reaches
 * theme_manager_refresh_layout_constants(), and the rest of the suite assumes
 * the fixture's TEST_DISPLAY_WIDTH x TEST_DISPLAY_HEIGHT. Restoring the
 * resolution does NOT restore the XML constants the refresh rewrote — call the
 * refresh once more after the scope ends if the test moved them.
 */
class ScopedResolution {
  public:
    ScopedResolution(lv_display_t* disp, int32_t w, int32_t h)
        : disp_(disp), w0_(lv_display_get_horizontal_resolution(disp)),
          h0_(lv_display_get_vertical_resolution(disp)) {
        lv_display_set_resolution(disp_, w, h);
    }
    ~ScopedResolution() {
        lv_display_set_resolution(disp_, w0_, h0_);
    }

    ScopedResolution(const ScopedResolution&) = delete;
    ScopedResolution& operator=(const ScopedResolution&) = delete;

  private:
    lv_display_t* disp_;
    int32_t w0_;
    int32_t h0_;
};

/**
 * @brief Shared LVGL test fixture base class for Catch2 tests
 *
 * Provides thread-safe singleton LVGL initialization with automatic cleanup.
 * Use as a base class with TEST_CASE_METHOD for tests requiring LVGL:
 *
 * @code
 * TEST_CASE_METHOD(LVGLTestFixture, "Test name", "[tags]") {
 *     lv_obj_t* obj = lv_obj_create(test_screen());
 *     process_lvgl(100);  // Process for 100ms
 *     REQUIRE(obj != nullptr);
 * }
 * @endcode
 *
 * Key features:
 * - Thread-safe LVGL initialization (only once per test run)
 * - Virtual display buffer for headless testing
 * - Helper methods for LVGL timer processing
 * - Test screen creation with automatic cleanup
 */
class LVGLTestFixture : public HelixTestFixture {
    static bool s_queue_initialized;

  public:
    /**
     * @brief Construct fixture and ensure LVGL is initialized
     *
     * Creates a fresh test screen for each test case.
     * Also initializes the UpdateQueue (L053/L054 pattern).
     */
    LVGLTestFixture();

    /**
     * @brief Destroy fixture and clean up test objects
     *
     * Drains pending callbacks and shuts down UpdateQueue (L053/L054 pattern).
     */
    virtual ~LVGLTestFixture();

    // Non-copyable, non-movable
    LVGLTestFixture(const LVGLTestFixture&) = delete;
    LVGLTestFixture& operator=(const LVGLTestFixture&) = delete;
    LVGLTestFixture(LVGLTestFixture&&) = delete;
    LVGLTestFixture& operator=(LVGLTestFixture&&) = delete;

    /**
     * @brief Process LVGL timers for specified duration
     * @param ms Duration in milliseconds to process
     *
     * Runs lv_timer_handler() repeatedly, allowing animations,
     * transitions, and async operations to complete.
     */
    void process_lvgl(int ms);

    /**
     * @brief Get the test screen for this fixture
     * @return Test screen object (created fresh for each test)
     *
     * Use this as the parent for widgets created during tests.
     */
    lv_obj_t* test_screen() const {
        return m_test_screen;
    }

    /**
     * @brief Create a new test screen and set it as active
     * @return Newly created screen object
     *
     * Call this if you need a fresh screen within a test.
     * The previous screen is automatically cleaned up.
     */
    lv_obj_t* create_test_screen();

  protected:
    /**
     * @brief Ensure LVGL is initialized (thread-safe, called once)
     *
     * Creates virtual display buffer for headless testing.
     * Safe to call multiple times - initialization happens only once.
     */
    static void ensure_lvgl_initialized();

  protected:
    lv_obj_t* m_test_screen; ///< Test screen for this fixture instance

    // Static initialization state
    static std::once_flag s_init_flag;
    static bool s_initialized;
    static lv_display_t* s_display;
};
