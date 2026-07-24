// SPDX-License-Identifier: GPL-3.0-or-later
#include "../catch_amalgamated.hpp"
#include "helix-xml/src/xml/lv_xml.h"
#include "helix_test_fixture.h"   // pulls LVGL init

TEST_CASE_METHOD(HelixTestFixture, "lv_xml_unregister_subject removes a global name", "[xml][subject][unregister]") {
    lv_subject_t s;
    lv_subject_init_int(&s, 7);
    REQUIRE(lv_xml_register_subject(nullptr, "unreg_test_a", &s) == LV_RESULT_OK);
    REQUIRE(lv_xml_get_subject(nullptr, "unreg_test_a") == &s);

    REQUIRE(lv_xml_unregister_subject(nullptr, "unreg_test_a") == LV_RESULT_OK);
    REQUIRE(lv_xml_get_subject(nullptr, "unreg_test_a") == nullptr);   // gone from registry

    // Non-owning: the subject is still usable (not freed/deinited by unregister)
    REQUIRE(lv_subject_get_int(&s) == 7);
    lv_subject_deinit(&s);   // caller owns teardown
}

TEST_CASE_METHOD(HelixTestFixture, "lv_xml_unregister_subject on absent name is invalid", "[xml][subject][unregister]") {
    REQUIRE(lv_xml_unregister_subject(nullptr, "no_such_name_zzz") == LV_RESULT_INVALID);
}
