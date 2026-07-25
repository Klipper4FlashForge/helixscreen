// SPDX-License-Identifier: GPL-3.0-or-later
#include "../catch_amalgamated.hpp"
#include "../lvgl_test_fixture.h"
#include <string>
extern "C" {
#include "helix-xml/src/xml/lv_xml.h"
}

// ${i + 1}: 1-based names inside a <repeat count='4'> -> slot_1_label .. slot_4_label.
static const char * COMP_EXPR_NAME =
  "<component>"
  "  <view>"
  "    <lv_obj name='root'>"
  "      <repeat count='4'>"
  "        <lv_label name='lbl' bind_text='slot_${i + 1}_label'/>"
  "      </repeat>"
  "    </lv_obj>"
  "  </view>"
  "</component>";

TEST_CASE_METHOD(LVGLTestFixture, "expr: ${i + 1} composes 1-based subject names", "[xml][expr_compose]") {
    static char b[4][32];
    static lv_subject_t s[4];
    for (int k = 0; k < 4; k++) {
        lv_subject_init_string(&s[k], b[k], nullptr, 32, k == 0 ? "one" : k == 1 ? "two" : k == 2 ? "three" : "four");
        lv_xml_register_subject(nullptr, ("slot_" + std::to_string(k + 1) + "_label").c_str(), &s[k]);
    }
    REQUIRE(lv_xml_register_component_from_data("t_expr1", COMP_EXPR_NAME) == LV_RESULT_OK);
    lv_obj_t * v = (lv_obj_t*)lv_xml_create(lv_screen_active(), "t_expr1", nullptr);
    lv_obj_t * root = lv_obj_find_by_name(v, "root");
    REQUIRE(lv_obj_get_child_count(root) == 4);
    REQUIRE(std::string(lv_label_get_text(lv_obj_get_child(root, 0))) == "one");
    REQUIRE(std::string(lv_label_get_text(lv_obj_get_child(root, 3))) == "four");
    lv_obj_delete(v);
    lv_xml_component_unregister("t_expr1");
}

// ${i * 84}: computed numeric attribute (style_translate_x) -> 0, 84, 168, 252.
static const char * COMP_EXPR_NUM =
  "<component>"
  "  <view>"
  "    <lv_obj name='root'>"
  "      <repeat count='4'>"
  "        <lv_obj name='cell' width='10' height='10' style_translate_x='${i * 84}'/>"
  "      </repeat>"
  "    </lv_obj>"
  "  </view>"
  "</component>";

TEST_CASE_METHOD(LVGLTestFixture, "expr: ${i * 84} computes a numeric attribute", "[xml][expr_compose]") {
    REQUIRE(lv_xml_register_component_from_data("t_expr2", COMP_EXPR_NUM) == LV_RESULT_OK);
    lv_obj_t * v = (lv_obj_t*)lv_xml_create(lv_screen_active(), "t_expr2", nullptr);
    lv_obj_t * root = lv_obj_find_by_name(v, "root");
    REQUIRE(lv_obj_get_child_count(root) == 4);
    REQUIRE(lv_obj_get_style_translate_x(lv_obj_get_child(root, 0), LV_PART_MAIN) == 0);
    REQUIRE(lv_obj_get_style_translate_x(lv_obj_get_child(root, 1), LV_PART_MAIN) == 84);
    REQUIRE(lv_obj_get_style_translate_x(lv_obj_get_child(root, 3), LV_PART_MAIN) == 252);
    lv_obj_delete(v);
    lv_xml_component_unregister("t_expr2");
}

// Equivalence: ${i} (legacy name path) and ${i + 0} (expr path) yield the same value.
static const char * COMP_EXPR_EQUIV =
  "<component>"
  "  <view>"
  "    <lv_obj name='root'>"
  "      <repeat count='3'>"
  "        <lv_obj name='a' width='5' height='5' style_translate_x='${i}'/>"
  "        <lv_obj name='b' width='5' height='5' style_translate_x='${i + 0}'/>"
  "      </repeat>"
  "    </lv_obj>"
  "  </view>"
  "</component>";

