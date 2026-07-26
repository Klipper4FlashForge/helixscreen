// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_gcode_classify.cpp
 * @brief Tests for the is_discretionary_gcode() default-allow classifier.
 *
 * Discretionary = convenience commands (fan, temp, non-homing moves, LED) that
 * are safe to REFUSE while the printer is busy with a blocking op so they don't
 * queue and time out. Default-ALLOW: true ONLY for a known discretionary set;
 * everything else returns false (allowed) so nothing important is ever blocked.
 */

#include "../../include/gcode_classify.h"

#include "../catch_amalgamated.hpp"

using namespace helix;

// ============================================================================
// Case 1: is_discretionary_gcode() token matcher
// ============================================================================

TEST_CASE("is_discretionary_gcode recognizes the discretionary command set", "[gcode][classify]") {
    SECTION("fan commands are discretionary (upper + lower case)") {
        CHECK(is_discretionary_gcode("M106"));
        CHECK(is_discretionary_gcode("m106"));
        CHECK(is_discretionary_gcode("M106 S255"));
        CHECK(is_discretionary_gcode("M107"));
        CHECK(is_discretionary_gcode("m107"));
        CHECK(is_discretionary_gcode("SET_FAN_SPEED FAN=fan0 SPEED=1.0"));
        CHECK(is_discretionary_gcode("set_fan_speed FAN=fan0 SPEED=1.0"));
    }

    SECTION("temp commands are discretionary (upper + lower case)") {
        CHECK(is_discretionary_gcode("M104 S200"));
        CHECK(is_discretionary_gcode("m104 s200"));
        CHECK(is_discretionary_gcode("M140 S60"));
        CHECK(is_discretionary_gcode("m140 s60"));
        CHECK(is_discretionary_gcode("M109 S200"));
        CHECK(is_discretionary_gcode("m109 s200"));
        CHECK(is_discretionary_gcode("M190 S60"));
        CHECK(is_discretionary_gcode("m190 s60"));
        CHECK(is_discretionary_gcode("SET_HEATER_TEMPERATURE HEATER=extruder TARGET=200"));
        CHECK(is_discretionary_gcode("set_heater_temperature HEATER=extruder TARGET=200"));
        // Exact strings MoonrakerAPI::set_temperature emits for chamber + temperature_fan
        // heaters (build_heater_gcode): these also route through the guarded execute_gcode.
        CHECK(is_discretionary_gcode("M141 S50"));
        CHECK(is_discretionary_gcode("m141 s50"));
        CHECK(
            is_discretionary_gcode("SET_TEMPERATURE_FAN_TARGET TEMPERATURE_FAN=chamber TARGET=40"));
    }

    SECTION("non-homing moves are discretionary (upper + lower case)") {
        CHECK(is_discretionary_gcode("G0 X10"));
        CHECK(is_discretionary_gcode("g0 x10"));
        CHECK(is_discretionary_gcode("G1 X10 Y10 F3000"));
        CHECK(is_discretionary_gcode("g1 x10 y10 f3000"));
    }

    SECTION("LED command is discretionary (upper + lower case)") {
        CHECK(is_discretionary_gcode("SET_LED LED=my_led RED=1"));
        CHECK(is_discretionary_gcode("set_led LED=my_led RED=1"));
    }

    SECTION("positioning-mode sets are discretionary (upper + lower case)") {
        CHECK(is_discretionary_gcode("G90"));
        CHECK(is_discretionary_gcode("g90"));
        CHECK(is_discretionary_gcode("G91"));
        CHECK(is_discretionary_gcode("g91"));
    }

    SECTION("wrapped relative jog (as move_axis emits it) is entirely discretionary") {
        // MoonrakerMotionAPI::move_axis emits G91 / G0 / G90 — all three lines
        // must be discretionary or the jog would slip past the busy guard.
        CHECK(is_discretionary_gcode("G91\nG0 X10 F3000\nG90"));
    }
}

// ============================================================================
// Case 2: recovery / critical / probe-control commands are NOT discretionary
// ============================================================================

