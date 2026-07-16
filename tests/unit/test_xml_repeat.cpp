// SPDX-License-Identifier: GPL-3.0-or-later
#include "../catch_amalgamated.hpp"
#include "../lvgl_test_fixture.h"
extern "C" {
#include "helix-xml/src/xml/lv_xml.h"
}

static const char * COMP_LITERAL =
  "<component>"
  "  <view>"
  "    <lv_obj name='root'>"
  "      <repeat count='4'>"
  "        <lv_obj name='item'/>"
  "      </repeat>"
  "    </lv_obj>"
  "  </view>"
  "</component>";

TEST_CASE_METHOD(LVGLTestFixture, "repeat: literal count creates N children", "[xml][repeat]") {
    REQUIRE(lv_xml_register_component_from_data("t_rep", COMP_LITERAL) == LV_RESULT_OK);
    lv_obj_t * v = (lv_obj_t*)lv_xml_create(lv_screen_active(), "t_rep", nullptr);
    REQUIRE(v != nullptr);
    lv_obj_t * root = lv_obj_find_by_name(v, "root");
    REQUIRE(root != nullptr);
    REQUIRE(lv_obj_get_child_count(root) == 4);
    lv_obj_delete(v);
    lv_xml_component_unregister("t_rep");
}

static const char * COMP_ZERO =
  "<component>"
  "  <view>"
  "    <lv_obj name='root'>"
  "      <repeat count='0'>"
  "        <lv_obj name='item'/>"
  "      </repeat>"
  "    </lv_obj>"
  "  </view>"
  "</component>";

TEST_CASE_METHOD(LVGLTestFixture, "repeat: count 0 creates nothing, no crash", "[xml][repeat]") {
    REQUIRE(lv_xml_register_component_from_data("t_rep0", COMP_ZERO) == LV_RESULT_OK);
    lv_obj_t * v = (lv_obj_t*)lv_xml_create(lv_screen_active(), "t_rep0", nullptr);
    REQUIRE(v != nullptr);
    lv_obj_t * root = lv_obj_find_by_name(v, "root");
    REQUIRE(root != nullptr);
    REQUIRE(lv_obj_get_child_count(root) == 0);
    lv_obj_delete(v);
    lv_xml_component_unregister("t_rep0");
}

static const char * COMP_ONE =
  "<component>"
  "  <view>"
  "    <lv_obj name='root'>"
  "      <repeat count='1'>"
  "        <lv_obj name='item'/>"
  "      </repeat>"
  "    </lv_obj>"
  "  </view>"
  "</component>";

TEST_CASE_METHOD(LVGLTestFixture, "repeat: count 1 creates one", "[xml][repeat]") {
    REQUIRE(lv_xml_register_component_from_data("t_rep1", COMP_ONE) == LV_RESULT_OK);
    lv_obj_t * v = (lv_obj_t*)lv_xml_create(lv_screen_active(), "t_rep1", nullptr);
    REQUIRE(v != nullptr);
    lv_obj_t * root = lv_obj_find_by_name(v, "root");
    REQUIRE(root != nullptr);
    REQUIRE(lv_obj_get_child_count(root) == 1);
    lv_obj_delete(v);
    lv_xml_component_unregister("t_rep1");
}

// #const count: the body still expands N times where N comes from a component const.
static const char * COMP_CONST =
  "<component>"
  "  <consts><const name='rows' value='5'/></consts>"
  "  <view>"
  "    <lv_obj name='root'>"
  "      <repeat count='#rows'>"
  "        <lv_obj name='item'/>"
  "      </repeat>"
  "    </lv_obj>"
  "  </view>"
  "</component>";

TEST_CASE_METHOD(LVGLTestFixture, "repeat: #const count expands", "[xml][repeat]") {
    REQUIRE(lv_xml_register_component_from_data("t_rep_c", COMP_CONST) == LV_RESULT_OK);
    lv_obj_t * v = (lv_obj_t*)lv_xml_create(lv_screen_active(), "t_rep_c", nullptr);
    REQUIRE(v != nullptr);
    lv_obj_t * root = lv_obj_find_by_name(v, "root");
    REQUIRE(root != nullptr);
    REQUIRE(lv_obj_get_child_count(root) == 5);
    lv_obj_delete(v);
    lv_xml_component_unregister("t_rep_c");
}

