// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#include "../lvgl_test_fixture.h"
#include "../catch_amalgamated.hpp"
extern "C" {
#include "helix-xml/src/xml/lv_xml.h"
}

namespace {
const char* COMP =
    "<component>"
    "  <subjects>"
    "    <subject name='p_active' type='int' value='0'/>"
    "    <subject name='p_prog'   type='int' value='0'/>"
    "    <subject_expr name='show_card' expr='p_active and p_prog gt 0'/>"
    "  </subjects>"
    "  <view><lv_obj/></view>"
    "</component>";
} // namespace

TEST_CASE_METHOD(LVGLTestFixture, "subject_expr: derived subject recomputes reactively",
                 "[xml_expr][subject_expr]") {
    REQUIRE(lv_xml_register_component_from_data("t_expr", COMP) == LV_RESULT_OK);

    // <subject_expr> (like <subject>) registers into the DECLARING component's
    // own scope, not the global namespace -- mirrors test_scoped_subject_registry.cpp.
    lv_xml_component_scope_t* scope = lv_xml_component_get_scope("t_expr");
    REQUIRE(scope != nullptr);

    lv_obj_t* v = (lv_obj_t*)lv_xml_create(lv_screen_active(), "t_expr", nullptr);
    REQUIRE(v != nullptr);

    lv_subject_t* active = lv_xml_get_subject(scope, "p_active");
    lv_subject_t* prog = lv_xml_get_subject(scope, "p_prog");
    lv_subject_t* show = lv_xml_get_subject(scope, "show_card");
    REQUIRE(active != nullptr);
    REQUIRE(prog != nullptr);
    REQUIRE(show != nullptr);

    REQUIRE(lv_subject_get_int(show) == 0);

    lv_subject_set_int(active, 1);
    REQUIRE(lv_subject_get_int(show) == 0); // prog still 0

    lv_subject_set_int(prog, 40);
    REQUIRE(lv_subject_get_int(show) == 1); // both conditions met

    lv_subject_set_int(active, 0);
    REQUIRE(lv_subject_get_int(show) == 0); // active dropped -> recomputes back to false

    // Unregister cleanly: exercises the subject_expr_ll teardown path (frees
    // the compiled expr + shared observer ctx; the derived subject itself is
    // freed by the pre-existing subjects_ll cleanup).
    REQUIRE(lv_xml_component_unregister("t_expr") == LV_RESULT_OK);
}

TEST_CASE_METHOD(LVGLTestFixture, "subject_expr: missing name or expr is a no-op warning",
                 "[xml_expr][subject_expr]") {
    const char* comp_no_expr =
        "<component>"
        "  <subjects>"
        "    <subject name='p_a' type='int' value='0'/>"
        "    <subject_expr name='derived'/>"
        "  </subjects>"
        "  <view><lv_obj/></view>"
        "</component>";
    REQUIRE(lv_xml_register_component_from_data("t_expr_noexpr", comp_no_expr) == LV_RESULT_OK);
    lv_xml_component_scope_t* scope = lv_xml_component_get_scope("t_expr_noexpr");
    REQUIRE(scope != nullptr);
    REQUIRE(lv_xml_get_subject(scope, "derived") == nullptr);
    REQUIRE(lv_xml_component_unregister("t_expr_noexpr") == LV_RESULT_OK);

    const char* comp_no_name =
        "<component>"
        "  <subjects>"
        "    <subject name='p_a' type='int' value='0'/>"
        "    <subject_expr expr='p_a'/>"
        "  </subjects>"
        "  <view><lv_obj/></view>"
        "</component>";
    REQUIRE(lv_xml_register_component_from_data("t_expr_noname", comp_no_name) == LV_RESULT_OK);
    REQUIRE(lv_xml_component_unregister("t_expr_noname") == LV_RESULT_OK);
}

namespace {
lv_subject_t g_expr_global_input;  // static storage: outlives the component that observes it
}

TEST_CASE_METHOD(LVGLTestFixture,
                 "subject_expr: GLOBAL input observer is removed on unregister (no UAF)",
                 "[xml_expr][subject_expr]") {
    // A subject_expr whose input lives in the GLOBAL scope (not the component's own
    // subjects_ll). Only the subject_expr_ll teardown removes that observer -- if it
    // didn't, mutating the global after unregister would fire a dangling observer on
    // freed ctx (ASAN use-after-free). This is the retained-observer removal's reason
    // for existing; the same-scope tests don't cover it.
    lv_subject_init_int(&g_expr_global_input, 0);
    lv_xml_register_subject(nullptr, "g_expr_in", &g_expr_global_input);  // global scope

    const char* comp =
        "<component>"
        "  <subjects>"
        "    <subject_expr name='g_derived' expr='g_expr_in gt 5'/>"
        "  </subjects>"
        "  <view><lv_obj/></view>"
        "</component>";
    REQUIRE(lv_xml_register_component_from_data("t_gexpr", comp) == LV_RESULT_OK);
    lv_obj_t* v = (lv_obj_t*)lv_xml_create(lv_screen_active(), "t_gexpr", nullptr);
    REQUIRE(v != nullptr);

    lv_subject_t* d = lv_xml_get_subject(lv_xml_component_get_scope("t_gexpr"), "g_derived");
    REQUIRE(d != nullptr);

    // Observer is live: changing the global recomputes the derived subject.
    lv_subject_set_int(&g_expr_global_input, 10);
    REQUIRE(lv_subject_get_int(d) == 1);
    lv_subject_set_int(&g_expr_global_input, 2);
    REQUIRE(lv_subject_get_int(d) == 0);

    // Unregister the component. subject_expr_ll must lv_observer_remove() the observer
    // sitting on the still-live GLOBAL subject before freeing its ctx.
    REQUIRE(lv_xml_component_unregister("t_gexpr") == LV_RESULT_OK);

    // Mutating the global now must NOT reach a freed ctx.
    lv_subject_set_int(&g_expr_global_input, 99);
    SUCCEED("global-input subject_expr observer cleanly removed; no UAF on post-unregister change");
}

TEST_CASE_METHOD(LVGLTestFixture, "subject_expr: bad expr does not register a broken subject",
                 "[xml_expr][subject_expr]") {
    const char* comp_bad_expr =
        "<component>"
        "  <subjects>"
        "    <subject name='p_a' type='int' value='0'/>"
        "    <subject_expr name='derived' expr='p_a +'/>"
        "  </subjects>"
        "  <view><lv_obj/></view>"
        "</component>";
    REQUIRE(lv_xml_register_component_from_data("t_expr_bad", comp_bad_expr) == LV_RESULT_OK);
    lv_xml_component_scope_t* scope = lv_xml_component_get_scope("t_expr_bad");
    REQUIRE(scope != nullptr);
    REQUIRE(lv_xml_get_subject(scope, "derived") == nullptr);
    REQUIRE(lv_xml_component_unregister("t_expr_bad") == LV_RESULT_OK);
}
