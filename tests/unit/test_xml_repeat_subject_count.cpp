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
                 "[xml][repeat]") {
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
                 "[xml][repeat]") {
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
                 "[xml][repeat]") {
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
                 "[xml][repeat]") {
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

namespace {
lv_subject_t g_repeat_uaf_count;
const char * COMP_UAF_CNT =
  "<component>"
  "  <view>"
  "    <lv_obj name='root'>"
  "      <repeat count='g_rep_uaf'><lv_obj name='item'/></repeat>"
  "    </lv_obj>"
  "  </view>"
  "</component>";
} // namespace

TEST_CASE_METHOD(LVGLTestFixture,
                 "repeat: count change AFTER instance delete does not UAF (observer tied to instance)",
                 "[xml][repeat]") {
    // The count subject is GLOBAL and outlives the instance; the component stays
    // REGISTERED. When the instance is deleted its roots[] are freed with the tree.
    // If the count observer were tied only to the component scope (not the instance),
    // it would stay live with dangling roots -- and the mutation below would fire the
    // rebuild callback -> teardown -> lv_obj_set_layout/lv_obj_set_parent on freed
    // widgets (teardown never validates, L076) -> use-after-free under ASAN. This is
    // the window BETWEEN instance-delete and unregister, which is unbounded in
    // production (nav away, later a printer-state subject changes).
    lv_subject_init_int(&g_repeat_uaf_count, 3);
    lv_xml_register_subject(nullptr, "g_rep_uaf", &g_repeat_uaf_count);

    REQUIRE(lv_xml_register_component_from_data("t_uaf", COMP_UAF_CNT) == LV_RESULT_OK);
    lv_obj_t * v = (lv_obj_t *)lv_xml_create(lv_screen_active(), "t_uaf", nullptr);
    lv_obj_t * root = lv_obj_find_by_name(v, "root");
    REQUIRE(root != nullptr);
    REQUIRE(lv_obj_get_child_count(root) == 3);

    lv_obj_delete(v);        // instance gone; roots[] freed; component still registered
    process_lvgl(30);

    // Dangerous window: the shared subject changes while no instance is alive.
    lv_subject_set_int(&g_repeat_uaf_count, 7);
    process_lvgl(30);
    SUCCEED("count change after instance delete did not reach a dangling observer");

    lv_xml_component_unregister("t_uaf");
}

namespace {
lv_subject_t g_repeat_reinst_count;
const char * COMP_REINST_CNT =
  "<component>"
  "  <view>"
  "    <lv_obj name='root'>"
  "      <repeat count='g_rep_reinst'><lv_obj name='item'/></repeat>"
  "    </lv_obj>"
  "  </view>"
  "</component>";
} // namespace

TEST_CASE_METHOD(LVGLTestFixture,
                 "repeat: re-instantiation does not accumulate stale observers",
                 "[xml][repeat]") {
    // view_def is re-parsed on every lv_xml_create, so each instantiation appends a
    // fresh record+observer. If the 1st instance's record isn't reclaimed on its
    // delete, a later count change fires teardown on BOTH records -- the dead one on
    // freed roots. Proves per-instance reclamation + no repeat_ll accumulation.
    lv_subject_init_int(&g_repeat_reinst_count, 2);
    lv_xml_register_subject(nullptr, "g_rep_reinst", &g_repeat_reinst_count);

    REQUIRE(lv_xml_register_component_from_data("t_reinst", COMP_REINST_CNT) == LV_RESULT_OK);

    lv_obj_t * v1 = (lv_obj_t *)lv_xml_create(lv_screen_active(), "t_reinst", nullptr);
    REQUIRE(lv_obj_get_child_count(lv_obj_find_by_name(v1, "root")) == 2);
    lv_obj_delete(v1);
    process_lvgl(30);

    lv_obj_t * v2 = (lv_obj_t *)lv_xml_create(lv_screen_active(), "t_reinst", nullptr);
    lv_obj_t * root2 = lv_obj_find_by_name(v2, "root");
    REQUIRE(root2 != nullptr);
    REQUIRE(lv_obj_get_child_count(root2) == 2);

    // Rebuild must touch ONLY the live 2nd instance; the 1st instance's observer must
    // be gone (else its teardown fires on freed roots -> UAF under ASAN).
    lv_subject_set_int(&g_repeat_reinst_count, 5);
    process_lvgl(30);
    REQUIRE(lv_obj_get_child_count(root2) == 5);

    lv_obj_delete(v2);
    process_lvgl(30);
    lv_xml_component_unregister("t_reinst");
}
