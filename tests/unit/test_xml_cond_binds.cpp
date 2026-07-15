// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#include "../lvgl_test_fixture.h"
#include "../catch_amalgamated.hpp"
extern "C" {
#include "helix-xml/src/xml/lv_xml.h"
}

static const char * COMP_INVERT =
  "<component>"
  "  <subjects>"
  "    <subject name='err' type='int' value='0'/>"
  "    <subject name='tmp' type='int' value='0'/>"
  "  </subjects>"
  "  <view>"
  "    <lv_obj name='box'>"
  "      <bind_flag_if cond='err or tmp gt 100' flag='hidden' invert='true'/>"
  "    </lv_obj>"
  "  </view>"
  "</component>";

TEST_CASE_METHOD(LVGLTestFixture, "cond: bind_flag_if with invert shows when cond true", "[xml_expr][cond]") {
    REQUIRE(lv_xml_register_component_from_data("t_cond", COMP_INVERT) == LV_RESULT_OK);
    lv_xml_component_scope_t * scope = lv_xml_component_get_scope("t_cond");
    REQUIRE(scope != nullptr);
    lv_obj_t * v = (lv_obj_t*)lv_xml_create(lv_screen_active(), "t_cond", nullptr);
    lv_obj_t * box = lv_obj_find_by_name(v, "box");
    REQUIRE(box != nullptr);
    lv_subject_t * err = lv_xml_get_subject(scope, "err");
    lv_subject_t * tmp = lv_xml_get_subject(scope, "tmp");
    REQUIRE(err != nullptr);
    REQUIRE(tmp != nullptr);
    // invert: hidden applied when cond FALSE → initially cond false → hidden set
    REQUIRE(lv_obj_has_flag(box, LV_OBJ_FLAG_HIDDEN));
    lv_subject_set_int(err, 1);                  // cond true → hidden removed
    REQUIRE_FALSE(lv_obj_has_flag(box, LV_OBJ_FLAG_HIDDEN));
    lv_subject_set_int(err, 0);
    lv_subject_set_int(tmp, 150);                // cond true again
    REQUIRE_FALSE(lv_obj_has_flag(box, LV_OBJ_FLAG_HIDDEN));
    // Delete the created instance BEFORE unregistering the component: the
    // instance's LV_EVENT_DELETE (via lv_xml_expr_bind) detaches its
    // observers from `err`/`tmp` while they're still alive. Unregistering
    // first would free the (component-scoped, shared) subjects out from
    // under the still-live widget's observers.
    lv_obj_delete(v);
    lv_xml_component_unregister("t_cond");
}

static const char * COMP_PLAIN =
  "<component>"
  "  <subjects>"
  "    <subject name='err2' type='int' value='0'/>"
  "    <subject name='tmp2' type='int' value='0'/>"
  "  </subjects>"
  "  <view>"
  "    <lv_obj name='box2'>"
  "      <bind_flag_if cond='err2 or tmp2 gt 100' flag='hidden'/>"
  "    </lv_obj>"
  "  </view>"
  "</component>";

TEST_CASE_METHOD(LVGLTestFixture, "cond: bind_flag_if without invert hides when cond true", "[xml_expr][cond]") {
    REQUIRE(lv_xml_register_component_from_data("t_cond2", COMP_PLAIN) == LV_RESULT_OK);
    lv_xml_component_scope_t * scope2 = lv_xml_component_get_scope("t_cond2");
    REQUIRE(scope2 != nullptr);
    lv_obj_t * v = (lv_obj_t*)lv_xml_create(lv_screen_active(), "t_cond2", nullptr);
    lv_obj_t * box = lv_obj_find_by_name(v, "box2");
    REQUIRE(box != nullptr);
    lv_subject_t * err2 = lv_xml_get_subject(scope2, "err2");
    lv_subject_t * tmp2 = lv_xml_get_subject(scope2, "tmp2");
    REQUIRE(err2 != nullptr);
    REQUIRE(tmp2 != nullptr);
    // no invert: hidden applied when cond TRUE → initially cond false → hidden NOT set
    REQUIRE_FALSE(lv_obj_has_flag(box, LV_OBJ_FLAG_HIDDEN));
    lv_subject_set_int(err2, 1);                 // cond true → hidden applied
    REQUIRE(lv_obj_has_flag(box, LV_OBJ_FLAG_HIDDEN));
    lv_subject_set_int(err2, 0);                 // cond false again → hidden removed
    REQUIRE_FALSE(lv_obj_has_flag(box, LV_OBJ_FLAG_HIDDEN));
    lv_subject_set_int(tmp2, 150);                // cond true via other input
    REQUIRE(lv_obj_has_flag(box, LV_OBJ_FLAG_HIDDEN));
    lv_obj_delete(v);   // see comment in the invert test above
    lv_xml_component_unregister("t_cond2");
}

