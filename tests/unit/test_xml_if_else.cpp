// SPDX-License-Identifier: GPL-3.0-or-later
#include "../catch_amalgamated.hpp"
#include "../lvgl_test_fixture.h"
extern "C" {
#include "helix-xml/src/xml/lv_xml.h"
}

static const char * COMP_STATIC_TRUE =
  "<component><subjects><subject name='c' type='int' value='1'/></subjects>"
  "  <view><lv_obj name='root'>"
  "    <if cond='c gt 0'><lv_obj name='t'/><else/><lv_obj name='f'/></if>"
  "  </lv_obj></view></component>";

TEST_CASE_METHOD(LVGLTestFixture, "if: static true -> true-body only", "[xml][if]") {
    REQUIRE(lv_xml_register_component_from_data("if_st", COMP_STATIC_TRUE) == LV_RESULT_OK);
    lv_obj_t * v = (lv_obj_t *)lv_xml_create(lv_screen_active(), "if_st", nullptr);
    lv_obj_t * root = lv_obj_find_by_name(v, "root");
    REQUIRE(root != nullptr);
    REQUIRE(lv_obj_find_by_name(v, "t") != nullptr);
    REQUIRE(lv_obj_find_by_name(v, "f") == nullptr);
    lv_obj_delete(v);
    lv_xml_component_unregister("if_st");
}

static const char * COMP_STATIC_FALSE =
  "<component><subjects><subject name='c' type='int' value='0'/></subjects>"
  "  <view><lv_obj name='root'>"
  "    <if cond='c gt 0'><lv_obj name='t'/><else/><lv_obj name='f'/></if>"
  "  </lv_obj></view></component>";

TEST_CASE_METHOD(LVGLTestFixture, "if: static false -> false-body only", "[xml][if]") {
    REQUIRE(lv_xml_register_component_from_data("if_sf", COMP_STATIC_FALSE) == LV_RESULT_OK);
    lv_obj_t * v = (lv_obj_t *)lv_xml_create(lv_screen_active(), "if_sf", nullptr);
    lv_obj_t * root = lv_obj_find_by_name(v, "root");
    REQUIRE(root != nullptr);
    REQUIRE(lv_obj_find_by_name(v, "t") == nullptr);
    REQUIRE(lv_obj_find_by_name(v, "f") != nullptr);
    lv_obj_delete(v);
    lv_xml_component_unregister("if_sf");
}

static const char * COMP_NO_ELSE_FALSE =
  "<component><subjects><subject name='c' type='int' value='0'/></subjects>"
  "  <view><lv_obj name='root'>"
  "    <if cond='c gt 0'><lv_obj name='t'/></if>"
  "  </lv_obj></view></component>";

TEST_CASE_METHOD(LVGLTestFixture, "if: no else, static false -> nothing, component still loads", "[xml][if]") {
    REQUIRE(lv_xml_register_component_from_data("if_noelse_f", COMP_NO_ELSE_FALSE) == LV_RESULT_OK);
    lv_obj_t * v = (lv_obj_t *)lv_xml_create(lv_screen_active(), "if_noelse_f", nullptr);
    lv_obj_t * root = lv_obj_find_by_name(v, "root");
    REQUIRE(root != nullptr);
    REQUIRE(lv_obj_find_by_name(v, "t") == nullptr);
    REQUIRE(lv_obj_get_child_count(root) == 0);
    lv_obj_delete(v);
    lv_xml_component_unregister("if_noelse_f");
}

/* --- Edge cases (Step 6) --- */

static const char * COMP_ELSE_SELFCLOSE_FALSE =
  "<component><subjects><subject name='c' type='int' value='0'/></subjects>"
  "  <view><lv_obj name='root'>"
  "    <if cond='c gt 0'><lv_obj name='t'/><else/><lv_obj name='f'/></if>"
  "  </lv_obj></view></component>";

static const char * COMP_ELSE_SELFCLOSE_TRUE =
  "<component><subjects><subject name='c' type='int' value='1'/></subjects>"
  "  <view><lv_obj name='root'>"
  "    <if cond='c gt 0'><lv_obj name='t'/><else/><lv_obj name='f'/></if>"
  "  </lv_obj></view></component>";

static const char * COMP_ELSE_OPENCLOSE_FALSE =
  "<component><subjects><subject name='c' type='int' value='0'/></subjects>"
  "  <view><lv_obj name='root'>"
  "    <if cond='c gt 0'><lv_obj name='t'/><else></else><lv_obj name='f'/></if>"
  "  </lv_obj></view></component>";

static const char * COMP_ELSE_OPENCLOSE_TRUE =
  "<component><subjects><subject name='c' type='int' value='1'/></subjects>"
  "  <view><lv_obj name='root'>"
  "    <if cond='c gt 0'><lv_obj name='t'/><else></else><lv_obj name='f'/></if>"
  "  </lv_obj></view></component>";

