// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Regression: a `$prop|ref` value (used by hidden_if_prop_eq / _not_eq) must
// resolve only the param name before the pipe and keep `|ref` as a literal
// suffix. resolve_params() previously treated the whole "prop|ref" as one param
// name, failed the lookup, blanked the attribute, and silently disabled the
// hide — e.g. setting_dropdown_row's hide_description="true" never hid the
// description (debug bundle ET5ACW4S: `resolve_params: 'hide_description|true'
// parameter is not defined on 'setting_dropdown_row'`).
#include "../lvgl_test_fixture.h"
#include "../catch_amalgamated.hpp"
extern "C" {
#include "helix-xml/src/xml/lv_xml.h"
}

// Child exposes a `hp` prop and hides `target` when hp == "true".
static const char * COMP_PIPE_CHILD =
  "<component>"
  "  <api>"
  "    <prop name='hp' type='string' default='false'/>"
  "  </api>"
  "  <view>"
  "    <lv_obj name='target' hidden_if_prop_eq='$hp|true'/>"
  "  </view>"
  "</component>";

// Parent instantiates the child three ways: explicit true, explicit false, and
// unset (falls back to the default 'false').
static const char * COMP_PIPE_PARENT =
  "<component>"
  "  <view>"
  "    <t_pipe_child name='c_true' hp='true'/>"
  "    <t_pipe_child name='c_false' hp='false'/>"
  "    <t_pipe_child name='c_default'/>"
  "  </view>"
  "</component>";

TEST_CASE_METHOD(LVGLTestFixture, "prop-pipe: $prop|ref resolves name and keeps |ref suffix",
                 "[xml][prop][cond]") {
    REQUIRE(lv_xml_register_component_from_data("t_pipe_child", COMP_PIPE_CHILD) == LV_RESULT_OK);
    REQUIRE(lv_xml_register_component_from_data("t_pipe_parent", COMP_PIPE_PARENT) == LV_RESULT_OK);

    lv_obj_t * v = (lv_obj_t *)lv_xml_create(lv_screen_active(), "t_pipe_parent", nullptr);
    REQUIRE(v != nullptr);

    auto target_of = [&](const char * child_name) -> lv_obj_t * {
        lv_obj_t * child = lv_obj_find_by_name(v, child_name);
        REQUIRE(child != nullptr);
        return lv_obj_find_by_name(child, "target");
    };

    lv_obj_t * t_true = target_of("c_true");
    lv_obj_t * t_false = target_of("c_false");
    lv_obj_t * t_default = target_of("c_default");
    REQUIRE(t_true != nullptr);
    REQUIRE(t_false != nullptr);
    REQUIRE(t_default != nullptr);

    // hp=="true"  -> "true|true"  -> equal  -> hidden
    REQUIRE(lv_obj_has_flag(t_true, LV_OBJ_FLAG_HIDDEN));
    // hp=="false" -> "false|true" -> differ -> NOT hidden
    REQUIRE_FALSE(lv_obj_has_flag(t_false, LV_OBJ_FLAG_HIDDEN));
    // unset -> default "false" -> "false|true" -> differ -> NOT hidden
    REQUIRE_FALSE(lv_obj_has_flag(t_default, LV_OBJ_FLAG_HIDDEN));

    lv_obj_delete(v);
    lv_xml_component_unregister("t_pipe_parent");
    lv_xml_component_unregister("t_pipe_child");
}

// _not_eq is the mirror: hide when the resolved value differs from ref.
static const char * COMP_PIPE_NEQ_CHILD =
  "<component>"
  "  <api>"
  "    <prop name='hp' type='string' default='false'/>"
  "  </api>"
  "  <view>"
  "    <lv_obj name='target' hidden_if_prop_not_eq='$hp|true'/>"
  "  </view>"
  "</component>";

static const char * COMP_PIPE_NEQ_PARENT =
  "<component>"
  "  <view>"
  "    <t_pipe_neq_child name='c_true' hp='true'/>"
  "    <t_pipe_neq_child name='c_false' hp='false'/>"
  "  </view>"
  "</component>";

TEST_CASE_METHOD(LVGLTestFixture, "prop-pipe: $prop|ref works for hidden_if_prop_not_eq",
                 "[xml][prop][cond]") {
    REQUIRE(lv_xml_register_component_from_data("t_pipe_neq_child", COMP_PIPE_NEQ_CHILD) == LV_RESULT_OK);
    REQUIRE(lv_xml_register_component_from_data("t_pipe_neq_parent", COMP_PIPE_NEQ_PARENT) == LV_RESULT_OK);

    lv_obj_t * v = (lv_obj_t *)lv_xml_create(lv_screen_active(), "t_pipe_neq_parent", nullptr);
    REQUIRE(v != nullptr);

    lv_obj_t * c_true = lv_obj_find_by_name(v, "c_true");
    lv_obj_t * c_false = lv_obj_find_by_name(v, "c_false");
    REQUIRE(c_true != nullptr);
    REQUIRE(c_false != nullptr);
    lv_obj_t * t_true = lv_obj_find_by_name(c_true, "target");
    lv_obj_t * t_false = lv_obj_find_by_name(c_false, "target");

    // not_eq: hide when value != ref.
    // hp=="true"  -> "true|true"  -> equal  -> NOT hidden
    REQUIRE_FALSE(lv_obj_has_flag(t_true, LV_OBJ_FLAG_HIDDEN));
    // hp=="false" -> "false|true" -> differ -> hidden
    REQUIRE(lv_obj_has_flag(t_false, LV_OBJ_FLAG_HIDDEN));

    lv_obj_delete(v);
    lv_xml_component_unregister("t_pipe_neq_parent");
    lv_xml_component_unregister("t_pipe_neq_child");
}
