// SPDX-License-Identifier: GPL-3.0-or-later
// Tests for inline XML text content: <text_muted>Foo</text_muted> ==
// <text_muted text="Foo" translation_tag="Foo"/>.

#include "../test_fixtures.h"

#include "helix-xml/src/xml/lv_xml_component.h"

#include "../catch_amalgamated.hpp"

namespace {

// Registers a one-off component from an XML string and instantiates it.
// Returns the named child, or nullptr.
lv_obj_t* create_and_find(XMLTestFixture& fx, const char* comp_name, const char* xml,
                          const char* child_name) {
    if (lv_xml_register_component_from_data(comp_name, xml) != LV_RESULT_OK) return nullptr;
    lv_obj_t* root = fx.create_component(comp_name);
    if (!root) return nullptr;
    return lv_obj_find_by_name(root, child_name);
}

} // namespace

TEST_CASE_METHOD(XMLTestFixture, "Inline text applies to semantic text widget",
                 "[xml][inline_text]") {
    const char* xml = R"(<component>
  <view extends="lv_obj" width="300" height="300">
    <text_muted name="msg">Hello world</text_muted>
  </view>
</component>)";
    lv_obj_t* msg = create_and_find(*this, "it_basic", xml, "msg");
    REQUIRE(msg != nullptr);
    CHECK(lv_streq(lv_label_get_text(msg), "Hello world"));
}

TEST_CASE_METHOD(XMLTestFixture, "Inline text applies to lv_label", "[xml][inline_text]") {
    const char* xml = R"(<component>
  <view extends="lv_obj" width="300" height="300">
    <lv_label name="msg">Plain label</lv_label>
  </view>
</component>)";
    lv_obj_t* msg = create_and_find(*this, "it_lvlabel", xml, "msg");
    REQUIRE(msg != nullptr);
    CHECK(lv_streq(lv_label_get_text(msg), "Plain label"));
}

TEST_CASE_METHOD(XMLTestFixture, "Inline text collapses whitespace HTML-style",
                 "[xml][inline_text]") {
    // Shared collapse-parity table — keep in sync with
    // tests/python/test_inline_text_extraction.py.
    const char* xml = "<component>\n"
                      "  <view extends=\"lv_obj\" width=\"300\" height=\"300\">\n"
                      "    <text_muted name=\"a\">  Hello  world  </text_muted>\n"
                      "    <text_muted name=\"b\">\n    Hello\n    world\n  </text_muted>\n"
                      "    <text_muted name=\"c\">Tabs\there\tand\rthere</text_muted>\n"
                      "    <text_muted name=\"d\">Hello&#10;world</text_muted>\n"
                      "  </view>\n"
                      "</component>";
    REQUIRE(lv_xml_register_component_from_data("it_ws", xml) == LV_RESULT_OK);
    lv_obj_t* root = create_component("it_ws");
    REQUIRE(root != nullptr);
    CHECK(lv_streq(lv_label_get_text(lv_obj_find_by_name(root, "a")), "Hello world"));
    CHECK(lv_streq(lv_label_get_text(lv_obj_find_by_name(root, "b")), "Hello world"));
    CHECK(lv_streq(lv_label_get_text(lv_obj_find_by_name(root, "c")), "Tabs here and there"));
    CHECK(lv_streq(lv_label_get_text(lv_obj_find_by_name(root, "d")), "Hello world"));
}

TEST_CASE_METHOD(XMLTestFixture, "Inline text decodes XML entities", "[xml][inline_text]") {
    const char* xml = R"(<component>
  <view extends="lv_obj" width="300" height="300">
    <text_muted name="msg">Fish &amp; chips &lt;3</text_muted>
  </view>
</component>)";
    lv_obj_t* msg = create_and_find(*this, "it_ent", xml, "msg");
    REQUIRE(msg != nullptr);
    CHECK(lv_streq(lv_label_get_text(msg), "Fish & chips <3"));
}

TEST_CASE_METHOD(XMLTestFixture, "Whitespace-only inline content is a no-op",
                 "[xml][inline_text]") {
    // This is exactly what pretty-printed XML produces between tags today.
    const char* xml = "<component>\n"
                      "  <view extends=\"lv_obj\" width=\"300\" height=\"300\">\n"
                      "    <text_muted name=\"msg\" text=\"kept\">\n    </text_muted>\n"
                      "  </view>\n"
                      "</component>";
    lv_obj_t* msg = create_and_find(*this, "it_wsonly", xml, "msg");
    REQUIRE(msg != nullptr);
    CHECK(lv_streq(lv_label_get_text(msg), "kept"));
}

TEST_CASE_METHOD(XMLTestFixture, "Inline text sets the translation tag",
                 "[xml][inline_text]") {
    const char* xml = R"(<component>
  <view extends="lv_obj" width="300" height="300">
    <text_muted name="msg">Untranslated key</text_muted>
  </view>
</component>)";
    lv_obj_t* msg = create_and_find(*this, "it_tag", xml, "msg");
    REQUIRE(msg != nullptr);
    // No pack registered: lv_tr() falls back to the tag itself.
    // (lv_label has no public translation_tag getter, so the deep-check that
    // the tag was stored lives in the Task 3 language-switch test.)
    CHECK(lv_streq(lv_label_get_text(msg), "Untranslated key"));
}

