// SPDX-License-Identifier: GPL-3.0-or-later
#include "lvgl_test_fixture.h"
#include "printer_state.h"
#include "toolhead_homing.h"

#include "../catch_amalgamated.hpp"

TEST_CASE("toolhead_is_homed reads the live homed_axes subject", "[homing][toolhead]") {
    LVGLTestFixture fixture;
    helix::PrinterState ps;
    ps.init_subjects(true);

    SECTION("empty means not homed") {
        lv_subject_copy_string(ps.get_homed_axes_subject(), "");
        CHECK_FALSE(helix::toolhead_is_homed(ps));
    }
    SECTION("xyz means homed") {
        lv_subject_copy_string(ps.get_homed_axes_subject(), "xyz");
        CHECK(helix::toolhead_is_homed(ps));
    }
    SECTION("partial homing is NOT homed") {
        lv_subject_copy_string(ps.get_homed_axes_subject(), "xy");
        CHECK_FALSE(helix::toolhead_is_homed(ps));
    }
    SECTION("z-only is NOT homed") {
        lv_subject_copy_string(ps.get_homed_axes_subject(), "z");
        CHECK_FALSE(helix::toolhead_is_homed(ps));
    }
}
