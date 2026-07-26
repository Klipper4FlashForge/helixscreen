// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_callback_helpers.cpp
 * @brief Unit tests for ui_callback_helpers.h batch registration and widget lookup helpers
 */

#include "ui_callback_helpers.h"

#include "../lvgl_test_fixture.h"

#include <spdlog/spdlog.h>

#include "../catch_amalgamated.hpp"

// ============================================================================
// Test Callbacks (static functions matching lv_event_cb_t signature)
// ============================================================================

static int g_callback_a_count = 0;
static int g_callback_b_count = 0;

static void test_callback_a(lv_event_t* /*e*/) {
    g_callback_a_count++;
}

static void test_callback_b(lv_event_t* /*e*/) {
    g_callback_b_count++;
}

// ============================================================================
// register_xml_callbacks Tests
// ============================================================================

TEST_CASE_METHOD(LVGLTestFixture, "register_xml_callbacks registers without crash",
                 "[callback_helpers]") {
    // Registering callbacks should not crash
    REQUIRE_NOTHROW(register_xml_callbacks({
        {"test_cb_a", test_callback_a},
        {"test_cb_b", test_callback_b},
    }));

    // Verify callbacks are retrievable via LVGL XML API
    lv_event_cb_t retrieved_a = lv_xml_get_event_cb(nullptr, "test_cb_a");
    lv_event_cb_t retrieved_b = lv_xml_get_event_cb(nullptr, "test_cb_b");
    REQUIRE(retrieved_a == test_callback_a);
    REQUIRE(retrieved_b == test_callback_b);
}

TEST_CASE_METHOD(LVGLTestFixture, "register_xml_callbacks handles empty list",
                 "[callback_helpers]") {
    REQUIRE_NOTHROW(register_xml_callbacks({}));
}

TEST_CASE_METHOD(LVGLTestFixture, "register_xml_callbacks handles single entry",
                 "[callback_helpers]") {
    REQUIRE_NOTHROW(register_xml_callbacks({
        {"test_single_cb", test_callback_a},
    }));

    lv_event_cb_t retrieved = lv_xml_get_event_cb(nullptr, "test_single_cb");
    REQUIRE(retrieved == test_callback_a);
}

// Regression: bundle SSHGTVZQ (Qidi Q2 / v0.99.46 / pi). WizardWifiStep
// registered `on_network_item_clicked` globally during add-printer; when
// NetworkSettingsOverlay later tried to register the same name for clicks on
// its own wifi_network_item instances, lv_xml_register_event_cb's
// first-write-wins semantics silently dropped the second registration. Items
// created by NetworkSettingsOverlay::populate_network_list still bound to the
// wizard's static handler, which cast NetworkSettingsItemData{ssid, is_secured}
// as the larger WifiWizardNetworkItemData and SEGV'd dereferencing
// item_data->parent.
//
// This test reproduces the dispatch path: register Owner A under a shared
// name, register Owner B under the same name, then go through the same
// codepath the XML parser uses to bind a callback to a widget at instance
// creation (lv_xml_get_event_cb -> lv_obj_add_event_cb) and fire a synthetic
// click. Owner B's handler must run; Owner A's must not. Under the original
// first-write-wins behavior this test would dispatch to Owner A and fail.
TEST_CASE_METHOD(LVGLTestFixture,
                 "shared callback name: later registration wins at widget bind time",
                 "[callback_helpers][regression][bundle_SSHGTVZQ]") {
    g_callback_a_count = 0;
    g_callback_b_count = 0;

    // Owner A (e.g. WizardWifiStep) registers first.
    lv_xml_register_event_cb(nullptr, "shared_click_cb", test_callback_a);
    REQUIRE(lv_xml_get_event_cb(nullptr, "shared_click_cb") == test_callback_a);

    // Owner B (e.g. NetworkSettingsOverlay) registers the same name later.
    // Under first-write-wins this would be silently dropped — the bug.
    lv_xml_register_event_cb(nullptr, "shared_click_cb", test_callback_b);

    // Owner B then creates a widget. lv_obj_xml_event_cb_apply in the XML
    // parser does exactly this lookup-and-bind pair at instance creation, so
    // we mimic it directly.
    lv_obj_t* item = lv_obj_create(test_screen());
    lv_event_cb_t bound = lv_xml_get_event_cb(nullptr, "shared_click_cb");
    REQUIRE(bound == test_callback_b);
    lv_obj_add_event_cb(item, bound, LV_EVENT_CLICKED, nullptr);

    lv_obj_send_event(item, LV_EVENT_CLICKED, nullptr);

    REQUIRE(g_callback_b_count == 1);
    REQUIRE(g_callback_a_count == 0);
}
