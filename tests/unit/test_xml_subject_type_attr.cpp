// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Regression: `<subject name="x" type="int" value="0"/>` (the attribute form,
// used by ui_xml/hidden_network_modal.xml) must create a working typed subject.
// Before the type= fix, process_subject_element received the tag name ("subject")
// as the type, matched no branch, and silently left the subject at
// LV_SUBJECT_TYPE_INVALID -- so every bind against it stuck at its default and
// lv_subject_set_* was a no-op. The tag-per-type form (<int>/<string>/<color>)
// carries no type= attribute and is unaffected.
#include <string>

#include "../lvgl_test_fixture.h"
#include "../catch_amalgamated.hpp"
extern "C" {
#include "helix-xml/src/xml/lv_xml.h"
}

TEST_CASE_METHOD(LVGLTestFixture, "subject: type= attribute yields a working typed subject",
                 "[xml_subject][type_attr]") {
    const char* comp =
        "<component>"
        "  <subjects>"
        "    <subject name='s_int' type='int'    value='7'/>"
        "    <subject name='s_str' type='string' value='hi'/>"
        "  </subjects>"
        "  <view><lv_obj/></view>"
        "</component>";
    REQUIRE(lv_xml_register_component_from_data("t_typeattr", comp) == LV_RESULT_OK);

    lv_xml_component_scope_t* scope = lv_xml_component_get_scope("t_typeattr");
    REQUIRE(scope != nullptr);

    lv_subject_t* si = lv_xml_get_subject(scope, "s_int");
    lv_subject_t* ss = lv_xml_get_subject(scope, "s_str");
    REQUIRE(si != nullptr);
    REQUIRE(ss != nullptr);

    // The type= attribute took effect (NOT LV_SUBJECT_TYPE_INVALID).
    REQUIRE(si->type == LV_SUBJECT_TYPE_INT);
    REQUIRE(ss->type == LV_SUBJECT_TYPE_STRING);

    // Initial values were applied, and the subject actually reacts (an INVALID
    // subject would silently no-op both of these).
    REQUIRE(lv_subject_get_int(si) == 7);
    REQUIRE(std::string(lv_subject_get_string(ss)) == "hi");
    lv_subject_set_int(si, 42);
    REQUIRE(lv_subject_get_int(si) == 42);

    REQUIRE(lv_xml_component_unregister("t_typeattr") == LV_RESULT_OK);
}
