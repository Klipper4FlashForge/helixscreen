// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Per-printer "hidden macros" persistence on SettingsManager. Macro-panel
// edit mode lets a user hide individual macro buttons; this set is what
// remembers the choice across restarts, keyed under the active printer's
// config section (df() + "macros/hidden") so different printers can hide
// different macros.

#include "../helix_test_fixture.h"

#include "config.h"
#include "settings_manager.h"

#include "../catch_amalgamated.hpp"

#include <algorithm>
#include <string>
#include <vector>

using namespace helix;

TEST_CASE_METHOD(HelixTestFixture, "hidden macros round-trip per printer", "[settings][macros]") {
    auto& sm = SettingsManager::instance();
    REQUIRE(sm.get_hidden_macros().empty());
    REQUIRE_FALSE(sm.hidden_macros_key_exists());

    sm.set_hidden_macros({"_HOME_Z", "_CALIBRATE"});
    REQUIRE(sm.hidden_macros_key_exists());
    auto got = sm.get_hidden_macros();
    REQUIRE(got.size() == 2);
    REQUIRE(std::find(got.begin(), got.end(), "_HOME_Z") != got.end());
}