TEST_CASE_METHOD(XMLTestFixture, "Mixed content: text plus child element",
                 "[xml][inline_text]") {
    // Text before and after a child concatenates onto the OWNING element;
    // the child is created normally.
    const char* xml = R"(<component>
  <view extends="lv_obj" width="300" height="300">
    <lv_obj name="card" width="200" height="200">Before
      <text_muted name="inner">Inner</text_muted>
    </lv_obj>
  </view>
</component>)";
    REQUIRE(lv_xml_register_component_from_data("it_mixed", xml) == LV_RESULT_OK);
    lv_obj_t* root = create_component("it_mixed");
    REQUIRE(root != nullptr);
    lv_obj_t* inner = lv_obj_find_by_name(root, "inner");
    REQUIRE(inner != nullptr);
    CHECK(lv_streq(lv_label_get_text(inner), "Inner"));
    // "Before" targeted the lv_obj card, which ignores text= — must not crash,
    // must not leak onto the inner label.
    lv_obj_t* card = lv_obj_find_by_name(root, "card");
    REQUIRE(card != nullptr);
}

TEST_CASE_METHOD(XMLTestFixture, "Inline text directly on the root view is dropped",
                 "[xml][inline_text]") {
    // The closing </view> tag is what expat hands view_end_element_handler --
    // literally "view", never the resolved `extends` target -- so
    // apply_pending_inline_text()'s processor lookup (lv_xml_widget_get_processor
    // + lv_xml_component_get_scope, both keyed on that raw name) always misses
    // for "view" and the pending text is freed unapplied. This pins that
    // behavior empirically: extending lv_label (which would render inline text
    // if it were applied) proves it is NOT applied, rather than just being a
    // widget that ignores text like lv_obj would.
    const char* xml = R"(<component>
  <view extends="lv_label" width="300" height="60">Root inline text</view>
</component>)";
    REQUIRE(lv_xml_register_component_from_data("it_root_view", xml) == LV_RESULT_OK);
    lv_obj_t* root = create_component("it_root_view");
    REQUIRE(root != nullptr);
    // lv_label_create()'s constructor unconditionally sets text to
    // LV_LABEL_DEFAULT_TEXT ("Text", an XML-editor preview placeholder) before
    // any XML attribute/content is applied. No "text" attribute was given, so
    // the label still showing that exact placeholder (rather than "Root
    // inline text") confirms the inline content never reached
    // lv_label_set_text() at all.
    CHECK(lv_streq(lv_label_get_text(root), "Text"));
}

TEST_CASE_METHOD(XMLTestFixture, "Non-text widget ignores inline text without crashing",
                 "[xml][inline_text]") {
    const char* xml = R"(<component>
  <view extends="lv_obj" width="300" height="300">
    <lv_obj name="box" width="100" height="100">stray text</lv_obj>
  </view>
</component>)";
    lv_obj_t* box = create_and_find(*this, "it_ignore", xml, "box");
    REQUIRE(box != nullptr);
    CHECK(lv_obj_get_child_count(box) == 0);
}

TEST_CASE_METHOD(XMLTestFixture, "text attribute wins over inline text",
                 "[xml][inline_text]") {
    const char* xml = R"(<component>
  <view extends="lv_obj" width="300" height="300">
    <text_muted name="msg" text="attribute wins">inline loses</text_muted>
  </view>
</component>)";
    lv_obj_t* msg = create_and_find(*this, "it_conflict_text", xml, "msg");
    REQUIRE(msg != nullptr);
    CHECK(lv_streq(lv_label_get_text(msg), "attribute wins"));
}

TEST_CASE_METHOD(XMLTestFixture, "translation_tag attribute wins over inline text",
                 "[xml][inline_text]") {
    const char* xml = R"(<component>
  <view extends="lv_obj" width="300" height="300">
    <text_muted name="msg" translation_tag="Existing Key">inline loses</text_muted>
  </view>
</component>)";
    lv_obj_t* msg = create_and_find(*this, "it_conflict_tag", xml, "msg");
    REQUIRE(msg != nullptr);
    // No pack: tag falls back to itself.
    CHECK(lv_streq(lv_label_get_text(msg), "Existing Key"));
}

TEST_CASE_METHOD(XMLTestFixture, "bind_text wins over inline text", "[xml][inline_text]") {
    static lv_subject_t subj;
    static char buf[64];
    static char prev[64];
    lv_subject_init_string(&subj, buf, prev, sizeof(buf), "bound value");
    lv_xml_register_subject(NULL, "it_bound_subject", &subj);
    const char* xml = R"(<component>
  <view extends="lv_obj" width="300" height="300">
    <text_muted name="msg" bind_text="it_bound_subject">inline loses</text_muted>
  </view>
</component>)";
    lv_obj_t* msg = create_and_find(*this, "it_conflict_bind", xml, "msg");
    REQUIRE(msg != nullptr);
    CHECK(lv_streq(lv_label_get_text(msg), "bound value"));
}