TEST_CASE_METHOD(LVGLTestFixture, "if: <else/> and <else></else> produce identical split", "[xml][if]") {
    REQUIRE(lv_xml_register_component_from_data("if_e1f", COMP_ELSE_SELFCLOSE_FALSE) == LV_RESULT_OK);
    REQUIRE(lv_xml_register_component_from_data("if_e1t", COMP_ELSE_SELFCLOSE_TRUE) == LV_RESULT_OK);
    REQUIRE(lv_xml_register_component_from_data("if_e2f", COMP_ELSE_OPENCLOSE_FALSE) == LV_RESULT_OK);
    REQUIRE(lv_xml_register_component_from_data("if_e2t", COMP_ELSE_OPENCLOSE_TRUE) == LV_RESULT_OK);

    {
        lv_obj_t * v = (lv_obj_t *)lv_xml_create(lv_screen_active(), "if_e1f", nullptr);
        REQUIRE(lv_obj_find_by_name(v, "t") == nullptr);
        REQUIRE(lv_obj_find_by_name(v, "f") != nullptr);
        lv_obj_delete(v);
    }
    {
        lv_obj_t * v = (lv_obj_t *)lv_xml_create(lv_screen_active(), "if_e2f", nullptr);
        REQUIRE(lv_obj_find_by_name(v, "t") == nullptr);
        REQUIRE(lv_obj_find_by_name(v, "f") != nullptr);
        lv_obj_delete(v);
    }
    {
        lv_obj_t * v = (lv_obj_t *)lv_xml_create(lv_screen_active(), "if_e1t", nullptr);
        REQUIRE(lv_obj_find_by_name(v, "t") != nullptr);
        REQUIRE(lv_obj_find_by_name(v, "f") == nullptr);
        lv_obj_delete(v);
    }
    {
        lv_obj_t * v = (lv_obj_t *)lv_xml_create(lv_screen_active(), "if_e2t", nullptr);
        REQUIRE(lv_obj_find_by_name(v, "t") != nullptr);
        REQUIRE(lv_obj_find_by_name(v, "f") == nullptr);
        lv_obj_delete(v);
    }

    lv_xml_component_unregister("if_e1f");
    lv_xml_component_unregister("if_e1t");
    lv_xml_component_unregister("if_e2f");
    lv_xml_component_unregister("if_e2t");
}

static const char * COMP_DOUBLE_ELSE =
  "<component><subjects><subject name='c' type='int' value='0'/></subjects>"
  "  <view><lv_obj name='root'>"
  "    <if cond='c gt 0'><lv_obj name='t'/><else/><lv_obj name='f1'/><else/><lv_obj name='f2'/></if>"
  "  </lv_obj></view></component>";

TEST_CASE_METHOD(LVGLTestFixture, "if: second <else> — first split wins", "[xml][if]") {
    REQUIRE(lv_xml_register_component_from_data("if_2else", COMP_DOUBLE_ELSE) == LV_RESULT_OK);
    lv_obj_t * v = (lv_obj_t *)lv_xml_create(lv_screen_active(), "if_2else", nullptr);
    lv_obj_t * root = lv_obj_find_by_name(v, "root");
    REQUIRE(root != nullptr);
    REQUIRE(lv_obj_find_by_name(v, "t") == nullptr);
    REQUIRE(lv_obj_find_by_name(v, "f1") != nullptr);
    REQUIRE(lv_obj_find_by_name(v, "f2") != nullptr);
    lv_obj_delete(v);
    lv_xml_component_unregister("if_2else");
}

static const char * COMP_STRAY_ELSE =
  "<component>"
  "  <view><lv_obj name='root'>"
  "    <else/>"
  "  </lv_obj></view></component>";

TEST_CASE_METHOD(LVGLTestFixture, "if: stray <else/> outside any <if> — warn + ignore, component loads", "[xml][if]") {
    REQUIRE(lv_xml_register_component_from_data("if_stray", COMP_STRAY_ELSE) == LV_RESULT_OK);
    lv_obj_t * v = (lv_obj_t *)lv_xml_create(lv_screen_active(), "if_stray", nullptr);
    REQUIRE(v != nullptr);
    lv_obj_t * root = lv_obj_find_by_name(v, "root");
    REQUIRE(root != nullptr);
    lv_obj_delete(v);
    lv_xml_component_unregister("if_stray");
}

// A stray <else/> pushes no parent-stack frame on start, so its end tag must pop
// none. If </else> falls through to the generic end-handler's unconditional pop, it
// removes the enclosing element's still-open frame one event early and mis-parents
// every following sibling. Assert the sibling after a stray <else/> still lands
// under root (both children present) — the discriminator the last-child-only stray
// test above cannot catch.
static const char * COMP_STRAY_ELSE_SIBLING =
  "<component>"
  "  <view><lv_obj name='root'>"
  "    <lv_obj name='a'/><else/><lv_obj name='b'/>"
  "  </lv_obj></view></component>";

TEST_CASE_METHOD(LVGLTestFixture, "if: stray <else/> does not corrupt the parent stack for following siblings", "[xml][if]") {
    REQUIRE(lv_xml_register_component_from_data("if_stray_sib", COMP_STRAY_ELSE_SIBLING) == LV_RESULT_OK);
    lv_obj_t * v = (lv_obj_t *)lv_xml_create(lv_screen_active(), "if_stray_sib", nullptr);
    REQUIRE(v != nullptr);
    lv_obj_t * root = lv_obj_find_by_name(v, "root");
    REQUIRE(root != nullptr);
    lv_obj_t * a = lv_obj_find_by_name(v, "a");
    lv_obj_t * b = lv_obj_find_by_name(v, "b");
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    // Both siblings must be DIRECT children of root; a premature </else> pop would
    // mis-parent b (root would hold only 'a').
    REQUIRE(lv_obj_get_parent(a) == root);
    REQUIRE(lv_obj_get_parent(b) == root);
    REQUIRE(lv_obj_get_child_count(root) == 2);
    lv_obj_delete(v);
    lv_xml_component_unregister("if_stray_sib");
}
