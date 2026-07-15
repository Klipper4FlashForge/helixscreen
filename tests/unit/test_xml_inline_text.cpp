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