TEST_CASE("is_discretionary_gcode never blocks recovery or critical commands",
          "[gcode][classify]") {
    SECTION("homing / motor / e-stop") {
        CHECK_FALSE(is_discretionary_gcode("G28"));
        CHECK_FALSE(is_discretionary_gcode("g28"));
        CHECK_FALSE(is_discretionary_gcode("M112"));
        CHECK_FALSE(is_discretionary_gcode("EMERGENCY_STOP"));
        CHECK_FALSE(is_discretionary_gcode("M84"));
        CHECK_FALSE(is_discretionary_gcode("M18"));
    }

    SECTION("print lifecycle recovery") {
        CHECK_FALSE(is_discretionary_gcode("PAUSE"));
        CHECK_FALSE(is_discretionary_gcode("RESUME"));
        CHECK_FALSE(is_discretionary_gcode("CANCEL_PRINT"));
        CHECK_FALSE(is_discretionary_gcode("FIRMWARE_RESTART"));
        CHECK_FALSE(is_discretionary_gcode("RESTART"));
    }

    SECTION("manual-probe control commands (needed to finish/abort a probe)") {
        CHECK_FALSE(is_discretionary_gcode("TESTZ"));
        CHECK_FALSE(is_discretionary_gcode("ACCEPT"));
        CHECK_FALSE(is_discretionary_gcode("ABORT"));
        CHECK_FALSE(is_discretionary_gcode("SET_GCODE_OFFSET Z=0.1"));
    }

    SECTION("calibration commands and arbitrary macros") {
        CHECK_FALSE(is_discretionary_gcode("BED_MESH_CALIBRATE"));
        CHECK_FALSE(is_discretionary_gcode("QUAD_GANTRY_LEVEL"));
        CHECK_FALSE(is_discretionary_gcode("PROBE_ACCURACY"));
        CHECK_FALSE(is_discretionary_gcode("MY_CUSTOM_MACRO"));
        CHECK_FALSE(is_discretionary_gcode("START_PRINT"));
    }
}

// ============================================================================
// Case 3: whole-token matching (no substring / prefix false positives)
// ============================================================================

TEST_CASE("is_discretionary_gcode matches whole tokens only", "[gcode][classify]") {
    CHECK_FALSE(is_discretionary_gcode("M1090"));                     // not M109
    CHECK_FALSE(is_discretionary_gcode("M1060"));                     // not M106
    CHECK_FALSE(is_discretionary_gcode("G10"));                       // not G0/G1
    CHECK_FALSE(is_discretionary_gcode("M10"));                       // not M104/M106/M107
    CHECK_FALSE(is_discretionary_gcode("SET_FAN_SPEED_EXTRA FAN=x")); // not SET_FAN_SPEED
    CHECK_FALSE(is_discretionary_gcode("SET_LEDX R=1"));              // not SET_LED
}

// ============================================================================
// Case 4: empty / blank input is NOT discretionary
// ============================================================================

TEST_CASE("is_discretionary_gcode returns false for empty or blank scripts", "[gcode][classify]") {
    CHECK_FALSE(is_discretionary_gcode(""));
    CHECK_FALSE(is_discretionary_gcode("   "));
    CHECK_FALSE(is_discretionary_gcode("\n\n"));
    CHECK_FALSE(is_discretionary_gcode("   \n  \t "));
    CHECK_FALSE(is_discretionary_gcode("; only a comment"));
}

// ============================================================================
// Case 5: multi-line scripts — discretionary ONLY if EVERY line is
// ============================================================================

TEST_CASE("is_discretionary_gcode requires every non-blank line to be discretionary",
          "[gcode][classify]") {
    SECTION("all-discretionary multi-line -> true") {
        CHECK(is_discretionary_gcode("M106 S255\nM104 S200"));
        CHECK(is_discretionary_gcode("G1 X10\nG1 Y10\nM106 S128"));
        // blank lines between discretionary lines are ignored
        CHECK(is_discretionary_gcode("M106 S255\n\n  \nM104 S200"));
    }

    SECTION("any non-discretionary line -> false (allow the whole script)") {
        CHECK_FALSE(is_discretionary_gcode("M106 S255\nG28"));
        CHECK_FALSE(is_discretionary_gcode("G28\nM106 S255"));
        CHECK_FALSE(is_discretionary_gcode("M104 S200\nTESTZ\nM106 S128"));
    }
}

