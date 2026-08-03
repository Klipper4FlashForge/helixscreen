// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Provenance of the records in a component scope's `subjects_ll`.
//
// A scope gets subjects from two completely different places:
//
//   owned    — the XML parser allocated the lv_subject_t for a `<subject>` /
//              `<subject_expr>` element. The scope must deinit and free it at
//              teardown or it leaks.
//   borrowed — C++ called lv_xml_register_subject() with a pointer to storage it
//              owns (a `static inline lv_subject_t` member, a manager-owned
//              field, …). The scope holds the pointer only.
//
// lv_xml_component_unregister() used to free every record unconditionally, so
// tearing a scope down freed C++ statics. With LV_USE_STDLIB_MALLOC =
// LV_STDLIB_CLIB that is libc free() on a __DATA address: "malloc: pointer being
// freed was not allocated" -> SIGABRT. Real trigger: HELIX_HOT_RELOAD saving
// ui_xml/runout_guidance_modal.xml, whose scope borrows
// RunoutGuidanceModal::resume_blocked_subject_ / autofeed_capable_subject_.
//
// The three failure modes these tests pin, in the order a naive fix hits them:
//   1. free a borrowed subject               -> heap abort
//   2. deinit a borrowed subject             -> observers ripped off widgets in
//                                               OTHER live components
//   3. stop freeing owned subjects too       -> leak
// plus the hot-reload consequence: a borrowed subject must still be resolvable in
// the scope after the unregister/re-register cycle, or every bind_* naming it
// silently resolves to nothing and the component comes back live but inert.

#include "../lvgl_test_fixture.h"
#include "xml_hot_reloader.h"

#include "../catch_amalgamated.hpp"

extern "C" {
#include "helix-xml/src/xml/lv_xml.h"
}

namespace {

/// Counts how many times an observer fired, so a test can prove the observer is
/// still attached (and therefore that the subject was not deinit'd).
struct ObserverProbe {
    int fired = 0;
    int last_value = 0;
};

void probe_cb(lv_observer_t* observer, lv_subject_t* subject) {
    auto* probe = static_cast<ObserverProbe*>(lv_observer_get_user_data(observer));
    probe->fired++;
    probe->last_value = lv_subject_get_int(subject);
}

} // namespace

// ============================================================================
// 1. A borrowed subject survives its borrowing scope's teardown
// ============================================================================

TEST_CASE_METHOD(LVGLTestFixture,
                 "borrowed subject survives lv_xml_component_unregister of the scope that "
                 "registered it",
                 "[xml][hotreload]") {
    // No <subject> in the XML: every record in this scope is borrowed.
    const char* comp = "<component>"
                       "  <view>"
                       "    <lv_obj name='box'/>"
                       "  </view>"
                       "</component>";
    REQUIRE(lv_xml_register_component_from_data("t_borrow_survive", comp) == LV_RESULT_OK);

    lv_xml_component_scope_t* scope = lv_xml_component_get_scope("t_borrow_survive");
    REQUIRE(scope != nullptr);

    // Stands in for a `static inline lv_subject_t` member like
    // RunoutGuidanceModal::resume_blocked_subject_ — storage this translation
    // unit owns, handed to the scope by pointer.
    lv_subject_t borrowed;
    lv_subject_init_int(&borrowed, 41);
    REQUIRE(lv_xml_register_subject(scope, "borrowed_flag", &borrowed) == LV_RESULT_OK);
    REQUIRE(lv_xml_get_subject(scope, "borrowed_flag") == &borrowed);

    lv_subject_set_int(&borrowed, 42);

    // Pre-fix this aborts the process here: lv_free() on &borrowed.
    REQUIRE(lv_xml_component_unregister("t_borrow_survive") == LV_RESULT_OK);
    REQUIRE(lv_xml_component_get_scope("t_borrow_survive") == nullptr);

    // The subject must still be intact and usable — value preserved, and it can
    // still take a new observer and drive it.
    REQUIRE(lv_subject_get_int(&borrowed) == 42);

    ObserverProbe probe;
    lv_observer_t* obs = lv_subject_add_observer(&borrowed, probe_cb, &probe);
    REQUIRE(obs != nullptr);
    REQUIRE(probe.fired == 1); // add_observer fires once with the current value
    REQUIRE(probe.last_value == 42);

    lv_subject_set_int(&borrowed, 99);
    REQUIRE(probe.fired == 2);
    REQUIRE(probe.last_value == 99);

    lv_subject_deinit(&borrowed);
}

// ============================================================================
// 2. An owned subject is still torn down
// ============================================================================