static const char * COMP_STATE_INVERT =
  "<component>"
  "  <subjects>"
  "    <subject name='busy' type='int' value='0'/>"
  "  </subjects>"
  "  <view>"
  "    <lv_obj name='box3'>"
  "      <bind_state_if cond='busy' state='disabled' invert='true'/>"
  "    </lv_obj>"
  "  </view>"
  "</component>";

TEST_CASE_METHOD(LVGLTestFixture, "cond: bind_state_if with invert applies state when cond false", "[xml_expr][cond]") {
    REQUIRE(lv_xml_register_component_from_data("t_cond_state_inv", COMP_STATE_INVERT) == LV_RESULT_OK);
    lv_xml_component_scope_t * scope = lv_xml_component_get_scope("t_cond_state_inv");
    REQUIRE(scope != nullptr);
    lv_obj_t * v = (lv_obj_t*)lv_xml_create(lv_screen_active(), "t_cond_state_inv", nullptr);
    lv_obj_t * box = lv_obj_find_by_name(v, "box3");
    REQUIRE(box != nullptr);
    lv_subject_t * busy = lv_xml_get_subject(scope, "busy");
    REQUIRE(busy != nullptr);
    // invert: state applied when cond FALSE -> initially cond false -> disabled set
    REQUIRE(lv_obj_has_state(box, LV_STATE_DISABLED));
    lv_subject_set_int(busy, 1);                  // cond true -> disabled removed
    REQUIRE_FALSE(lv_obj_has_state(box, LV_STATE_DISABLED));
    lv_subject_set_int(busy, 0);                  // cond false again -> disabled set
    REQUIRE(lv_obj_has_state(box, LV_STATE_DISABLED));
    lv_obj_delete(v);
    lv_xml_component_unregister("t_cond_state_inv");
}

static const char * COMP_STATE_PLAIN =
  "<component>"
  "  <subjects>"
  "    <subject name='busy2' type='int' value='0'/>"
  "  </subjects>"
  "  <view>"
  "    <lv_obj name='box4'>"
  "      <bind_state_if cond='busy2' state='disabled'/>"
  "    </lv_obj>"
  "  </view>"
  "</component>";

TEST_CASE_METHOD(LVGLTestFixture, "cond: bind_state_if without invert applies state when cond true", "[xml_expr][cond]") {
    REQUIRE(lv_xml_register_component_from_data("t_cond_state", COMP_STATE_PLAIN) == LV_RESULT_OK);
    lv_xml_component_scope_t * scope = lv_xml_component_get_scope("t_cond_state");
    REQUIRE(scope != nullptr);
    lv_obj_t * v = (lv_obj_t*)lv_xml_create(lv_screen_active(), "t_cond_state", nullptr);
    lv_obj_t * box = lv_obj_find_by_name(v, "box4");
    REQUIRE(box != nullptr);
    lv_subject_t * busy2 = lv_xml_get_subject(scope, "busy2");
    REQUIRE(busy2 != nullptr);
    // no invert: state applied when cond TRUE -> initially cond false -> not set
    REQUIRE_FALSE(lv_obj_has_state(box, LV_STATE_DISABLED));
    lv_subject_set_int(busy2, 1);                 // cond true -> disabled applied
    REQUIRE(lv_obj_has_state(box, LV_STATE_DISABLED));
    lv_subject_set_int(busy2, 0);                 // cond false again -> disabled removed
    REQUIRE_FALSE(lv_obj_has_state(box, LV_STATE_DISABLED));
    lv_obj_delete(v);
    lv_xml_component_unregister("t_cond_state");
}

