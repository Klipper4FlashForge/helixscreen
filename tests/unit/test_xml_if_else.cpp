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

/* --- Reactive + crash-class (Task 4) --- */

static const char * COMP_REACTIVE =
  "<component><subjects><subject name='c' type='int' value='1'/></subjects>"
  "  <view><lv_obj name='root'>"
  "    <if cond='c gt 0'><lv_obj name='t'/><else/><lv_obj name='f'/></if>"
  "  </lv_obj></view></component>";

TEST_CASE_METHOD(LVGLTestFixture, "if: reactive flip true->false->true", "[xml][if]") {
    REQUIRE(lv_xml_register_component_from_data("if_rx", COMP_REACTIVE) == LV_RESULT_OK);
    lv_xml_component_scope_t * scope = lv_xml_component_get_scope("if_rx");
    lv_obj_t * v = (lv_obj_t *)lv_xml_create(lv_screen_active(), "if_rx", nullptr);
    lv_subject_t * c = lv_xml_get_subject(scope, "c");
    REQUIRE(lv_obj_find_by_name(v, "t") != nullptr);
    REQUIRE(lv_obj_find_by_name(v, "f") == nullptr);

    lv_subject_set_int(c, 0); process_lvgl(50);
    REQUIRE(lv_obj_find_by_name(v, "t") == nullptr);
    REQUIRE(lv_obj_find_by_name(v, "f") != nullptr);

    lv_subject_set_int(c, 1); process_lvgl(50);
    REQUIRE(lv_obj_find_by_name(v, "t") != nullptr);
    REQUIRE(lv_obj_find_by_name(v, "f") == nullptr);

    lv_obj_delete(v);
    lv_xml_component_unregister("if_rx");
}

static const char * COMP_REACTIVE_MULTI =
  "<component><subjects>"
  "  <subject name='a' type='int' value='1'/>"
  "  <subject name='b' type='int' value='1'/>"
  "  <subject name='c' type='int' value='0'/>"
  "</subjects>"
  "  <view><lv_obj name='root'>"
  "    <if cond='a and b gt c'><lv_obj name='t'/><else/><lv_obj name='f'/></if>"
  "  </lv_obj></view></component>";

TEST_CASE_METHOD(LVGLTestFixture, "if: multi-subject cond re-evaluates on any operand", "[xml][if]") {
    REQUIRE(lv_xml_register_component_from_data("if_multi", COMP_REACTIVE_MULTI) == LV_RESULT_OK);
    lv_xml_component_scope_t * scope = lv_xml_component_get_scope("if_multi");
    lv_obj_t * v = (lv_obj_t *)lv_xml_create(lv_screen_active(), "if_multi", nullptr);
    lv_subject_t * a = lv_xml_get_subject(scope, "a");
    lv_subject_t * b = lv_xml_get_subject(scope, "b");
    lv_subject_t * c = lv_xml_get_subject(scope, "c");
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    REQUIRE(c != nullptr);

    // a=1, b=1, c=0 -> a(1) and (b gt c)(1) -> true
    REQUIRE(lv_obj_find_by_name(v, "t") != nullptr);
    REQUIRE(lv_obj_find_by_name(v, "f") == nullptr);

    // Flip `a` to 0 -> false, regardless of b/c.
    lv_subject_set_int(a, 0); process_lvgl(50);
    REQUIRE(lv_obj_find_by_name(v, "t") == nullptr);
    REQUIRE(lv_obj_find_by_name(v, "f") != nullptr);

    // Restore `a`, then flip `b` below `c` -> false via the second operand.
    lv_subject_set_int(a, 1); process_lvgl(50);
    REQUIRE(lv_obj_find_by_name(v, "t") != nullptr);

    lv_subject_set_int(c, 5); process_lvgl(50);   // b(1) gt c(5) -> false
    REQUIRE(lv_obj_find_by_name(v, "t") == nullptr);
    REQUIRE(lv_obj_find_by_name(v, "f") != nullptr);

    // Flip `c` back down -> true again, proving the THIRD subject alone re-evaluates.
    lv_subject_set_int(c, 0); process_lvgl(50);
    REQUIRE(lv_obj_find_by_name(v, "t") != nullptr);
    REQUIRE(lv_obj_find_by_name(v, "f") == nullptr);

    lv_obj_delete(v);
    lv_xml_component_unregister("if_multi");
}

