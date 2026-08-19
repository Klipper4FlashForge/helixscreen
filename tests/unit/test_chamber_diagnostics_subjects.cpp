// tests/unit/test_chamber_diagnostics_subjects.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Task 4 of the chamber-heater backend abstraction (issue #1290): backend
// status frames flow into capability-named diagnostics subjects in
// PrinterTemperatureState, and PrinterCapabilitiesState carries the
// printer_has_chamber_heater_diagnostics / _filter_fan gates.
#include "../lvgl_test_fixture.h"
#include "chamber_heater_backend.h"
#include "printer_capabilities_state.h"
#include "printer_temperature_state.h"

#include <lvgl.h>
#include <string>

#include "../catch_amalgamated.hpp"
#include "hv/json.hpp"

using helix::PrinterCapabilitiesState;
using helix::PrinterTemperatureState;

namespace {

/// Nominal faulted dragonbreath frame (live schema, issue #1290): latched
/// fault with reason, PTC element temp at 106.2°C, filter fan purging at
/// 100%, mode "off" (not externally controlled — heater inactive).
nlohmann::json faulted_diagnostics_status() {
    return nlohmann::json::parse(R"({
      "heater_generic dragonbreath": {"temperature": 25.5, "target": 30.0},
      "dragonbreath": {"fault": true, "inhibited": false, "fault_reason": "ptc_overtemp",
        "ptc_temp": 106.2, "fan_percent": 100, "fan_reason": "purge",
        "mode": "off", "source": "device", "lease_owned": false},
      "output_pin dragonbreath_filter": {"value": 1.0}})");
}

} // namespace

TEST_CASE("dragonbreath status drives diagnostics subjects", "[chamber][subjects]") {
    LVGLTestFixture fixture;

    PrinterTemperatureState ts;
    ts.init_subjects(false);
    ts.set_chamber_heater_name("heater_generic dragonbreath");
    ts.set_chamber_diagnostics_source("dragonbreath", "dragonbreath",
                                      "output_pin dragonbreath_filter");

    ts.update_from_status(faulted_diagnostics_status());

    CHECK(lv_subject_get_int(ts.get_chamber_heater_fault_subject()) == 1);
    CHECK(lv_subject_get_int(ts.get_chamber_heater_inhibited_subject()) == 0);
    CHECK(std::string(lv_subject_get_string(ts.get_chamber_heater_fault_reason_subject())) ==
          "ptc_overtemp");
    CHECK(lv_subject_get_int(ts.get_chamber_heater_externally_controlled_subject()) == 0);
    // 106.2°C → decidegrees
    CHECK(lv_subject_get_int(ts.get_chamber_heater_element_temp_subject()) == 1062);
    CHECK(lv_subject_get_int(ts.get_chamber_filter_fan_percent_subject()) == 100);
    CHECK(std::string(lv_subject_get_string(ts.get_chamber_filter_fan_reason_subject())) ==
          "purge");
    CHECK(lv_subject_get_int(ts.get_chamber_filter_fan_on_subject()) == 1);
    // Capabilities are set by PrinterState::set_hardware in production; unit-level here:
    CHECK(ts.chamber_diagnostics_object() == "dragonbreath");
}

TEST_CASE("absent diagnostics objects in a delta frame keep last values", "[chamber][subjects]") {
    LVGLTestFixture fixture;

    PrinterTemperatureState ts;
    ts.init_subjects(false);
    ts.set_chamber_heater_name("heater_generic dragonbreath");
    ts.set_chamber_diagnostics_source("dragonbreath", "dragonbreath",
                                      "output_pin dragonbreath_filter");

    ts.update_from_status(faulted_diagnostics_status());
    REQUIRE(lv_subject_get_int(ts.get_chamber_heater_fault_subject()) == 1);
    REQUIRE(lv_subject_get_int(ts.get_chamber_filter_fan_on_subject()) == 1);

    // Moonraker status updates are deltas. A frame touching only the heater
    // carries no news about diagnostics or the filter pin — the subjects keep
    // their last values (they do NOT reset to defaults).
    ts.update_from_status({{"heater_generic dragonbreath", {{"temperature", 26.1}}}});

    CHECK(lv_subject_get_int(ts.get_chamber_heater_fault_subject()) == 1);
    CHECK(std::string(lv_subject_get_string(ts.get_chamber_heater_fault_reason_subject())) ==
          "ptc_overtemp");
    CHECK(lv_subject_get_int(ts.get_chamber_heater_element_temp_subject()) == 1062);
    CHECK(lv_subject_get_int(ts.get_chamber_filter_fan_on_subject()) == 1);
}

