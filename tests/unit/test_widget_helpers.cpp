// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_widget_helpers.cpp
 * @brief Tests for FIND_WIDGET macros
 *
 * These tests verify the widget lookup helper macros work correctly
 * using the LVGL test fixture infrastructure.
 */

#include "../lvgl_test_fixture.h"
#include "../test_helpers/log_capture.h"
#include "ui/ui_widget_helpers.h"

#include "../catch_amalgamated.hpp"

// ============================================================================
// FIND_WIDGET tests
// ============================================================================

TEST_CASE_METHOD(LVGLTestFixture, "FIND_WIDGET assigns non-null when widget exists",
                 "[widget_helpers]") {
    // Create a parent and child widget with a name
    lv_obj_t* parent = lv_obj_create(test_screen());
    lv_obj_t* child = lv_obj_create(parent);
    lv_obj_set_user_data(child, nullptr);

    // Set name for lookup (LVGL stores names for lv_obj_find_by_name)
    lv_obj_set_name(child, "test_button");

    // Use the macro
    lv_obj_t* result = nullptr;
    FIND_WIDGET(result, parent, "test_button", "TestPanel");

    REQUIRE(result != nullptr);
    REQUIRE(result == child);
}

TEST_CASE_METHOD(LVGLTestFixture, "FIND_WIDGET assigns nullptr when widget not found",
                 "[widget_helpers]") {
    lv_obj_t* parent = lv_obj_create(test_screen());

    lv_obj_t* result = reinterpret_cast<lv_obj_t*>(0xDEADBEEF); // Non-null sentinel
    {
        helix::TextLogCapture log; // Capture warnings
        FIND_WIDGET(result, parent, "nonexistent_widget", "TestPanel");
    }

    REQUIRE(result == nullptr);
}

TEST_CASE_METHOD(LVGLTestFixture, "FIND_WIDGET logs warning when widget not found",
                 "[widget_helpers][logging]") {
    lv_obj_t* parent = lv_obj_create(test_screen());
    helix::TextLogCapture log;

    lv_obj_t* result = nullptr;
    FIND_WIDGET(result, parent, "missing_widget", "MyPanel");

    // Verify warning was logged with correct format
    REQUIRE(log.contains("[MyPanel]"));
    REQUIRE(log.contains("missing_widget"));
    REQUIRE(log.contains("not found"));
}

TEST_CASE_METHOD(LVGLTestFixture, "FIND_WIDGET does not log when widget found",
                 "[widget_helpers][logging]") {
    lv_obj_t* parent = lv_obj_create(test_screen());
    lv_obj_t* child = lv_obj_create(parent);
    lv_obj_set_name(child, "existing_widget");

    helix::TextLogCapture log;

    lv_obj_t* result = nullptr;
    FIND_WIDGET(result, parent, "existing_widget", "MyPanel");

    // Verify no warning was logged
    REQUIRE_FALSE(log.contains("not found"));
}

// ============================================================================
// FIND_WIDGET_REQUIRED tests
// ============================================================================

TEST_CASE_METHOD(LVGLTestFixture, "FIND_WIDGET_REQUIRED logs error when widget not found",
                 "[widget_helpers][logging]") {
    lv_obj_t* parent = lv_obj_create(test_screen());
    helix::TextLogCapture log;

    lv_obj_t* result = nullptr;
    FIND_WIDGET_REQUIRED(result, parent, "critical_widget", "CriticalPanel");

    // Verify error was logged (not just warning) - note the "!" in error message
    REQUIRE(log.contains("[CriticalPanel]"));
    REQUIRE(log.contains("critical_widget"));
    REQUIRE(log.contains("not found!"));
}

TEST_CASE_METHOD(LVGLTestFixture, "FIND_WIDGET_REQUIRED finds existing widget",
                 "[widget_helpers]") {
    lv_obj_t* parent = lv_obj_create(test_screen());
    lv_obj_t* child = lv_obj_create(parent);
    lv_obj_set_name(child, "required_widget");

    lv_obj_t* result = nullptr;
    FIND_WIDGET_REQUIRED(result, parent, "required_widget", "TestPanel");

    REQUIRE(result != nullptr);
    REQUIRE(result == child);
}