TEST_CASE_METHOD(LVGLTestFixture, "if: rapid churn coalesces", "[xml][if]") {
    REQUIRE(lv_xml_register_component_from_data("if_churn", COMP_REACTIVE) == LV_RESULT_OK);
    lv_xml_component_scope_t * scope = lv_xml_component_get_scope("if_churn");
    lv_obj_t * v = (lv_obj_t *)lv_xml_create(lv_screen_active(), "if_churn", nullptr);
    lv_subject_t * c = lv_xml_get_subject(scope, "c");
    REQUIRE(lv_obj_find_by_name(v, "t") != nullptr);

    // Three flips before a single drain; each rebuild reads the current value, so
    // the live tree already reflects the final state (0 -> false) before draining.
    lv_subject_set_int(c, 0);
    lv_subject_set_int(c, 1);
    lv_subject_set_int(c, 0);
    REQUIRE(lv_obj_find_by_name(v, "t") == nullptr);
    REQUIRE(lv_obj_find_by_name(v, "f") != nullptr);
    process_lvgl(80);   // drain all pending async teardowns; ASAN-clean
    REQUIRE(lv_obj_find_by_name(v, "t") == nullptr);
    REQUIRE(lv_obj_find_by_name(v, "f") != nullptr);

    lv_obj_delete(v);
    lv_xml_component_unregister("if_churn");
}

namespace {
// static storage so the cond subject outlives the component that observes it
lv_subject_t g_if_uaf_cond;
const char * COMP_IF_UAF =
  "<component>"
  "  <view><lv_obj name='root'>"
  "    <if cond='g_if_uaf gt 0'><lv_obj name='t'/><else/><lv_obj name='f'/></if>"
  "  </lv_obj></view></component>";
} // namespace

TEST_CASE_METHOD(LVGLTestFixture,
                 "if: cond change AFTER instance delete does not UAF", "[xml][if]") {
    // The cond subject is GLOBAL and outlives the instance; the component stays
    // REGISTERED. Deleting the instance fires xml_frag_instance_delete_cb, which
    // (via xml_frag_record_free_heap) drops r->bind WITHOUT unbinding, because
    // lv_xml_expr_bind's own expr_bind_delete_cb -- also hooked on this same
    // view_root's LV_EVENT_DELETE -- detaches the cond observers and frees the
    // bind itself. If that hook didn't fire (or fired twice), the mutation below
    // would either UAF a freed observer or double-free the bind context.
    lv_subject_init_int(&g_if_uaf_cond, 1);
    lv_xml_register_subject(nullptr, "g_if_uaf", &g_if_uaf_cond);   // global scope

    REQUIRE(lv_xml_register_component_from_data("if_uaf", COMP_IF_UAF) == LV_RESULT_OK);
    lv_obj_t * v = (lv_obj_t *)lv_xml_create(lv_screen_active(), "if_uaf", nullptr);
    REQUIRE(lv_obj_find_by_name(v, "t") != nullptr);

    lv_obj_delete(v);        // instance gone; expr_bind_delete_cb fires, frees the bind
    process_lvgl(30);

    // Dangerous window: the shared subject changes while no instance is alive.
    lv_subject_set_int(&g_if_uaf_cond, 0);
    process_lvgl(30);
    SUCCEED("cond change after instance delete did not reach a dangling observer");

    lv_xml_component_unregister("if_uaf");
}

namespace {
lv_subject_t g_if_global_cond;
const char * COMP_IF_GLOBAL =
  "<component>"
  "  <view><lv_obj name='root'>"
  "    <if cond='g_if_global gt 0'><lv_obj name='t'/><else/><lv_obj name='f'/></if>"
  "  </lv_obj></view></component>";
} // namespace

TEST_CASE_METHOD(LVGLTestFixture,
                 "if: GLOBAL cond subject removed on unregister (no UAF)", "[xml][if]") {
    lv_subject_init_int(&g_if_global_cond, 1);
    lv_xml_register_subject(nullptr, "g_if_global", &g_if_global_cond);

    REQUIRE(lv_xml_register_component_from_data("if_global", COMP_IF_GLOBAL) == LV_RESULT_OK);
    lv_obj_t * v = (lv_obj_t *)lv_xml_create(lv_screen_active(), "if_global", nullptr);
    REQUIRE(lv_obj_find_by_name(v, "t") != nullptr);

    // Mandatory teardown order: delete the instance FIRST, THEN unregister.
    lv_obj_delete(v);
    process_lvgl(20);
    REQUIRE(lv_xml_component_unregister("if_global") == LV_RESULT_OK);

    // Mutating the global now must NOT reach any freed record/bind.
    lv_subject_set_int(&g_if_global_cond, 0);
    process_lvgl(20);
    SUCCEED("global-cond if observer cleanly removed; no UAF on post-unregister change");
}

