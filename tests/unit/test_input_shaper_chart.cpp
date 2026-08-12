// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_input_shaper_chart.cpp
 * @brief Unit tests for input shaper frequency response chart data flow
 *
 * Tests the data pipeline from CSV calibration data through to
 * InputShaperResult structures used by the comparison table and chart.
 * Verifies CSV parsing populates freq_response/shaper_curves correctly,
 * peak detection, recommended shaper identification, and edge cases.
 */

#include "../../include/calibration_types.h"
#include "../../include/shaper_csv_parser.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "../catch_amalgamated.hpp"

using namespace helix::calibration;
namespace fs = std::filesystem;

// ============================================================================
// Test Helpers
// ============================================================================

namespace {

/// RAII temp file that auto-deletes on destruction
struct TempCsvFile {
    std::string path;

    explicit TempCsvFile(const std::string& content) {
        path = (fs::temp_directory_path() / ("test_is_chart_" +
                                             std::to_string(std::hash<std::string>{}(content) ^
                                                            reinterpret_cast<uintptr_t>(this)) +
                                             ".csv"))
                   .string();
        std::ofstream out(path);
        out << content;
    }

    ~TempCsvFile() {
        std::remove(path.c_str());
    }
};

/// Realistic CSV content matching Klipper's calibrate_shaper.py output (real format, no marker)
const char* REALISTIC_CSV =
    "freq, psd_x, psd_y, psd_z, psd_xyz, zv(59.0), mzv(53.8), "
    "ei(56.2), 2hump_ei(71.8), 3hump_ei(89.6)\n"
    "5.0, 1.234e-03, 2.345e-03, 1.123e-03, 4.702e-03, 0.001, 0.001, 0.001, 0.000, 0.000\n"
    "10.0, 2.500e-03, 3.100e-03, 1.800e-03, 7.400e-03, 0.002, 0.002, 0.002, 0.001, 0.001\n"
    "15.0, 4.100e-03, 5.200e-03, 2.900e-03, 1.220e-02, 0.004, 0.003, 0.004, 0.002, 0.001\n"
    "20.0, 8.700e-03, 1.020e-02, 5.600e-03, 2.450e-02, 0.009, 0.007, 0.008, 0.004, 0.003\n"
    "25.0, 1.500e-02, 1.800e-02, 9.200e-03, 4.220e-02, 0.016, 0.012, 0.014, 0.008, 0.005\n"
    "30.0, 3.200e-02, 4.100e-02, 2.100e-02, 9.400e-02, 0.035, 0.026, 0.030, 0.017, 0.011\n"
    "35.0, 6.800e-02, 8.500e-02, 4.200e-02, 1.950e-01, 0.074, 0.055, 0.063, 0.036, 0.024\n"
    "40.0, 1.200e-01, 1.500e-01, 7.800e-02, 3.480e-01, 0.130, 0.098, 0.112, 0.065, 0.043\n"
    "45.0, 2.100e-01, 2.800e-01, 1.400e-01, 6.300e-01, 0.228, 0.171, 0.196, 0.113, 0.075\n"
    "50.0, 3.500e-01, 4.200e-01, 2.100e-01, 9.800e-01, 0.380, 0.285, 0.327, 0.189, 0.126\n"
    "55.0, 2.800e-01, 3.600e-01, 1.700e-01, 8.100e-01, 0.304, 0.228, 0.261, 0.151, 0.101\n"
    "60.0, 1.500e-01, 2.000e-01, 9.500e-02, 4.450e-01, 0.163, 0.122, 0.140, 0.081, 0.054\n";

/**
 * @brief Build an InputShaperResult from parsed CSV data (mirrors collector logic)
 *
 * This replicates the data flow in InputShaperCollector::finalize() where
 * CSV data is parsed and merged into the result struct.
 */
InputShaperResult build_result_from_csv(const ShaperCsvData& csv_data, char axis) {
    InputShaperResult result;
    result.axis = axis;

    // Populate freq_response pairs from CSV data (same as collector)
    result.freq_response.reserve(csv_data.frequencies.size());
    for (size_t i = 0; i < csv_data.frequencies.size(); ++i) {
        result.freq_response.emplace_back(csv_data.frequencies[i],
                                          i < csv_data.raw_psd.size() ? csv_data.raw_psd[i] : 0.0f);
    }

    // Move shaper curves
    result.shaper_curves = csv_data.shaper_curves;

    return result;
}

} // anonymous namespace

// ============================================================================
// Test 1: CSV data populates freq_response in result
// ============================================================================

