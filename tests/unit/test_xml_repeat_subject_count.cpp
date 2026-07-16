// SPDX-License-Identifier: GPL-3.0-or-later
#include "../catch_amalgamated.hpp"
#include "../lvgl_test_fixture.h"
extern "C" {
#include "helix-xml/src/xml/lv_xml.h"
}

static const char * COMP_CNT =
  "<component>"
  "  <subjects><subject name='n' type='int' value='2'/></subjects>"
  "  <view>"
  "    <lv_obj name='root'>"
  "      <repeat count='n'><lv_obj name='item'/></repeat>"
  "    </lv_obj>"
  "  </view>"
  "</component>";

TEST_CASE_METHOD(LVGLTestFixture, "repeat: subject count expands then rebuilds on change",
                 "[xml][repeat][slow]") {
    REQUIRE(lv_xml_register_component_from_data("t_cnt", COMP_CNT) == LV_RESULT_OK);
    lv_xml_component_scope_t * scope = lv_xml_component_get_scope("t_cnt");
    lv_obj_t * v = (lv_obj_t *)lv_xml_create(lv_screen_active(), "t_cnt", nullptr);
    lv_obj_t * root = lv_obj_find_by_name(v, "root");
    lv_subject_t * n = lv_xml_get_subject(scope, "n");
    REQUIRE(root != nullptr);
    REQUIRE(n != nullptr);
    REQUIRE(lv_obj_get_child_count(root) == 2);   // initial value

    lv_subject_set_int(n, 5);
    process_lvgl(50);                              // drain async teardown + rebuild
    REQUIRE(lv_obj_get_child_count(root) == 5);

    lv_subject_set_int(n, 0);
    process_lvgl(50);
    REQUIRE(lv_obj_get_child_count(root) == 0);

    lv_obj_delete(v);
    lv_xml_component_unregister("t_cnt");
}

TEST_CASE_METHOD(LVGLTestFixture, "repeat: rapid count churn coalesces to the final value",
                 "[xml][repeat][slow]") {
    REQUIRE(lv_xml_register_component_from_data("t_churn", COMP_CNT) == LV_RESULT_OK);
    lv_xml_component_scope_t * scope = lv_xml_component_get_scope("t_churn");
    lv_obj_t * v = (lv_obj_t *)lv_xml_create(lv_screen_active(), "t_churn", nullptr);
    lv_obj_t * root = lv_obj_find_by_name(v, "root");
    lv_subject_t * n = lv_xml_get_subject(scope, "n");
    REQUIRE(root != nullptr);
    REQUIRE(lv_obj_get_child_count(root) == 2);

    // Three changes before a single drain: each rebuild reads the current value, so
    // the live tree already reflects 3 by the time we drain. Draining must free the
    // three separate condemned sinks without corrupting the event list.
    lv_subject_set_int(n, 1);
    lv_subject_set_int(n, 8);
    lv_subject_set_int(n, 3);
    REQUIRE(lv_obj_get_child_count(root) == 3);   // live tree already coalesced
    process_lvgl(80);                             // drain all pending async teardowns
    REQUIRE(lv_obj_get_child_count(root) == 3);

    lv_obj_delete(v);
    lv_xml_component_unregister("t_churn");
}

TEST_CASE_METHOD(LVGLTestFixture, "repeat: 0->N->0 cycles leave a consistent tree",
                 "[xml][repeat][slow]") {
    REQUIRE(lv_xml_register_component_from_data("t_cyc", COMP_CNT) == LV_RESULT_OK);
    lv_xml_component_scope_t * scope = lv_xml_component_get_scope("t_cyc");
    lv_obj_t * v = (lv_obj_t *)lv_xml_create(lv_screen_active(), "t_cyc", nullptr);
    lv_obj_t * root = lv_obj_find_by_name(v, "root");
    lv_subject_t * n = lv_xml_get_subject(scope, "n");
    REQUIRE(root != nullptr);

    for (int cycle = 0; cycle < 3; cycle++) {
        lv_subject_set_int(n, 0);
        process_lvgl(40);
        REQUIRE(lv_obj_get_child_count(root) == 0);

        lv_subject_set_int(n, 4);
        process_lvgl(40);
        REQUIRE(lv_obj_get_child_count(root) == 4);
    }

    lv_subject_set_int(n, 0);
    process_lvgl(40);
    REQUIRE(lv_obj_get_child_count(root) == 0);

    lv_obj_delete(v);
    lv_xml_component_unregister("t_cyc");
}

namespace {
// static storage so the count subject outlives the component that observes it
lv_subject_t g_repeat_global_count;
const char * COMP_GLOBAL_CNT =
  "<component>"
  "  <view>"
  "    <lv_obj name='root'>"
  "      <repeat count='g_rep_cnt'><lv_obj name='item'/></repeat>"
  "    </lv_obj>"
  "  </view>"
  "</component>";
} // namespace

TEST_CASE_METHOD(LVGLTestFixture, "repeat: GLOBAL count observer removed on unregister (no UAF)",
                 "[xml][repeat][slow]") {
    // The count subject lives in the GLOBAL scope, outliving the component. Only the
    // repeat_ll teardown removes the observer sitting on it -- if it didn't, mutating
    // the global after unregister would fire the rebuild callback on a freed record
    // (ASAN use-after-free). Mirrors test_xml_subject_expr.cpp's global-input test.
    lv_subject_init_int(&g_repeat_global_count, 3);
    lv_xml_register_subject(nullptr, "g_rep_cnt", &g_repeat_global_count);  // global scope

    REQUIRE(lv_xml_register_component_from_data("t_gcnt", COMP_GLOBAL_CNT) == LV_RESULT_OK);
    lv_obj_t * v = (lv_obj_t *)lv_xml_create(lv_screen_active(), "t_gcnt", nullptr);
    lv_obj_t * root = lv_obj_find_by_name(v, "root");
    REQUIRE(root != nullptr);
    REQUIRE(lv_obj_get_child_count(root) == 3);

    // Observer is live: changing the global rebuilds the expansion.
    lv_subject_set_int(&g_repeat_global_count, 6);
    process_lvgl(50);
    REQUIRE(lv_obj_get_child_count(root) == 6);

    // Mandatory teardown order: delete the instance FIRST (frees the roots), THEN
    // unregister (detaches the count observer before the record is freed).
    lv_obj_delete(v);
    process_lvgl(20);                       // let the instance-delete settle
    REQUIRE(lv_xml_component_unregister("t_gcnt") == LV_RESULT_OK);

    // Mutating the global now must NOT reach the freed record.
    lv_subject_set_int(&g_repeat_global_count, 99);
    process_lvgl(20);
    SUCCEED("global-count repeat observer cleanly removed; no UAF on post-unregister change");
}
