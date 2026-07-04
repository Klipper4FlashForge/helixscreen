// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#include "helix-xml/src/xml/lv_xml.h"
#include "lvgl/lvgl.h"
#include "setting_group.h"
#include "test_fixtures.h"

#include "../catch_amalgamated.hpp"

// Fixture that registers the setting_group widget (mirrors SplitButtonXmlFixture).
class SettingGroupFixture : public XMLTestFixture {
  public:
    SettingGroupFixture() : XMLTestFixture() {
        setting_group_register();
    }
};

TEST_CASE_METHOD(SettingGroupFixture, "setting_group: applies card shell", "[setting_group]") {
    auto* group = static_cast<lv_obj_t*>(lv_xml_create(test_screen(), "setting_group", nullptr));
    REQUIRE(group != nullptr);

    // Card fill is opaque (from StyleRole::Card via configure_card).
    REQUIRE(lv_obj_get_style_bg_opa(group, LV_PART_MAIN) == LV_OPA_COVER);
    // Children are clipped to the rounded rect so first/last row corners round.
    REQUIRE(lv_obj_get_style_clip_corner(group, LV_PART_MAIN) == true);
    // Group is a fixed container, not a scroll area.
    REQUIRE_FALSE(lv_obj_has_flag(group, LV_OBJ_FLAG_SCROLLABLE));
}

TEST_CASE_METHOD(SettingGroupFixture, "setting_group: divider count skips hidden children",
                 "[setting_group]") {
    auto* group = static_cast<lv_obj_t*>(lv_xml_create(test_screen(), "setting_group", nullptr));
    REQUIRE(group != nullptr);

    // Three visible children -> two dividers (one above each after the first).
    lv_obj_t* a = lv_obj_create(group);
    lv_obj_t* b = lv_obj_create(group);
    lv_obj_t* c = lv_obj_create(group);
    (void)a;
    (void)c;
    process_lvgl(50);
    REQUIRE(setting_group_divider_count(group) == 2);

    // Hide the middle child -> one divider.
    lv_obj_add_flag(b, LV_OBJ_FLAG_HIDDEN);
    process_lvgl(50);
    REQUIRE(setting_group_divider_count(group) == 1);

    // Hide all but one -> zero dividers.
    lv_obj_add_flag(c, LV_OBJ_FLAG_HIDDEN);
    process_lvgl(50);
    REQUIRE(setting_group_divider_count(group) == 0);
}