TEST_CASE("CSV data populates freq_response in result", "[input_shaper_chart]") {
    TempCsvFile csv(REALISTIC_CSV);
    auto csv_data = parse_shaper_csv(csv.path, 'X');
    auto result = build_result_from_csv(csv_data, 'X');

    REQUIRE(result.freq_response.size() == 12);

    SECTION("frequency values match CSV") {
        CHECK(result.freq_response[0].first == Catch::Approx(5.0f));
        CHECK(result.freq_response[5].first == Catch::Approx(30.0f));
        CHECK(result.freq_response[11].first == Catch::Approx(60.0f));
    }

    SECTION("PSD amplitude values match psd_x column") {
        CHECK(result.freq_response[0].second == Catch::Approx(1.234e-03f));
        CHECK(result.freq_response[7].second == Catch::Approx(1.200e-01f));
        CHECK(result.freq_response[9].second == Catch::Approx(3.500e-01f));
    }

    SECTION("has_freq_data returns true") {
        REQUIRE(result.has_freq_data());
    }
}

// ============================================================================
// Test 2: CSV data populates shaper_curves in result
// ============================================================================

TEST_CASE("CSV data populates shaper_curves in result", "[input_shaper_chart]") {
    TempCsvFile csv(REALISTIC_CSV);
    auto csv_data = parse_shaper_csv(csv.path, 'X');
    auto result = build_result_from_csv(csv_data, 'X');

    REQUIRE(result.shaper_curves.size() == 5);

    SECTION("shaper names match expected order") {
        CHECK(result.shaper_curves[0].name == "zv");
        CHECK(result.shaper_curves[1].name == "mzv");
        CHECK(result.shaper_curves[2].name == "ei");
        CHECK(result.shaper_curves[3].name == "2hump_ei");
        CHECK(result.shaper_curves[4].name == "3hump_ei");
    }

    SECTION("shaper fitted frequencies are populated") {
        CHECK(result.shaper_curves[0].frequency == Catch::Approx(59.0f));
        CHECK(result.shaper_curves[1].frequency == Catch::Approx(53.8f));
        CHECK(result.shaper_curves[2].frequency == Catch::Approx(56.2f));
        CHECK(result.shaper_curves[3].frequency == Catch::Approx(71.8f));
        CHECK(result.shaper_curves[4].frequency == Catch::Approx(89.6f));
    }

    SECTION("shaper curve values have same row count as freq_response") {
        for (const auto& curve : result.shaper_curves) {
            CHECK(curve.values.size() == result.freq_response.size());
        }
    }

    SECTION("shaper curve values are non-negative") {
        for (const auto& curve : result.shaper_curves) {
            for (float val : curve.values) {
                CHECK(val >= 0.0f);
            }
        }
    }
}

// ============================================================================
// Test 3: Peak detection from freq_response
// ============================================================================

TEST_CASE("Peak PSD value found from freq_response", "[input_shaper_chart]") {
    TempCsvFile csv(REALISTIC_CSV);
    auto csv_data = parse_shaper_csv(csv.path, 'X');
    auto result = build_result_from_csv(csv_data, 'X');

    REQUIRE_FALSE(result.freq_response.empty());

    // Find peak amplitude (same logic as populate_chart)
    auto peak_it =
        std::max_element(result.freq_response.begin(), result.freq_response.end(),
                         [](const auto& a, const auto& b) { return a.second < b.second; });

    REQUIRE(peak_it != result.freq_response.end());

    SECTION("peak frequency is at 50 Hz") {
        // From the CSV data, psd_x is highest at 50 Hz (0.350)
        CHECK(peak_it->first == Catch::Approx(50.0f));
    }

    SECTION("peak amplitude matches expected value") {
        CHECK(peak_it->second == Catch::Approx(3.500e-01f));
    }

    SECTION("peak is greater than all other amplitudes") {
        for (const auto& [freq, amp] : result.freq_response) {
            CHECK(amp <= peak_it->second);
        }
    }
}

// ============================================================================
// Test 4: Recommended shaper identification
// ============================================================================

