// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_heater_config.h"
#include "ui_heater_icon_binder.h"

#include <string>

#include "../catch_amalgamated.hpp"

using helix::HeaterType;
using helix::ui::HeaterIconBinder;

TEST_CASE("HeaterIconBinder: conventional glyph names per heater", "[heater_binder]") {
    REQUIRE(std::string(HeaterIconBinder::default_icon_name(HeaterType::Nozzle)) ==
            "nozzle_icon_glyph");
    REQUIRE(std::string(HeaterIconBinder::default_icon_name(HeaterType::Bed)) == "bed_icon_glyph");
    REQUIRE(std::string(HeaterIconBinder::default_icon_name(HeaterType::Chamber)) ==
            "chamber_icon_glyph");
}

TEST_CASE("HeaterIconBinder: starts unbound", "[heater_binder]") {
    HeaterIconBinder binder;
    REQUIRE_FALSE(binder.is_bound());
}

TEST_CASE("HeaterIconBinder: binding a null root is a safe no-op", "[heater_binder]") {
    HeaterIconBinder binder;
    REQUIRE_FALSE(binder.bind_subjects(nullptr, "bed_icon_glyph", nullptr, nullptr));
    REQUIRE_FALSE(binder.is_bound());
}

TEST_CASE("HeaterIconBinder: unbind on an unbound binder is safe", "[heater_binder]") {
    HeaterIconBinder binder;
    binder.unbind();
    binder.unbind();
    REQUIRE_FALSE(binder.is_bound());
}