TEST_CASE_METHOD(LVGLTestFixture, "expr: ${i} equals ${i + 0}", "[xml][expr_compose]") {
    REQUIRE(lv_xml_register_component_from_data("t_expr3", COMP_EXPR_EQUIV) == LV_RESULT_OK);
    lv_obj_t * v = (lv_obj_t*)lv_xml_create(lv_screen_active(), "t_expr3", nullptr);
    lv_obj_t * root = lv_obj_find_by_name(v, "root");
    REQUIRE(lv_obj_get_child_count(root) == 6);  // 2 per iteration * 3
    for (int k = 0; k < 3; k++) {
        int32_t a = lv_obj_get_style_translate_x(lv_obj_get_child(root, k * 2), LV_PART_MAIN);
        int32_t b = lv_obj_get_style_translate_x(lv_obj_get_child(root, k * 2 + 1), LV_PART_MAIN);
        REQUIRE(a == b);
        REQUIRE(a == k);
    }
    lv_obj_delete(v);
    lv_xml_component_unregister("t_expr3");
}

// Subject operands, RESOLVE-ONCE: width = base * scale is evaluated once; a later
// subject change does NOT update the width. This test locks the resolve-once contract.
static const char * COMP_EXPR_SUBJ =
  "<component>"
  "  <view>"
  "    <lv_obj name='g' width='${base * scale}' height='10'/>"
  "  </view>"
  "</component>";

TEST_CASE_METHOD(LVGLTestFixture, "expr: subject operands are resolve-once", "[xml][expr_compose]") {
    static lv_subject_t base, scale;
    lv_subject_init_int(&base, 10);
    lv_subject_init_int(&scale, 3);
    lv_xml_register_subject(nullptr, "base", &base);
    lv_xml_register_subject(nullptr, "scale", &scale);

    REQUIRE(lv_xml_register_component_from_data("t_expr4", COMP_EXPR_SUBJ) == LV_RESULT_OK);
    lv_obj_t * v = (lv_obj_t*)lv_xml_create(lv_screen_active(), "t_expr4", nullptr);
    lv_obj_t * g = lv_obj_find_by_name(v, "g");
    process_lvgl(20);
    lv_obj_update_layout(g);
    REQUIRE(lv_obj_get_width(g) == 30);

    lv_subject_set_int(&base, 100);              // change an operand
    process_lvgl(20);
    lv_obj_update_layout(g);
    REQUIRE(lv_obj_get_width(g) == 30);          // resolve-once: width did NOT change

    lv_obj_delete(v);
    lv_xml_component_unregister("t_expr4");
}

// Malformed / unknown expressions splice empty and warn; no crash.
static const char * COMP_EXPR_BAD =
  "<component>"
  "  <view>"
  "    <lv_obj name='root'>"
  "      <lv_obj name='a' width='5' height='5' style_translate_x='${bogus_ident + 1}'/>"
  "      <lv_obj name='b' width='5' height='5' style_translate_x='${5 + }'/>"
  "    </lv_obj>"
  "  </view>"
  "</component>";

TEST_CASE_METHOD(LVGLTestFixture, "expr: malformed/unknown splices empty, no crash", "[xml][expr_compose]") {
    REQUIRE(lv_xml_register_component_from_data("t_expr5", COMP_EXPR_BAD) == LV_RESULT_OK);
    lv_obj_t * v = (lv_obj_t*)lv_xml_create(lv_screen_active(), "t_expr5", nullptr);
    lv_obj_t * root = lv_obj_find_by_name(v, "root");
    REQUIRE(lv_obj_get_child_count(root) == 2);  // widgets still created
    // both malformed exprs splice empty; empty translate_x parses to 0 in the numeric apply path
    REQUIRE(lv_obj_get_style_translate_x(lv_obj_get_child(root, 0), LV_PART_MAIN) == 0);  // ${bogus_ident + 1}
    REQUIRE(lv_obj_get_style_translate_x(lv_obj_get_child(root, 1), LV_PART_MAIN) == 0);  // ${5 + }
    lv_obj_delete(v);
    lv_xml_component_unregister("t_expr5");
    SUCCEED("malformed expression did not crash");
}

// Backward compatibility: bare ${i} and bare ${grp} keep the legacy name-substitution path.
static const char * COMP_EXPR_COMPAT =
  "<component>"
  "  <api><prop name='grp' type='string'/></api>"
  "  <view>"
  "    <lv_obj name='root'>"
  "      <repeat count='2'>"
  "        <lv_label name='lbl' bind_text='status_${grp}_${i}_x'/>"
  "      </repeat>"
  "    </lv_obj>"
  "  </view>"
  "</component>";