// THE #1 hazard: destructive in-place attrs mutation across iterations. Each
// pair of widgets per iteration exercises $i, $param, and #const simultaneously
// so all three must survive the per-iteration fresh-attrs replay independently.
// If the shared-attrs bug is present $i labels read the same value (e.g. "2",
// "2","2") or empty, #const fails to re-resolve, and $param would drop.
static const char * COMP_I =
  "<component>"
  "  <api><prop name='label' type='string'/></api>"
  "  <consts><const name='pad' value='7'/></consts>"
  "  <view>"
  "    <lv_obj name='root'>"
  "      <repeat count='3'>"
  "        <lv_label name='lbl' text='$i' style_pad_all='#pad'/>"
  "        <lv_label name='plbl' text='$label'/>"
  "      </repeat>"
  "    </lv_obj>"
  "  </view>"
  "</component>";

TEST_CASE_METHOD(LVGLTestFixture, "repeat: $i + $param + #const yield independent per-iteration values", "[xml][repeat]") {
    REQUIRE(lv_xml_register_component_from_data("t_rep_i", COMP_I) == LV_RESULT_OK);
    const char * inst_attrs[] = {"label", "hi", nullptr};
    lv_obj_t * v = (lv_obj_t*)lv_xml_create(lv_screen_active(), "t_rep_i", inst_attrs);
    REQUIRE(v != nullptr);
    lv_obj_t * root = lv_obj_find_by_name(v, "root");
    REQUIRE(root != nullptr);
    // 3 iterations x 2 labels = 6 children, interleaved [lbl0, plbl0, lbl1, ...].
    REQUIRE(lv_obj_get_child_count(root) == 6);
    for (uint32_t k = 0; k < 3; k++) {
        lv_obj_t * lbl = lv_obj_get_child(root, k * 2);      // the $i / #const label
        lv_obj_t * plbl = lv_obj_get_child(root, k * 2 + 1); // the $param label
        REQUIRE(lbl != nullptr);
        REQUIRE(plbl != nullptr);
        char expect[8]; snprintf(expect, sizeof expect, "%u", k);
        REQUIRE(std::string(lv_label_get_text(lbl)) == expect);   // $i is per-iteration
        // pad_all sets all four sides; read one back. #const must resolve each iter.
        REQUIRE(lv_obj_get_style_pad_left(lbl, LV_PART_MAIN) == 7);
        // $param resolves against the instantiation attrs on every iteration.
        REQUIRE(std::string(lv_label_get_text(plbl)) == "hi");
    }
    lv_obj_delete(v);
    lv_xml_component_unregister("t_rep_i");
}

// A <repeat> with no `count` must still balance the parent stack: the closing
// </repeat> is intercepted (count=0), so the following sibling stays parented to
// `root` instead of mis-parenting when </repeat> pops a frame it never pushed.
static const char * COMP_NOCOUNT =
  "<component>"
  "  <view>"
  "    <lv_obj name='root'>"
  "      <repeat>"
  "        <lv_label name='item'/>"
  "      </repeat>"
  "      <lv_button name='sib'/>"
  "    </lv_obj>"
  "  </view>"
  "</component>";

TEST_CASE_METHOD(LVGLTestFixture, "repeat: missing count expands zero times, sibling stays parented", "[xml][repeat]") {
    REQUIRE(lv_xml_register_component_from_data("t_rep_nc", COMP_NOCOUNT) == LV_RESULT_OK);
    lv_obj_t * v = (lv_obj_t*)lv_xml_create(lv_screen_active(), "t_rep_nc", nullptr);
    REQUIRE(v != nullptr);
    lv_obj_t * root = lv_obj_find_by_name(v, "root");
    REQUIRE(root != nullptr);
    lv_obj_t * sib = lv_obj_find_by_name(v, "sib");
    REQUIRE(sib != nullptr);
    REQUIRE(lv_obj_get_parent(sib) == root);        // NOT mis-parented onto the screen
    REQUIRE(lv_obj_get_child_count(root) == 1);      // only sib; repeat expanded nothing
    lv_obj_delete(v);
    lv_xml_component_unregister("t_rep_nc");
}