// ============================================================================
// Case 6: inline comments are stripped (comment-only line counts as blank)
// ============================================================================

TEST_CASE("is_discretionary_gcode strips inline comments", "[gcode][classify]") {
    CHECK(is_discretionary_gcode("M106 S128 ; cool"));
    CHECK(is_discretionary_gcode("M104 S200 ; set nozzle"));
    // comment-only line among discretionary lines is treated as blank
    CHECK(is_discretionary_gcode("M106 S255\n; a note\nM104 S200"));
    // a non-discretionary line still trips even with a trailing comment
    CHECK_FALSE(is_discretionary_gcode("G28 ; home first\nM106 S255"));
}

// ============================================================================
// Case 7: gcode_contains_move() — isolates dangerous-to-queue motion (G0/G1)
// ============================================================================
//
// Within the already-discretionary bucket the busy guard has to distinguish
// commands that queue harmlessly (fan/temp/LED) from a physical MOVE that must
// never fire minutes late after the user has walked away. gcode_contains_move
// flags the latter so the guard can keep rejecting jogs while it queues the rest.

TEST_CASE("gcode_contains_move flags physical moves only", "[gcode][classify][move]") {
    SECTION("bare moves (upper + lower case)") {
        CHECK(gcode_contains_move("G0 X10"));
        CHECK(gcode_contains_move("g0 x10"));
        CHECK(gcode_contains_move("G1 X10 Y10 F3000"));
        CHECK(gcode_contains_move("g1 x10 y10 f3000"));
    }
    SECTION("wrapped relative jog (G91 / G0 / G90) contains a move") {
        CHECK(gcode_contains_move("G91\nG0 X10 F3000\nG90"));
    }
    SECTION("fan / temp / LED are NOT moves") {
        CHECK_FALSE(gcode_contains_move("M106 S255"));
        CHECK_FALSE(gcode_contains_move("M104 S200"));
        CHECK_FALSE(gcode_contains_move("SET_FAN_SPEED FAN=fan0 SPEED=1.0"));
        CHECK_FALSE(gcode_contains_move("SET_LED LED=my_led RED=1"));
    }
    SECTION("bare positioning-mode sets are NOT moves (pure modal state)") {
        CHECK_FALSE(gcode_contains_move("G90"));
        CHECK_FALSE(gcode_contains_move("G91"));
    }
    SECTION("whole-token match — G10/G100 are not G0/G1") {
        CHECK_FALSE(gcode_contains_move("G10"));
        CHECK_FALSE(gcode_contains_move("G100 X1"));
    }
    SECTION("empty / comment-only") {
        CHECK_FALSE(gcode_contains_move(""));
        CHECK_FALSE(gcode_contains_move("; just a note"));
    }
    SECTION("a move anywhere in a multi-line script trips it") {
        CHECK(gcode_contains_move("M106 S255\nG0 X10"));
        CHECK(gcode_contains_move("M104 S200 ; heat\nG1 Z5"));
    }
}

// ============================================================================
// Case 8: discretionary_gcode_noun() — command-type-aware toast wording
// ============================================================================
//
// When a benign command is queued behind a blocking op, the once-per-episode
// toast names what queued ("your temperature change will run when it's ready").

TEST_CASE("discretionary_gcode_noun names the command type", "[gcode][classify][noun]") {
    SECTION("temperature") {
        CHECK(discretionary_gcode_noun("M104 S200") == "temperature change");
        CHECK(discretionary_gcode_noun("M140 S60") == "temperature change");
        CHECK(discretionary_gcode_noun("SET_HEATER_TEMPERATURE HEATER=extruder TARGET=200") ==
              "temperature change");
        CHECK(discretionary_gcode_noun("M141 S50") == "temperature change");
    }
    SECTION("fan") {
        CHECK(discretionary_gcode_noun("M106 S255") == "fan change");
        CHECK(discretionary_gcode_noun("M107") == "fan change");
        CHECK(discretionary_gcode_noun("SET_FAN_SPEED FAN=fan0 SPEED=1.0") == "fan change");
    }
    SECTION("LED") {
        CHECK(discretionary_gcode_noun("SET_LED LED=my_led RED=1") == "LED change");
    }
    SECTION("first meaningful line wins; bare modal/unknown fall back to generic") {
        // The G91 wrapper is modal-only, so the fan line names the toast.
        CHECK(discretionary_gcode_noun("G91\nM106 S255") == "fan change");
        CHECK(discretionary_gcode_noun("G90") == "change");
        CHECK(discretionary_gcode_noun("") == "change");
    }
}