static const char * COMP_STYLE_PLAIN =
  "<component>"
  "  <subjects>"
  "    <subject name='active' type='int' value='0'/>"
  "  </subjects>"
  "  <styles>"
  "    <style name='hot' bg_color='0xff0000' bg_opa='255'/>"
  "  </styles>"
  "  <view>"
  "    <lv_obj name='box5'>"
  "      <bind_style_if cond='active' name='hot'/>"
  "    </lv_obj>"
  "  </view>"
  "</component>";

TEST_CASE_METHOD(LVGLTestFixture, "cond: bind_style_if without invert enables style when cond true", "[xml_expr][cond]") {
    REQUIRE(lv_xml_register_component_from_data("t_cond_style", COMP_STYLE_PLAIN) == LV_RESULT_OK);
    lv_xml_component_scope_t * scope = lv_xml_component_get_scope("t_cond_style");
    REQUIRE(scope != nullptr);
    lv_obj_t * v = (lv_obj_t*)lv_xml_create(lv_screen_active(), "t_cond_style", nullptr);
    lv_obj_t * box = lv_obj_find_by_name(v, "box5");
    REQUIRE(box != nullptr);
    lv_subject_t * active = lv_xml_get_subject(scope, "active");
    REQUIRE(active != nullptr);
    lv_color_t red = lv_color_hex(0xff0000);
    // no invert: style enabled when cond TRUE -> initially cond false -> style disabled
    REQUIRE_FALSE(lv_color_eq(lv_obj_get_style_bg_color(box, LV_PART_MAIN), red));
    lv_subject_set_int(active, 1);                // cond true -> style enabled
    REQUIRE(lv_color_eq(lv_obj_get_style_bg_color(box, LV_PART_MAIN), red));
    lv_subject_set_int(active, 0);                // cond false again -> style disabled
    REQUIRE_FALSE(lv_color_eq(lv_obj_get_style_bg_color(box, LV_PART_MAIN), red));
    lv_obj_delete(v);
    lv_xml_component_unregister("t_cond_style");
}

static const char * COMP_STYLE_INVERT =
  "<component>"
  "  <subjects>"
  "    <subject name='active2' type='int' value='0'/>"
  "  </subjects>"
  "  <styles>"
  "    <style name='hot2' bg_color='0x00ff00' bg_opa='255'/>"
  "  </styles>"
  "  <view>"
  "    <lv_obj name='box6'>"
  "      <bind_style_if cond='active2' name='hot2' invert='true'/>"
  "    </lv_obj>"
  "  </view>"
  "</component>";

TEST_CASE_METHOD(LVGLTestFixture, "cond: bind_style_if with invert enables style when cond false", "[xml_expr][cond]") {
    REQUIRE(lv_xml_register_component_from_data("t_cond_style_inv", COMP_STYLE_INVERT) == LV_RESULT_OK);
    lv_xml_component_scope_t * scope = lv_xml_component_get_scope("t_cond_style_inv");
    REQUIRE(scope != nullptr);
    lv_obj_t * v = (lv_obj_t*)lv_xml_create(lv_screen_active(), "t_cond_style_inv", nullptr);
    lv_obj_t * box = lv_obj_find_by_name(v, "box6");
    REQUIRE(box != nullptr);
    lv_subject_t * active2 = lv_xml_get_subject(scope, "active2");
    REQUIRE(active2 != nullptr);
    lv_color_t green = lv_color_hex(0x00ff00);
    // invert: style enabled when cond FALSE -> initially cond false -> style enabled
    REQUIRE(lv_color_eq(lv_obj_get_style_bg_color(box, LV_PART_MAIN), green));
    lv_subject_set_int(active2, 1);               // cond true -> style disabled
    REQUIRE_FALSE(lv_color_eq(lv_obj_get_style_bg_color(box, LV_PART_MAIN), green));
    lv_subject_set_int(active2, 0);               // cond false again -> style enabled
    REQUIRE(lv_color_eq(lv_obj_get_style_bg_color(box, LV_PART_MAIN), green));
    lv_obj_delete(v);
    lv_xml_component_unregister("t_cond_style_inv");
}