TEST_CASE_METHOD(LVGLTestFixture, "expr: bare ${i}/${grp} unchanged (backward compat)", "[xml][expr_compose]") {
    static char pb0[32], pb1[32];
    static lv_subject_t p0, p1;
    lv_subject_init_string(&p0, pb0, nullptr, 32, "compat-zero");
    lv_subject_init_string(&p1, pb1, nullptr, 32, "compat-one");
    lv_xml_register_subject(nullptr, "status_fan_0_x", &p0);
    lv_xml_register_subject(nullptr, "status_fan_1_x", &p1);

    REQUIRE(lv_xml_register_component_from_data("t_expr6", COMP_EXPR_COMPAT) == LV_RESULT_OK);
    const char * attrs[] = {"grp", "fan", nullptr};
    lv_obj_t * v = (lv_obj_t*)lv_xml_create(lv_screen_active(), "t_expr6", attrs);
    lv_obj_t * root = lv_obj_find_by_name(v, "root");
    REQUIRE(lv_obj_get_child_count(root) == 2);
    REQUIRE(std::string(lv_label_get_text(lv_obj_get_child(root, 0))) == "compat-zero");
    REQUIRE(std::string(lv_label_get_text(lv_obj_get_child(root, 1))) == "compat-one");
    lv_obj_delete(v);
    lv_xml_component_unregister("t_expr6");
}

// Numeric component param as an operand: cols="4" -> ${cols * 2} = 8.
static const char * COMP_EXPR_PARAM =
  "<component>"
  "  <api><prop name='cols' type='string'/></api>"
  "  <view>"
  "    <lv_obj name='g' width='${cols * 2}' height='10'/>"
  "  </view>"
  "</component>";

TEST_CASE_METHOD(LVGLTestFixture, "expr: numeric param is a valid operand", "[xml][expr_compose]") {
    REQUIRE(lv_xml_register_component_from_data("t_expr7", COMP_EXPR_PARAM) == LV_RESULT_OK);
    const char * attrs[] = {"cols", "4", nullptr};
    lv_obj_t * v = (lv_obj_t*)lv_xml_create(lv_screen_active(), "t_expr7", attrs);
    lv_obj_t * g = lv_obj_find_by_name(v, "g");
    process_lvgl(20);
    lv_obj_update_layout(g);
    REQUIRE(lv_obj_get_width(g) == 8);
    lv_obj_delete(v);
    lv_xml_component_unregister("t_expr7");
}

// Non-numeric param in arithmetic: grp="fan" -> ${grp * 2} fails to compile,
// splices empty (numeric apply -> width 0), no crash.
static const char * COMP_EXPR_PARAM_BAD =
  "<component>"
  "  <api><prop name='grp' type='string'/></api>"
  "  <view>"
  "    <lv_obj name='g' width='${grp * 2}' height='10'/>"
  "  </view>"
  "</component>";

TEST_CASE_METHOD(LVGLTestFixture, "expr: non-numeric param in arithmetic degrades", "[xml][expr_compose]") {
    REQUIRE(lv_xml_register_component_from_data("t_expr8", COMP_EXPR_PARAM_BAD) == LV_RESULT_OK);
    const char * attrs[] = {"grp", "fan", nullptr};
    lv_obj_t * v = (lv_obj_t*)lv_xml_create(lv_screen_active(), "t_expr8", attrs);
    lv_obj_t * g = lv_obj_find_by_name(v, "g");
    process_lvgl(20);
    lv_obj_update_layout(g);
    // Assert the parsed style value, not just the laid-out width: this used to
    // read as a flake (#1121) because lv_xml_to_size("") indexed txt[-1] and a
    // stray '%' in the preceding byte produced lv_pct(0) instead of 0, which then
    // laid out to some other number. Checking width_def points a regression at
    // the parser directly. See test_xml_size_parsing.cpp.
    REQUIRE((int32_t)lv_obj_get_style_width(g, LV_PART_MAIN) == 0);
    REQUIRE(lv_obj_get_width(g) == 0);   // empty width string -> 0
    lv_obj_delete(v);
    lv_xml_component_unregister("t_expr8");
    SUCCEED("non-numeric param in arithmetic did not crash");
}
