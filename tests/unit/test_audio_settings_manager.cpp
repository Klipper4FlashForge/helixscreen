// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../lvgl_test_fixture.h"
#include "audio_settings_manager.h"
#include "config.h"

#include "../catch_amalgamated.hpp"

using namespace helix;

// ============================================================================
// AudioSettingsManager Tests
// ============================================================================

TEST_CASE_METHOD(LVGLTestFixture, "AudioSettingsManager default values after init",
                 "[audio_settings]") {
    // Ensure config exists with defaults
    Config::get_instance();
    AudioSettingsManager::instance().init_subjects();

    SECTION("sounds_enabled defaults to false") {
        REQUIRE(AudioSettingsManager::instance().get_sounds_enabled() == false);
    }

    SECTION("ui_sounds_enabled defaults to true") {
        REQUIRE(AudioSettingsManager::instance().get_ui_sounds_enabled() == true);
    }

    SECTION("volume defaults to 80") {
        REQUIRE(AudioSettingsManager::instance().get_volume() == 80);
    }

    SECTION("completion_alert defaults to ALERT (2)") {
        REQUIRE(AudioSettingsManager::instance().get_completion_alert_mode() ==
                CompletionAlertMode::ALERT);
    }

    SECTION("sound_theme defaults to 'default'") {
        REQUIRE(AudioSettingsManager::instance().get_sound_theme() == "default");
    }

    AudioSettingsManager::instance().deinit_subjects();
}

TEST_CASE_METHOD(LVGLTestFixture, "AudioSettingsManager set/get round trips", "[audio_settings]") {
    Config::get_instance();
    AudioSettingsManager::instance().init_subjects();

    SECTION("sounds_enabled set/get") {
        AudioSettingsManager::instance().set_sounds_enabled(true);
        REQUIRE(AudioSettingsManager::instance().get_sounds_enabled() == true);

        AudioSettingsManager::instance().set_sounds_enabled(false);
        REQUIRE(AudioSettingsManager::instance().get_sounds_enabled() == false);
    }

    SECTION("ui_sounds_enabled set/get") {
        AudioSettingsManager::instance().set_ui_sounds_enabled(false);
        REQUIRE(AudioSettingsManager::instance().get_ui_sounds_enabled() == false);

        AudioSettingsManager::instance().set_ui_sounds_enabled(true);
        REQUIRE(AudioSettingsManager::instance().get_ui_sounds_enabled() == true);
    }

    SECTION("volume set/get") {
        AudioSettingsManager::instance().set_volume(42);
        REQUIRE(AudioSettingsManager::instance().get_volume() == 42);

        AudioSettingsManager::instance().set_volume(0);
        REQUIRE(AudioSettingsManager::instance().get_volume() == 0);

        AudioSettingsManager::instance().set_volume(100);
        REQUIRE(AudioSettingsManager::instance().get_volume() == 100);
    }

    SECTION("volume clamping") {
        AudioSettingsManager::instance().set_volume(-10);
        REQUIRE(AudioSettingsManager::instance().get_volume() == 0);

        AudioSettingsManager::instance().set_volume(200);
        REQUIRE(AudioSettingsManager::instance().get_volume() == 100);
    }

    SECTION("completion_alert set/get") {
        AudioSettingsManager::instance().set_completion_alert_mode(CompletionAlertMode::OFF);
        REQUIRE(AudioSettingsManager::instance().get_completion_alert_mode() ==
                CompletionAlertMode::OFF);

        AudioSettingsManager::instance().set_completion_alert_mode(
            CompletionAlertMode::NOTIFICATION);
        REQUIRE(AudioSettingsManager::instance().get_completion_alert_mode() ==
                CompletionAlertMode::NOTIFICATION);

        AudioSettingsManager::instance().set_completion_alert_mode(CompletionAlertMode::ALERT);
        REQUIRE(AudioSettingsManager::instance().get_completion_alert_mode() ==
                CompletionAlertMode::ALERT);
    }

    AudioSettingsManager::instance().deinit_subjects();
}

TEST_CASE_METHOD(LVGLTestFixture, "AudioSettingsManager subject values match getters",
                 "[audio_settings]") {
    Config::get_instance();
    AudioSettingsManager::instance().init_subjects();

    SECTION("sounds_enabled subject reflects setter") {
        AudioSettingsManager::instance().set_sounds_enabled(true);
        REQUIRE(lv_subject_get_int(AudioSettingsManager::instance().subject_sounds_enabled()) == 1);

        AudioSettingsManager::instance().set_sounds_enabled(false);
        REQUIRE(lv_subject_get_int(AudioSettingsManager::instance().subject_sounds_enabled()) == 0);
    }

    SECTION("volume subject reflects setter") {
        AudioSettingsManager::instance().set_volume(55);
        REQUIRE(lv_subject_get_int(AudioSettingsManager::instance().subject_volume()) == 55);
    }

    SECTION("completion_alert subject reflects setter") {
        AudioSettingsManager::instance().set_completion_alert_mode(
            CompletionAlertMode::NOTIFICATION);
        REQUIRE(lv_subject_get_int(AudioSettingsManager::instance().subject_completion_alert()) ==
                1);
    }

    AudioSettingsManager::instance().deinit_subjects();
}

// Backward compat test removed: forwarding wrappers in SettingsManager have been eliminated.
// All consumers now use AudioSettingsManager directly.

// Volume's per-tick handler must not write settings.json. See the equivalent
// brightness case in test_display_settings_manager.cpp for the full rationale;
// the slider fires this on every drag tick and persists once on release.
TEST_CASE_METHOD(LVGLTestFixture, "AudioSettingsManager preview_volume does not persist",
                 "[audio_settings]") {
    Config* config = Config::get_instance();
    config->set<int>("/sounds/volume", 40);
    AudioSettingsManager::instance().deinit_subjects();
    AudioSettingsManager::instance().init_subjects();

    SECTION("preview updates the subject, not the config") {
        const int applied = AudioSettingsManager::instance().preview_volume(85);

        CHECK(applied == 85);
        CHECK(AudioSettingsManager::instance().get_volume() == 85);
        CHECK(config->get<int>("/sounds/volume", -1) == 40);
    }

    SECTION("commit persists the final value") {
        for (int v = 41; v <= 50; ++v) {
            AudioSettingsManager::instance().preview_volume(v);
        }
        CHECK(config->get<int>("/sounds/volume", -1) == 40);

        AudioSettingsManager::instance().set_volume(50);
        CHECK(config->get<int>("/sounds/volume", -1) == 50);
    }

    SECTION("preview clamps like set_volume") {
        CHECK(AudioSettingsManager::instance().preview_volume(-20) == 0);
        CHECK(AudioSettingsManager::instance().preview_volume(300) == 100);
        CHECK(config->get<int>("/sounds/volume", -1) == 40);
    }
}
