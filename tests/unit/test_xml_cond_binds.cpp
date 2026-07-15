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
