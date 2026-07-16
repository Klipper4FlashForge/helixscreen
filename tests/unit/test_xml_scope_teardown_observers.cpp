// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Regression for the systemic dangling-observer UAF on component-scope teardown.
// lv_xml_component_unregister() frees each scope subject; widgets created from the
// component keep observers on those subjects (via bind_text / bind_flag_if_* /
// bind_style_* / cond= / subject_expr). If unregister frees a subject WITHOUT first
// removing its observers (lv_subject_deinit), a widget deleted afterwards -- e.g.
// NavigationManager::rebuild_active_views() during a HELIX_HOT_RELOAD re-register,
// which deletes old widgets AFTER the component is unregistered -- calls
// lv_observer_remove() on a freed subject -> use-after-free.
//
// This test drives that exact order (create instance -> unregister -> delete
// instance) for a PRE-EXISTING bind (bind_flag_if_eq), proving the fix protects
// every bind_* tag, not just the new expression tags. Full UAF detection is under
// the ASAN sweep; in a plain build this still exercises the path and asserts the
// bind is live before teardown.
#include "../lvgl_test_fixture.h"
#include "../catch_amalgamated.hpp"
extern "C" {
#include "helix-xml/src/xml/lv_xml.h"
}

TEST_CASE_METHOD(LVGLTestFixture,
                 "scope teardown: unregister detaches observers so a later widget delete is safe",
                 "[xml_expr][teardown]") {
    const char* comp =
        "<component>"
        "  <subjects>"
        "    <subject name='s' type='int' value='0'/>"
        "  </subjects>"
        "  <view>"
        "    <lv_obj name='box'>"
        "      <bind_flag_if_eq subject='s' flag='hidden' ref_value='1'/>"
        "    </lv_obj>"
        "  </view>"
        "</component>";
    REQUIRE(lv_xml_register_component_from_data("t_teardown", comp) == LV_RESULT_OK);

    lv_obj_t* inst = (lv_obj_t*)lv_xml_create(lv_screen_active(), "t_teardown", nullptr);
    REQUIRE(inst != nullptr);
    lv_obj_t* box = lv_obj_find_by_name(inst, "box");
    REQUIRE(box != nullptr);

    lv_subject_t* s = lv_xml_get_subject(lv_xml_component_get_scope("t_teardown"), "s");
    REQUIRE(s != nullptr);

    // The observer is live: driving the subject toggles the widget's flag.
    lv_subject_set_int(s, 1);
    REQUIRE(lv_obj_has_flag(box, LV_OBJ_FLAG_HIDDEN));
    lv_subject_set_int(s, 0);
    REQUIRE_FALSE(lv_obj_has_flag(box, LV_OBJ_FLAG_HIDDEN));

    // Tear down the scope (frees subject 's'). With the fix, lv_subject_deinit()
    // removes the still-attached observer first; without it, 's' is freed with the
    // observer dangling.
    REQUIRE(lv_xml_component_unregister("t_teardown") == LV_RESULT_OK);
    REQUIRE(lv_xml_component_get_scope("t_teardown") == nullptr);

    // Deleting the instance now must NOT reach into the freed subject. Pre-fix this
    // is a lv_observer_remove() on freed memory (UAF); post-fix the observer is
    // already gone, so this is a clean delete.
    lv_obj_delete(inst);
    SUCCEED("instance deleted after scope teardown without touching a freed subject");
}