TEST_CASE("filter fan pin maps output_pin value to on/off", "[chamber][subjects]") {
    LVGLTestFixture fixture;

    PrinterTemperatureState ts;
    ts.init_subjects(false);
    ts.set_chamber_diagnostics_source("dragonbreath", "dragonbreath",
                                      "output_pin dragonbreath_filter");

    // Unknown until the first pin frame arrives.
    REQUIRE(lv_subject_get_int(ts.get_chamber_filter_fan_on_subject()) == -1);

    ts.update_from_status({{"output_pin dragonbreath_filter", {{"value", 0.0}}}});
    CHECK(lv_subject_get_int(ts.get_chamber_filter_fan_on_subject()) == 0);

    ts.update_from_status({{"output_pin dragonbreath_filter", {{"value", 1.0}}}});
    CHECK(lv_subject_get_int(ts.get_chamber_filter_fan_on_subject()) == 1);
}

TEST_CASE("diagnostics objects are ignored without a configured source", "[chamber][subjects]") {
    LVGLTestFixture fixture;

    PrinterTemperatureState ts;
    ts.init_subjects(false);
    // set_chamber_diagnostics_source() intentionally NOT called — capability off.

    ts.update_from_status(faulted_diagnostics_status());

    CHECK(lv_subject_get_int(ts.get_chamber_heater_fault_subject()) == 0);
    CHECK(lv_subject_get_int(ts.get_chamber_heater_element_temp_subject()) == -1);
    CHECK(lv_subject_get_int(ts.get_chamber_filter_fan_on_subject()) == -1);
    CHECK(ts.chamber_diagnostics_object().empty());
}

TEST_CASE("chamber diagnostics subjects are XML-registered", "[chamber][xml][structural]") {
    LVGLTestFixture fixture;

    PrinterTemperatureState ts;
    ts.init_subjects(true); // register_xml=true: full production path

    for (const char* name :
         {"chamber_heater_fault", "chamber_heater_inhibited", "chamber_heater_fault_reason",
          "chamber_heater_externally_controlled", "chamber_heater_element_temp",
          "chamber_filter_fan_percent", "chamber_filter_fan_reason", "chamber_filter_fan_on"}) {
        CAPTURE(name);
        REQUIRE(lv_xml_get_subject(nullptr, name) != nullptr);
    }
    ts.deinit_subjects();

    PrinterCapabilitiesState caps;
    caps.init_subjects(true);
    REQUIRE(lv_xml_get_subject(nullptr, "printer_has_chamber_heater_diagnostics") != nullptr);
    REQUIRE(lv_xml_get_subject(nullptr, "printer_has_chamber_filter_fan") != nullptr);
    caps.deinit_subjects();
}

TEST_CASE("chamber diagnostics capabilities mirror manual resolution", "[chamber][capabilities]") {
    LVGLTestFixture fixture;

    PrinterCapabilitiesState caps;
    caps.init_subjects(false);

    REQUIRE(lv_subject_get_int(caps.get_printer_has_chamber_heater_diagnostics_subject()) == 0);
    REQUIRE(lv_subject_get_int(caps.get_printer_has_chamber_filter_fan_subject()) == 0);

    caps.set_has_chamber_heater_diagnostics(true);
    caps.set_has_chamber_filter_fan(true);
    CHECK(lv_subject_get_int(caps.get_printer_has_chamber_heater_diagnostics_subject()) == 1);
    CHECK(lv_subject_get_int(caps.get_printer_has_chamber_filter_fan_subject()) == 1);

    caps.set_has_chamber_heater_diagnostics(false);
    caps.set_has_chamber_filter_fan(false);
    CHECK(lv_subject_get_int(caps.get_printer_has_chamber_heater_diagnostics_subject()) == 0);
    CHECK(lv_subject_get_int(caps.get_printer_has_chamber_filter_fan_subject()) == 0);
}

TEST_CASE("chamber required_status_objects lists only non-empty surfaces", "[chamber][subjects]") {
    using helix::chamber::required_status_objects;

    auto both = required_status_objects("dragonbreath", "output_pin dragonbreath_filter");
    REQUIRE(both.size() == 2);
    CHECK(both[0] == "dragonbreath");
    CHECK(both[1] == "output_pin dragonbreath_filter");

    CHECK(required_status_objects("", "").empty());
    CHECK(required_status_objects("dragonbreath", "").size() == 1);
}
