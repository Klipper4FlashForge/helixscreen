// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#include "../lvgl_test_fixture.h"
#include "../catch_amalgamated.hpp"
extern "C" {
#include "helix-xml/src/xml/lv_xml_expr.h"
}

namespace {
lv_subject_t ra, rb;
lv_subject_t* res(void*, const char* n) {
    if (!strcmp(n, "a")) return &ra;
    if (!strcmp(n, "b")) return &rb;
    return nullptr;
}
int last = -999, calls = 0;
void cb(void*, int32_t v) {
    last = v;
    calls++;
}
} // namespace

TEST_CASE_METHOD(LVGLTestFixture, "reactive: recomputes on each distinct input", "[xml_expr][reactive]") {
    lv_subject_init_int(&ra, 0);
    lv_subject_init_int(&rb, 0);
    last = -999;
    calls = 0;
    lv_obj_t* owner = lv_obj_create(lv_screen_active());
    lv_xml_expr_t* e = lv_xml_expr_compile("a && b > 3", res, nullptr);
    REQUIRE(e != nullptr);
    REQUIRE(lv_xml_expr_subject_count(e) == 2);
    lv_xml_expr_bind(e, owner, cb, nullptr);
    REQUIRE(last == 0);  // initial eval fired
    REQUIRE(calls == 1); // fired EXACTLY once at bind, not once per distinct subject
    lv_subject_set_int(&ra, 1); // change A
    REQUIRE(last == 0); // b still <= 3
    lv_subject_set_int(&rb, 5); // change B
    REQUIRE(last == 1); // now true
    int before = calls;
    lv_obj_delete(owner); // must free expr + detach observers
    lv_subject_set_int(&ra, 0); // perturb BOTH subjects post-delete: no observers left
    lv_subject_set_int(&rb, 9);
    REQUIRE(calls == before);
}

TEST_CASE_METHOD(LVGLTestFixture, "reactive: repeated subject registers once, no double free", "[xml_expr][reactive]") {
    lv_subject_init_int(&ra, 2);
    last = -999;
    calls = 0;
    lv_obj_t* owner = lv_obj_create(lv_screen_active());
    lv_xml_expr_t* e = lv_xml_expr_compile("a + a", res, nullptr); // a referenced twice
    REQUIRE(lv_xml_expr_subject_count(e) == 1); // distinct collapse
    lv_xml_expr_bind(e, owner, cb, nullptr);
    REQUIRE(last == 4);
    REQUIRE(calls == 1); // fired exactly once at bind
    lv_obj_delete(owner); // ASAN: no double-free
}