namespace {
lv_subject_t g_if_sweep_cond;
const char * COMP_IF_SWEEP =
  "<component>"
  "  <view><lv_obj name='root'>"
  "    <if cond='g_if_sweep gt 0'><lv_obj name='t'/><else/><lv_obj name='f'/></if>"
  "  </lv_obj></view></component>";
} // namespace

TEST_CASE_METHOD(LVGLTestFixture,
                 "if: unregister WHILE instance alive detaches cond observers (no UAF)",
                 "[xml][if]") {
    // The strengthened test: unregister with the instance STILL ALIVE. This is the
    // path lv_xml_frag_record_free (walked from frag_ll, BEFORE subjects_ll teardown)
    // must handle by calling lv_xml_expr_unbind on r->bind -- the instance-delete
    // path's expr_bind_delete_cb has NOT fired yet, so without an explicit _unbind
    // here the cond observers would still sit on g_if_sweep_cond when it changes
    // below (UAF), and the bind's delete-cb hook would still be armed on `v`'s
    // LV_EVENT_DELETE, double-freeing the bind when `v` is deleted afterward.
    lv_subject_init_int(&g_if_sweep_cond, 1);
    lv_xml_register_subject(nullptr, "g_if_sweep", &g_if_sweep_cond);

    REQUIRE(lv_xml_register_component_from_data("if_sweep", COMP_IF_SWEEP) == LV_RESULT_OK);
    lv_obj_t * v = (lv_obj_t *)lv_xml_create(lv_screen_active(), "if_sweep", nullptr);
    REQUIRE(lv_obj_find_by_name(v, "t") != nullptr);

    // Unregister WITHOUT deleting `v` first -- exercises the frag_ll sweep's _unbind.
    REQUIRE(lv_xml_component_unregister("if_sweep") == LV_RESULT_OK);

    // The observer must be detached: this mutation must not reach a freed record.
    lv_subject_set_int(&g_if_sweep_cond, 0);
    process_lvgl(30);
    SUCCEED("unregister-while-alive detached the cond observer; no UAF on global change");

    // The bind's delete-cb hook must also be gone from `v` -- deleting it now must
    // not double-free the (already-unbound) bind context.
    lv_obj_delete(v);
    process_lvgl(30);
    SUCCEED("instance delete after unregister-while-alive did not double-free the bind");
}

namespace {
lv_subject_t g_if_reinst_cond;
const char * COMP_IF_REINST =
  "<component>"
  "  <view><lv_obj name='root'>"
  "    <if cond='g_if_reinst gt 0'><lv_obj name='t'/><else/><lv_obj name='f'/></if>"
  "  </lv_obj></view></component>";
} // namespace

TEST_CASE_METHOD(LVGLTestFixture,
                 "if: re-instantiation does not accumulate stale observers", "[xml][if]") {
    lv_subject_init_int(&g_if_reinst_cond, 1);
    lv_xml_register_subject(nullptr, "g_if_reinst", &g_if_reinst_cond);

    REQUIRE(lv_xml_register_component_from_data("if_reinst", COMP_IF_REINST) == LV_RESULT_OK);

    lv_obj_t * v1 = (lv_obj_t *)lv_xml_create(lv_screen_active(), "if_reinst", nullptr);
    REQUIRE(lv_obj_find_by_name(v1, "t") != nullptr);
    lv_obj_delete(v1);
    process_lvgl(30);

    lv_obj_t * v2 = (lv_obj_t *)lv_xml_create(lv_screen_active(), "if_reinst", nullptr);
    REQUIRE(lv_obj_find_by_name(v2, "t") != nullptr);

    // Rebuild must touch ONLY the live 2nd instance; v1's bind must be gone (else
    // its teardown fires on freed roots -> UAF under ASAN).
    lv_subject_set_int(&g_if_reinst_cond, 0);
    process_lvgl(30);
    REQUIRE(lv_obj_find_by_name(v2, "t") == nullptr);
    REQUIRE(lv_obj_find_by_name(v2, "f") != nullptr);

    lv_obj_delete(v2);
    process_lvgl(30);
    lv_xml_component_unregister("if_reinst");
}