TEST_CASE_METHOD(XMLTestFixture, "$prop substitution works in inline text",
                 "[xml][inline_text]") {
    const char* xml = R"(<component>
  <api>
    <prop name="title" type="string" default="Default title"/>
  </api>
  <view extends="lv_obj" width="300" height="300">
    <text_muted name="msg">$title</text_muted>
  </view>
</component>)";
    REQUIRE(lv_xml_register_component_from_data("it_prop", xml) == LV_RESULT_OK);
    const char* attrs[] = {"title", "Passed title", nullptr};
    lv_obj_t* root = create_component("it_prop", attrs);
    REQUIRE(root != nullptr);
    lv_obj_t* msg = lv_obj_find_by_name(root, "msg");
    REQUIRE(msg != nullptr);
    CHECK(lv_streq(lv_label_get_text(msg), "Passed title"));
}

TEST_CASE_METHOD(XMLTestFixture, "$prop default applies when attr not passed",
                 "[xml][inline_text]") {
    // Component registered in the previous test may persist per-scope; register
    // under a fresh name to stay order-independent.
    const char* xml = R"(<component>
  <api>
    <prop name="title" type="string" default="Default title"/>
  </api>
  <view extends="lv_obj" width="300" height="300">
    <text_muted name="msg">$title</text_muted>
  </view>
</component>)";
    lv_obj_t* msg = create_and_find(*this, "it_prop_default", xml, "msg");
    REQUIRE(msg != nullptr);
    CHECK(lv_streq(lv_label_get_text(msg), "Default title"));
}

TEST_CASE_METHOD(XMLTestFixture, "#const substitution works in inline text",
                 "[xml][inline_text]") {
    const char* xml = R"(<component>
  <consts>
    <str name="it_greeting" value="Const hello"/>
  </consts>
  <view extends="lv_obj" width="300" height="300">
    <text_muted name="msg">#it_greeting</text_muted>
  </view>
</component>)";
    lv_obj_t* msg = create_and_find(*this, "it_const", xml, "msg");
    REQUIRE(msg != nullptr);
    CHECK(lv_streq(lv_label_get_text(msg), "Const hello"));
}

TEST_CASE_METHOD(XMLTestFixture, "Inline text re-resolves on language change",
                 "[xml][inline_text][translation]") {
    // Deep-check for the "it_tag" test above: proves the synthesized
    // translation_tag is actually stored on the label (not just applied as a
    // literal text= fallback) by round-tripping it through a real pack and a
    // language switch.
    //
    // Pack lifetime note: LVGLTestFixture calls lv_init_safe() once via
    // std::call_once (tests/lvgl_test_fixture.cpp) and is never torn down with
    // lv_deinit() between test cases -- LVGL, and any dynamic translation pack
    // registered into it, persists for the lifetime of the whole test binary.
    // LVGL's translation module has no "remove one pack" API (see
    // include/translation_loader.h) and lv_translation_deinit() nukes every
    // registered pack process-wide, which would be unsafe to call here since
    // other tests may load the real app translation catalog. So this pack is
    // deliberately left registered rather than partially/unsafely torn down.
    // That's safe: lv_translation_get() walks packs most-recently-added
    // first, so it only intercepts lookups for the exact tag below, and
    // "Print speed" does not appear in ui_xml/translations/translations.xml
    // (verified), so it can't shadow a real translation used by another test.
    lv_translation_pack_t* pack = lv_translation_add_dynamic();
    REQUIRE(pack != nullptr);
    lv_translation_add_language(pack, "en");
    lv_translation_add_language(pack, "de");
    lv_translation_tag_dsc_t* tag = lv_translation_add_tag(pack, "Print speed");
    REQUIRE(tag != nullptr);
    lv_translation_set_tag_translation(pack, tag, 0, "Print speed");
    lv_translation_set_tag_translation(pack, tag, 1, "Druckgeschwindigkeit");
    lv_translation_set_language("en");

    const char* xml = R"(<component>
  <view extends="lv_obj" width="300" height="300">
    <text_muted name="msg">Print speed</text_muted>
  </view>
</component>)";
    lv_obj_t* msg = create_and_find(*this, "it_i18n", xml, "msg");
    REQUIRE(msg != nullptr);
    CHECK(lv_streq(lv_label_get_text(msg), "Print speed"));

    lv_translation_set_language("de");
    process_lvgl(50);
    CHECK(lv_streq(lv_label_get_text(msg), "Druckgeschwindigkeit"));

    // Restore the global language selection so it doesn't bleed into
    // whatever test runs next in this process (packs themselves are left
    // registered -- see note above).
    lv_translation_set_language("en");
}
