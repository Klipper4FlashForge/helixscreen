// Copyright (C) 2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_plugin_api_subjects.cpp
 * @brief A plugin's subjects leave the XML scope when the plugin does.
 *
 * Run with: ./build/bin/helix-tests "[plugin][subjects]"
 *
 * register_subject() puts the plugin's lv_subject_t into the global XML scope
 * so bind_text/bind_flag can reach it. Unregistering only forgot the name on
 * the plugin side and left the scope entry pointing at the unloaded module's
 * memory, so a reloaded plugin (or any binding created after unload) walked a
 * stale pointer (prestonbrown/helixscreen#1373).
 */

#include "../lvgl_test_fixture.h"
#include "app_globals.h"
#include "helix-xml/src/xml/lv_xml.h"
#include "lvgl/lvgl.h"
#include "plugin_api.h"

#include <memory>

#include "../catch_amalgamated.hpp"

TEST_CASE_METHOD(LVGLTestFixture, "unregister_subject removes the XML scope entry too",
                 "[plugin][subjects][1373]") {
    lv_subject_t subject;
    lv_subject_init_int(&subject, 7);

    helix::plugin::PluginAPI api(nullptr, nullptr, get_printer_state(), nullptr, "probe_plugin");
    api.register_subject("probe_plugin_counter", &subject);
    REQUIRE(lv_xml_get_subject(nullptr, "probe_plugin_counter") == &subject);

    CHECK(api.unregister_subject("probe_plugin_counter"));
    CHECK(lv_xml_get_subject(nullptr, "probe_plugin_counter") == nullptr);
    CHECK_FALSE(api.unregister_subject("probe_plugin_counter"));

    lv_subject_deinit(&subject);
}

TEST_CASE_METHOD(LVGLTestFixture, "cleanup drops every subject the plugin registered",
                 "[plugin][subjects][1373]") {
    lv_subject_t a;
    lv_subject_t b;
    lv_subject_init_int(&a, 1);
    lv_subject_init_int(&b, 2);

    {
        helix::plugin::PluginAPI api(nullptr, nullptr, get_printer_state(), nullptr,
                                     "probe_plugin");
        api.register_subject("probe_plugin_a", &a);
        api.register_subject("probe_plugin_b", &b);
        REQUIRE(lv_xml_get_subject(nullptr, "probe_plugin_a") == &a);
        // The destructor runs cleanup(), the unload path.
    }

    CHECK(lv_xml_get_subject(nullptr, "probe_plugin_a") == nullptr);
    CHECK(lv_xml_get_subject(nullptr, "probe_plugin_b") == nullptr);

    lv_subject_deinit(&a);
    lv_subject_deinit(&b);
}
