// SPDX-License-Identifier: GPL-3.0-or-later
#include "detect_printer_cmd.h"
#include "printer_detector.h"
#include "printer_discovery.h"

#include "catch_amalgamated.hpp"
#include "hv/json.hpp"

TEST_CASE("format_detect_verdict: confident match with runner-up", "[detect_cmd]") {
    PrinterDetectionResult r{"Qidi Q2", 90, "chamber"};
    r.preset = "qidi_q2";
    r.runner_up_type_name = "Qidi Q1 Pro";
    r.runner_up_confidence = 70;
    std::string json = helix::detect::format_detect_verdict(r, "qidi_q1_pro");
    REQUIRE(json.find("\"model\":\"Qidi Q2\"") != std::string::npos);
    REQUIRE(json.find("\"preset\":\"qidi_q2\"") != std::string::npos);
    REQUIRE(json.find("\"confidence\":90") != std::string::npos);
    REQUIRE(json.find("\"runner_up_preset\":\"qidi_q1_pro\"") != std::string::npos);
    REQUIRE(json.find("\"runner_up_confidence\":70") != std::string::npos);
}

TEST_CASE("format_detect_verdict: no preset and no runner-up emit null", "[detect_cmd]") {
    PrinterDetectionResult r{"Mystery Printer", 40, "corexy"};
    std::string json = helix::detect::format_detect_verdict(r, "");
    REQUIRE(json.find("\"preset\":null") != std::string::npos);
    REQUIRE(json.find("\"runner_up_preset\":null") != std::string::npos);
    REQUIRE(json.find("\"runner_up_confidence\":0") != std::string::npos);
}

// Regression: Voron 2.4 returns toolhead.kinematics=null — must not throw.
// Kinematics must be read from configfile.settings.printer.kinematics instead.
TEST_CASE("populate_discovery: null kinematics does not throw", "[detect_cmd]") {
    helix::PrinterDiscovery disc;
    nlohmann::json objects = nlohmann::json::array({"extruder", "heater_bed", "quad_gantry_level"});
    nlohmann::json info = {{"hostname", "voronv2"}};
    // configfile status with kinematics in the static printer config section
    nlohmann::json cfg = {{"configfile",
                           {{"settings",
                             {{"printer", {{"kinematics", "corexy"}}},
                              {"stepper_x", {{"position_min", 0}, {"position_max", 350}}},
                              {"stepper_y", {{"position_min", 0}, {"position_max", 350}}},
                              {"stepper_z", {{"position_max", 340}}}}}}}};
    REQUIRE_NOTHROW(helix::detect::populate_discovery(disc, objects, info, cfg));
    REQUIRE(disc.hostname() == "voronv2");
    REQUIRE(disc.kinematics() == "corexy");
}

// Regression: null/missing fields must all be skipped without throwing.
TEST_CASE("populate_discovery: missing/null fields are skipped safely", "[detect_cmd]") {
    helix::PrinterDiscovery disc;
    nlohmann::json objects = nlohmann::json::array({"extruder"});
    nlohmann::json info = {{"hostname", nullptr}}; // null hostname
    nlohmann::json cfg = {{"configfile",
                           {{"settings",
                             {
                                 {"printer", {{"kinematics", nullptr}}} // null kinematics
                             }}}}};
    REQUIRE_NOTHROW(helix::detect::populate_discovery(disc, objects, info, cfg));
    // No throw is the primary regression guard; hostname/kinematics remain empty.
    REQUIRE(disc.hostname().empty());
    REQUIRE(disc.kinematics().empty());
}

// Regression: an info object that omits "hostname" entirely is distinct from one
// carrying a null hostname. The const json operator[] does not insert on a miss —
// it is only JSON_ASSERT-guarded, so a missing key aborts where asserts are live
// and is undefined behaviour where they are not. Reading via find() is the only
// safe form on a const reference.
TEST_CASE("populate_discovery: info object without a hostname key is safe", "[detect_cmd]") {
    helix::PrinterDiscovery disc;
    nlohmann::json objects = nlohmann::json::array({"extruder"});
    nlohmann::json info = {{"software_version", "v0.12.0"}}; // object, but no hostname
    nlohmann::json cfg = nlohmann::json::object();
    REQUIRE_NOTHROW(helix::detect::populate_discovery(disc, objects, info, cfg));
    REQUIRE(disc.hostname().empty());
}

// Klipper's /printer/info is fetched over the network, so the result is not
// guaranteed to be an object at all. A scalar must be skipped, not indexed:
// the const operator[] throws type_error.305 on a non-object.
TEST_CASE("populate_discovery: non-object info is skipped", "[detect_cmd]") {
    helix::PrinterDiscovery disc;
    nlohmann::json objects = nlohmann::json::array({"extruder"});
    nlohmann::json cfg = nlohmann::json::object();
    REQUIRE_NOTHROW(helix::detect::populate_discovery(disc, objects, nlohmann::json("oops"), cfg));
    REQUIRE(disc.hostname().empty());
}