// ============================================================================
// FIND_WIDGET_OPTIONAL tests
// ============================================================================

TEST_CASE_METHOD(LVGLTestFixture, "FIND_WIDGET_OPTIONAL does not log on failure",
                 "[widget_helpers][logging]") {
    lv_obj_t* parent = lv_obj_create(test_screen());
    helix::TextLogCapture log;

    lv_obj_t* result = nullptr;
    FIND_WIDGET_OPTIONAL(result, parent, "optional_widget");

    // No logging should occur for optional widgets
    REQUIRE_FALSE(log.contains("optional_widget"));
    REQUIRE_FALSE(log.contains("not found"));
}

TEST_CASE_METHOD(LVGLTestFixture, "FIND_WIDGET_OPTIONAL still assigns result", "[widget_helpers]") {
    lv_obj_t* parent = lv_obj_create(test_screen());
    lv_obj_t* child = lv_obj_create(parent);
    lv_obj_set_name(child, "optional_child");

    lv_obj_t* result = nullptr;
    FIND_WIDGET_OPTIONAL(result, parent, "optional_child");

    REQUIRE(result != nullptr);
    REQUIRE(result == child);
}

TEST_CASE_METHOD(LVGLTestFixture, "FIND_WIDGET_OPTIONAL returns nullptr for missing widget",
                 "[widget_helpers]") {
    lv_obj_t* parent = lv_obj_create(test_screen());

    lv_obj_t* result = reinterpret_cast<lv_obj_t*>(0xDEADBEEF);
    FIND_WIDGET_OPTIONAL(result, parent, "missing_optional");

    REQUIRE(result == nullptr);
}

// ============================================================================
// Edge cases
// ============================================================================

TEST_CASE_METHOD(LVGLTestFixture, "FIND_WIDGET handles null parent gracefully",
                 "[widget_helpers][edge]") {
    helix::TextLogCapture log;

    lv_obj_t* result = reinterpret_cast<lv_obj_t*>(0xDEADBEEF);
    FIND_WIDGET(result, nullptr, "any_widget", "NullParentTest");

    // lv_obj_find_by_name returns nullptr for null parent
    REQUIRE(result == nullptr);
}

TEST_CASE_METHOD(LVGLTestFixture, "FIND_WIDGET searches recursively in children",
                 "[widget_helpers]") {
    // Create nested hierarchy: parent -> container -> button
    lv_obj_t* parent = lv_obj_create(test_screen());
    lv_obj_t* container = lv_obj_create(parent);
    lv_obj_t* deep_child = lv_obj_create(container);
    lv_obj_set_name(deep_child, "deep_button");

    lv_obj_t* result = nullptr;
    FIND_WIDGET(result, parent, "deep_button", "TestPanel");

    // Should find the widget in nested children
    REQUIRE(result != nullptr);
    REQUIRE(result == deep_child);
}

// ============================================================================
// Macro hygiene tests
// ============================================================================

TEST_CASE_METHOD(LVGLTestFixture, "FIND_WIDGET macro is expression-safe",
                 "[widget_helpers][hygiene]") {
    lv_obj_t* parent = lv_obj_create(test_screen());
    lv_obj_t* widget = nullptr;

    // Should work in if-else without braces
    if (true)
        FIND_WIDGET(widget, parent, "test", "Test");
    else
        FIND_WIDGET(widget, parent, "other", "Test");

    // Should work in single-line context
    for (int i = 0; i < 1; i++)
        FIND_WIDGET(widget, parent, "loop_test", "Test");

    // Compiles and runs without issues
    REQUIRE(widget == nullptr); // Neither widget exists
}

TEST_CASE_METHOD(LVGLTestFixture, "FIND_WIDGET_OPTIONAL works without do-while wrapper",
                 "[widget_helpers][hygiene]") {
    lv_obj_t* parent = lv_obj_create(test_screen());

    // FIND_WIDGET_OPTIONAL is a simple assignment, should work anywhere
    lv_obj_t* widget = nullptr;
    if (true)
        FIND_WIDGET_OPTIONAL(widget, parent, "test");

    REQUIRE(widget == nullptr);
}