TEST_CASE_METHOD(LVGLTestFixture, "owned <subject> is still deinit'd and freed at scope teardown",
                 "[xml][hotreload]") {
    const char* comp = "<component>"
                       "  <subjects>"
                       "    <subject name='owned_int' type='int' value='7'/>"
                       "    <subject name='owned_str' type='string' value='hello'/>"
                       "  </subjects>"
                       "  <view>"
                       "    <lv_obj name='box'/>"
                       "  </view>"
                       "</component>";
    REQUIRE(lv_xml_register_component_from_data("t_owned_freed", comp) == LV_RESULT_OK);

    lv_xml_component_scope_t* scope = lv_xml_component_get_scope("t_owned_freed");
    REQUIRE(scope != nullptr);
    lv_subject_t* owned = lv_xml_get_subject(scope, "owned_int");
    REQUIRE(owned != nullptr);
    REQUIRE(lv_subject_get_int(owned) == 7);

    // Parser-allocated subjects must NOT be mistaken for borrowed ones: the
    // scope is their only owner.
    REQUIRE(lv_xml_component_unregister("t_owned_freed") == LV_RESULT_OK);
    REQUIRE(lv_xml_component_get_scope("t_owned_freed") == nullptr);

    // Churn the same component through many register/unregister cycles. Each
    // cycle allocates two lv_subject_t plus two 256-byte string buffers; if the
    // owned path ever stops freeing them this is a visible leak under
    // `make test-asan` (LeakSanitizer). In a plain build the cycle still proves
    // the scope is fully reclaimed and re-creatable.
    for (int i = 0; i < 200; i++) {
        REQUIRE(lv_xml_register_component_from_data("t_owned_freed", comp) == LV_RESULT_OK);
        lv_xml_component_scope_t* s = lv_xml_component_get_scope("t_owned_freed");
        REQUIRE(s != nullptr);
        // A fresh scope each time: the subject is re-parsed from XML, back at 7.
        lv_subject_t* fresh = lv_xml_get_subject(s, "owned_int");
        REQUIRE(fresh != nullptr);
        REQUIRE(lv_subject_get_int(fresh) == 7);
        lv_subject_set_int(fresh, 1000 + i);
        REQUIRE(lv_xml_component_unregister("t_owned_freed") == LV_RESULT_OK);
    }
    REQUIRE(lv_xml_component_get_scope("t_owned_freed") == nullptr);
}

// ============================================================================
// 3. A borrowed subject is not deinit'd (observers in other components survive)
// ============================================================================

TEST_CASE_METHOD(LVGLTestFixture,
                 "unregistering a borrowing scope leaves observers of other components attached",
                 "[xml][hotreload]") {
    // Component "user" binds a widget flag to the shared subject.
    const char* user_xml = "<component>"
                           "  <view>"
                           "    <lv_obj name='box'>"
                           "      <bind_flag_if_eq subject='shared' flag='hidden' ref_value='1'/>"
                           "    </lv_obj>"
                           "  </view>"
                           "</component>";
    REQUIRE(lv_xml_register_component_from_data("t_borrow_user", user_xml) == LV_RESULT_OK);

    // Component "lender" borrows the very same C++-owned subject.
    const char* lender_xml = "<component><view><lv_obj name='other'/></view></component>";
    REQUIRE(lv_xml_register_component_from_data("t_borrow_lender", lender_xml) == LV_RESULT_OK);

    lv_subject_t shared;
    lv_subject_init_int(&shared, 0);

    lv_xml_component_scope_t* user_scope = lv_xml_component_get_scope("t_borrow_user");
    lv_xml_component_scope_t* lender_scope = lv_xml_component_get_scope("t_borrow_lender");
    REQUIRE(user_scope != nullptr);
    REQUIRE(lender_scope != nullptr);
    REQUIRE(lv_xml_register_subject(user_scope, "shared", &shared) == LV_RESULT_OK);
    REQUIRE(lv_xml_register_subject(lender_scope, "shared", &shared) == LV_RESULT_OK);

    lv_obj_t* inst =
        static_cast<lv_obj_t*>(lv_xml_create(lv_screen_active(), "t_borrow_user", nullptr));
    REQUIRE(inst != nullptr);
    lv_obj_t* box = lv_obj_find_by_name(inst, "box");
    REQUIRE(box != nullptr);

    // Direct observer as well, so the assertion does not depend on bind_flag_if_eq.
    ObserverProbe probe;
    REQUIRE(lv_subject_add_observer(&shared, probe_cb, &probe) != nullptr);
    const int fired_before = probe.fired;

    // The binding is live before teardown.
    lv_subject_set_int(&shared, 1);
    REQUIRE(lv_obj_has_flag(box, LV_OBJ_FLAG_HIDDEN));
    lv_subject_set_int(&shared, 0);
    REQUIRE_FALSE(lv_obj_has_flag(box, LV_OBJ_FLAG_HIDDEN));

    // Tear down only the LENDER. It borrowed `shared`; it must neither free it
    // (heap abort) nor deinit it (which walks subs_ll and removes EVERY
    // observer, including the ones t_borrow_user's live widgets hold).
    REQUIRE(lv_xml_component_unregister("t_borrow_lender") == LV_RESULT_OK);

    lv_subject_set_int(&shared, 1);
    REQUIRE(lv_obj_has_flag(box, LV_OBJ_FLAG_HIDDEN));
    lv_subject_set_int(&shared, 0);
    REQUIRE_FALSE(lv_obj_has_flag(box, LV_OBJ_FLAG_HIDDEN));
    REQUIRE(probe.fired > fired_before);

    lv_obj_delete(inst);
    REQUIRE(lv_xml_component_unregister("t_borrow_user") == LV_RESULT_OK);
    lv_subject_deinit(&shared);
}

