// SPDX-License-Identifier: GPL-3.0-or-later
#include "../catch_amalgamated.hpp"
#include "../lvgl_test_fixture.h"
extern "C" {
#include "helix-xml/src/xml/lv_xml.h"
}

// C++ registers demo_0_v..demo_2_v; repeat binds each label to its own indexed subject.
static const char * COMP_IDX =
  "<component>"
  "  <view>"
  "    <lv_obj name='root'>"
  "      <repeat count='3'>"
  "        <lv_label name='lbl' bind_text='demo_${i}_v'/>"
  "      </repeat>"
  "    </lv_obj>"
  "  </view>"
  "</component>";

TEST_CASE_METHOD(LVGLTestFixture, "indexed: ${i} composes per-iteration subject name", "[xml][indexed]") {
    static char b0[32], b1[32], b2[32];
    static lv_subject_t s0, s1, s2;
    lv_subject_init_string(&s0, b0, nullptr, 32, "zero");
    lv_subject_init_string(&s1, b1, nullptr, 32, "one");
    lv_subject_init_string(&s2, b2, nullptr, 32, "two");
    lv_xml_register_subject(nullptr, "demo_0_v", &s0);
    lv_xml_register_subject(nullptr, "demo_1_v", &s1);
    lv_xml_register_subject(nullptr, "demo_2_v", &s2);

    REQUIRE(lv_xml_register_component_from_data("t_idx", COMP_IDX) == LV_RESULT_OK);
    lv_obj_t * v = (lv_obj_t*)lv_xml_create(lv_screen_active(), "t_idx", nullptr);
    lv_obj_t * root = lv_obj_find_by_name(v, "root");
    REQUIRE(lv_obj_get_child_count(root) == 3);
    REQUIRE(std::string(lv_label_get_text(lv_obj_get_child(root, 0))) == "zero");
    REQUIRE(std::string(lv_label_get_text(lv_obj_get_child(root, 1))) == "one");
    REQUIRE(std::string(lv_label_get_text(lv_obj_get_child(root, 2))) == "two");
    lv_obj_delete(v);
    lv_xml_component_unregister("t_idx");
    // NOTE: global subjects intentionally left registered/live for the process;
    // deinit pattern mirrors test_xml_subject_expr.cpp global-input test.
}

// A component param (${grp}) and the loop index (${i}) compose together into one
// subject name: status_${grp}_${i}_x -> status_fan_0_x, status_fan_1_x.
static const char * COMP_IDX_PARAM =
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

TEST_CASE_METHOD(LVGLTestFixture, "indexed: ${param} + ${i} compose one subject name", "[xml][indexed]") {
    static char pb0[32], pb1[32];
    static lv_subject_t p0, p1;
    lv_subject_init_string(&p0, pb0, nullptr, 32, "fan-zero");
    lv_subject_init_string(&p1, pb1, nullptr, 32, "fan-one");
    lv_xml_register_subject(nullptr, "status_fan_0_x", &p0);
    lv_xml_register_subject(nullptr, "status_fan_1_x", &p1);

    REQUIRE(lv_xml_register_component_from_data("t_idx2", COMP_IDX_PARAM) == LV_RESULT_OK);
    const char * attrs[] = {"grp", "fan", nullptr};
    lv_obj_t * v = (lv_obj_t*)lv_xml_create(lv_screen_active(), "t_idx2", attrs);
    lv_obj_t * root = lv_obj_find_by_name(v, "root");
    REQUIRE(lv_obj_get_child_count(root) == 2);
    REQUIRE(std::string(lv_label_get_text(lv_obj_get_child(root, 0))) == "fan-zero");
    REQUIRE(std::string(lv_label_get_text(lv_obj_get_child(root, 1))) == "fan-one");
    lv_obj_delete(v);
    lv_xml_component_unregister("t_idx2");
}

// An unknown ${name} splices empty and degrades gracefully (no crash); the label
// binds to a non-existent subject and keeps its default text.
static const char * COMP_IDX_UNRESOLVED =
  "<component>"
  "  <view>"
  "    <lv_obj name='root'>"
  "      <repeat count='2'>"
  "        <lv_label name='lbl' bind_text='x_${nope}_${i}'/>"
  "      </repeat>"
  "    </lv_obj>"
  "  </view>"
  "</component>";

TEST_CASE_METHOD(LVGLTestFixture, "indexed: unknown ${name} splices empty, no crash", "[xml][indexed]") {
    REQUIRE(lv_xml_register_component_from_data("t_idx3", COMP_IDX_UNRESOLVED) == LV_RESULT_OK);
    lv_obj_t * v = (lv_obj_t*)lv_xml_create(lv_screen_active(), "t_idx3", nullptr);
    lv_obj_t * root = lv_obj_find_by_name(v, "root");
    REQUIRE(lv_obj_get_child_count(root) == 2);  // bodies still expand
    lv_obj_delete(v);
    lv_xml_component_unregister("t_idx3");
    SUCCEED("unknown ${name} did not crash");
}