TEST_CASE("newly discretionary commands are gated when busy", "[gcode_classify][1129]") {
    CHECK(helix::is_discretionary_gcode("SET_PIN PIN=case_light VALUE=1.0000"));
    CHECK(helix::is_discretionary_gcode("SET_LED_EFFECT EFFECT=rainbow"));
    CHECK(helix::is_discretionary_gcode("SET_LED_EFFECT EFFECT=rainbow STOP=1"));
    CHECK(helix::is_discretionary_gcode("M220 S100"));
    CHECK(helix::is_discretionary_gcode("M221 S95"));
    CHECK(helix::is_discretionary_gcode("M117 Hello"));
}

TEST_CASE("SET_GCODE_OFFSET is never discretionary", "[gcode_classify][1129]") {
    // It CONTROLS a blocking op: z-offset calibration sends it mid-probe
    // (ui_panel_calibration_zoffset.cpp). Queuing it behind the very operation it
    // steers would break calibration. Same class as TESTZ/ACCEPT/ABORT.
    CHECK_FALSE(helix::is_discretionary_gcode("SET_GCODE_OFFSET Z=0.100"));
    CHECK_FALSE(helix::is_discretionary_gcode("SET_GCODE_OFFSET Z_ADJUST=-0.010"));
}

TEST_CASE("widened table keeps whole-token matching", "[gcode_classify][1129]") {
    // Prefix collisions must not trip the new entries.
    CHECK_FALSE(helix::is_discretionary_gcode("SET_PIN_EXTRA PIN=x"));
    CHECK_FALSE(helix::is_discretionary_gcode("M2200 S1"));
    CHECK_FALSE(helix::is_discretionary_gcode("M1170"));
}

TEST_CASE("new commands do not register as physical moves", "[gcode_classify][1129]") {
    CHECK_FALSE(helix::gcode_contains_move("SET_PIN PIN=case_light VALUE=1.0000"));
    CHECK_FALSE(helix::gcode_contains_move("M220 S100"));
}

TEST_CASE("nouns name the new command kinds", "[gcode_classify][1129]") {
    // SET_PIN drives output_pin LEDs AND non-numeric output_pin fans (fan_gcode.h)
    // — the token alone can't tell which, so it gets the generic noun, not "LED
    // change". SET_LED_EFFECT is unambiguously an LED write.
    CHECK(helix::discretionary_gcode_noun("SET_PIN PIN=case_light VALUE=1.0000") == "change");
    CHECK(helix::discretionary_gcode_noun("SET_LED_EFFECT EFFECT=rainbow") == "LED change");
    CHECK(helix::discretionary_gcode_noun("M220 S100") == "speed change");
    CHECK(helix::discretionary_gcode_noun("M221 S95") == "flow change");
    CHECK(helix::discretionary_gcode_noun("M117 Hello") == "display message");
}

TEST_CASE("SET_PIN stays discretionary but is not misnamed as an LED",
          "[gcode][classify][gcode_classify][noun]") {
    // SET_PIN also drives non-LED output_pin fans (fan_gcode.h:51 emits
    // "SET_PIN PIN=<name> VALUE=..." for non-numeric output_pin fans). A user
    // nudging such a fan mid-bed-mesh must not see "your LED change will run
    // when it's ready" — but the command must still take the discretionary
    // queue path so it doesn't time out behind the blocking op.
    CHECK(helix::is_discretionary_gcode("SET_PIN PIN=x VALUE=1"));
    CHECK(helix::discretionary_gcode_noun("SET_PIN PIN=x VALUE=1") != "LED change");
}