// ============================================================================
// 4. Hot-reload round trip: borrowed subjects are carried into the new scope
// ============================================================================

TEST_CASE_METHOD(LVGLTestFixture,
                 "hot-reload cycle re-registers borrowed subjects into the fresh scope",
                 "[xml][hotreload]") {
    // What ui_xml/runout_guidance_modal.xml looks like to the reloader: some
    // XML-declared subjects, plus C++-registered ones bound by the view.
    const char* v1 = "<component>"
                     "  <subjects>"
                     "    <subject name='xml_owned' type='int' value='3'/>"
                     "  </subjects>"
                     "  <view>"
                     "    <lv_obj name='box'>"
                     "      <bind_flag_if_eq subject='cpp_flag' flag='hidden' ref_value='1'/>"
                     "    </lv_obj>"
                     "  </view>"
                     "</component>";
    const char* v2 = "<component>"
                     "  <subjects>"
                     "    <subject name='xml_owned' type='int' value='4'/>"
                     "  </subjects>"
                     "  <view>"
                     "    <lv_obj name='box' style_pad_all='8'>"
                     "      <bind_flag_if_eq subject='cpp_flag' flag='hidden' ref_value='1'/>"
                     "    </lv_obj>"
                     "  </view>"
                     "</component>";

    REQUIRE(lv_xml_register_component_from_data("t_reload_rt", v1) == LV_RESULT_OK);

    lv_subject_t cpp_flag; // C++-owned storage, as a modal's static member would be
    lv_subject_init_int(&cpp_flag, 0);
    REQUIRE(lv_xml_register_subject(lv_xml_component_get_scope("t_reload_rt"), "cpp_flag",
                                    &cpp_flag) == LV_RESULT_OK);

    // Sanity: v1 resolves both provenances.
    lv_xml_component_scope_t* scope = lv_xml_component_get_scope("t_reload_rt");
    REQUIRE(lv_xml_get_subject(scope, "cpp_flag") == &cpp_flag);
    REQUIRE(lv_subject_get_int(lv_xml_get_subject(scope, "xml_owned")) == 3);

    // --- the reload cycle XmlHotReloader runs on the LVGL thread ---
    auto borrowed = helix::snapshot_borrowed_subjects("t_reload_rt");
    REQUIRE(borrowed.size() == 1);
    REQUIRE(borrowed[0].first == "cpp_flag");
    REQUIRE(borrowed[0].second == &cpp_flag);

    REQUIRE(lv_xml_component_unregister("t_reload_rt") == LV_RESULT_OK);
    REQUIRE(lv_xml_register_component_from_data("t_reload_rt", v2) == LV_RESULT_OK);
    REQUIRE(helix::restore_borrowed_subjects("t_reload_rt", borrowed) == 1);
    // --- end of cycle ---

    // Deliberately no `fresh != scope` assertion: the old scope really is freed,
    // and the allocator hands the same block straight back, so the pointers
    // routinely match. The re-parsed value below is the honest proof of a fresh
    // registration.
    lv_xml_component_scope_t* fresh = lv_xml_component_get_scope("t_reload_rt");
    REQUIRE(fresh != nullptr);

    // The XML-declared subject was re-parsed from v2 (new object, new value).
    lv_subject_t* xml_owned = lv_xml_get_subject(fresh, "xml_owned");
    REQUIRE(xml_owned != nullptr);
    REQUIRE(lv_subject_get_int(xml_owned) == 4);

    // The borrowed subject is the SAME object, still resolvable by name — this
    // is what keeps the reloaded component's bind_* wiring alive.
    REQUIRE(lv_xml_get_subject(fresh, "cpp_flag") == &cpp_flag);

    // And it actually binds: an instance created from the reloaded definition
    // must react to the C++-owned subject.
    lv_obj_t* inst =
        static_cast<lv_obj_t*>(lv_xml_create(lv_screen_active(), "t_reload_rt", nullptr));
    REQUIRE(inst != nullptr);
    lv_obj_t* box = lv_obj_find_by_name(inst, "box");
    REQUIRE(box != nullptr);

    lv_subject_set_int(&cpp_flag, 1);
    REQUIRE(lv_obj_has_flag(box, LV_OBJ_FLAG_HIDDEN));
    lv_subject_set_int(&cpp_flag, 0);
    REQUIRE_FALSE(lv_obj_has_flag(box, LV_OBJ_FLAG_HIDDEN));

    lv_obj_delete(inst);
    REQUIRE(lv_xml_component_unregister("t_reload_rt") == LV_RESULT_OK);
    lv_subject_deinit(&cpp_flag);
}