TEST_CASE("Recommended shaper is identified from all_shapers", "[input_shaper_chart]") {
    // Build a result with all_shapers populated (as mock/collector would)
    InputShaperResult result;
    result.axis = 'X';
    result.shaper_type = "mzv"; // Recommended by Klipper
    result.shaper_freq = 53.8f;

    ShaperOption zv;
    zv.type = "zv";
    zv.frequency = 59.0f;
    zv.vibrations = 5.2f;
    zv.smoothing = 0.045f;
    zv.max_accel = 13400.0f;

    ShaperOption mzv;
    mzv.type = "mzv";
    mzv.frequency = 53.8f;
    mzv.vibrations = 1.6f;
    mzv.smoothing = 0.130f;
    mzv.max_accel = 4000.0f;

    ShaperOption ei;
    ei.type = "ei";
    ei.frequency = 56.2f;
    ei.vibrations = 0.7f;
    ei.smoothing = 0.120f;
    ei.max_accel = 4600.0f;

    ShaperOption two_hump;
    two_hump.type = "2hump_ei";
    two_hump.frequency = 71.8f;
    two_hump.vibrations = 0.0f;
    two_hump.smoothing = 0.260f;
    two_hump.max_accel = 8800.0f;

    ShaperOption three_hump;
    three_hump.type = "3hump_ei";
    three_hump.frequency = 89.6f;
    three_hump.vibrations = 0.0f;
    three_hump.smoothing = 0.350f;
    three_hump.max_accel = 8800.0f;

    result.all_shapers = {zv, mzv, ei, two_hump, three_hump};

    SECTION("recommended shaper matches result.shaper_type") {
        // Find the recommended shaper in all_shapers
        auto it =
            std::find_if(result.all_shapers.begin(), result.all_shapers.end(),
                         [&](const ShaperOption& opt) { return opt.type == result.shaper_type; });
        REQUIRE(it != result.all_shapers.end());
        CHECK(it->type == "mzv");
        CHECK(it->frequency == Catch::Approx(53.8f));
    }

    SECTION("recommended shaper frequency matches result.shaper_freq") {
        auto it =
            std::find_if(result.all_shapers.begin(), result.all_shapers.end(),
                         [&](const ShaperOption& opt) { return opt.type == result.shaper_type; });
        REQUIRE(it != result.all_shapers.end());
        CHECK(it->frequency == Catch::Approx(result.shaper_freq));
    }

    SECTION("recommended shaper has lower vibrations than zv") {
        auto rec_it =
            std::find_if(result.all_shapers.begin(), result.all_shapers.end(),
                         [&](const ShaperOption& opt) { return opt.type == result.shaper_type; });
        auto zv_it = std::find_if(result.all_shapers.begin(), result.all_shapers.end(),
                                  [](const ShaperOption& opt) { return opt.type == "zv"; });
        REQUIRE(rec_it != result.all_shapers.end());
        REQUIRE(zv_it != result.all_shapers.end());
        CHECK(rec_it->vibrations < zv_it->vibrations);
    }
}

// ============================================================================
// Test 5: Empty CSV path produces empty freq_response
// ============================================================================

TEST_CASE("Empty CSV path produces empty freq_response", "[input_shaper_chart]") {
    SECTION("nonexistent file path") {
        auto csv_data = parse_shaper_csv("/tmp/nonexistent_chart_test.csv", 'X');
        auto result = build_result_from_csv(csv_data, 'X');

        CHECK(result.freq_response.empty());
        CHECK(result.shaper_curves.empty());
        CHECK_FALSE(result.has_freq_data());
    }

    SECTION("empty string path") {
        auto csv_data = parse_shaper_csv("", 'X');
        auto result = build_result_from_csv(csv_data, 'X');

        CHECK(result.freq_response.empty());
        CHECK(result.shaper_curves.empty());
        CHECK_FALSE(result.has_freq_data());
    }

    SECTION("empty file content") {
        TempCsvFile csv("");
        auto csv_data = parse_shaper_csv(csv.path, 'X');
        auto result = build_result_from_csv(csv_data, 'X');

        CHECK(result.freq_response.empty());
        CHECK_FALSE(result.has_freq_data());
    }
}

// ============================================================================
// Test 6: Shaper curves match expected count (5 standard Klipper shapers)
// ============================================================================

TEST_CASE("Shaper curves match expected count from standard Klipper CSV", "[input_shaper_chart]") {
    TempCsvFile csv(REALISTIC_CSV);
    auto csv_data = parse_shaper_csv(csv.path, 'X');
    auto result = build_result_from_csv(csv_data, 'X');

    SECTION("5 shaper curves from standard Klipper output") {
        REQUIRE(result.shaper_curves.size() == 5);
    }

    SECTION("standard Klipper shaper types present") {
        std::vector<std::string> expected_types = {"zv", "mzv", "ei", "2hump_ei", "3hump_ei"};
        REQUIRE(result.shaper_curves.size() == expected_types.size());
        for (size_t i = 0; i < expected_types.size(); ++i) {
            CHECK(result.shaper_curves[i].name == expected_types[i]);
        }
    }

    SECTION("each shaper curve has a positive fitted frequency") {
        for (const auto& curve : result.shaper_curves) {
            INFO("Checking shaper: " << curve.name);
            CHECK(curve.frequency > 0.0f);
        }
    }

    SECTION("all shaper curves have same number of data points as freq bins") {
        size_t expected_bins = result.freq_response.size();
        REQUIRE(expected_bins == 12);
        for (const auto& curve : result.shaper_curves) {
            INFO("Checking shaper: " << curve.name);
            CHECK(curve.values.size() == expected_bins);
        }
    }
}
