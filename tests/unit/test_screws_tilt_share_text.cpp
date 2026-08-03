// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "screws_tilt_share_text.h"

#include "../catch_amalgamated.hpp"

using helix::build_screws_tilt_share_text;
using helix::format_screw_share_adjustment;
using helix::format_screw_share_z;

namespace {

ScrewTiltResult make_screw(const char* name, float z, const char* adjustment,
                           bool is_reference = false) {
    ScrewTiltResult s;
    s.screw_name = name;
    s.z_height = z;
    s.adjustment = adjustment;
    s.is_reference = is_reference;
    return s;
}

} // namespace

TEST_CASE("format_screw_share_z renders three decimals", "[calibration][screws_tilt][share]") {
    REQUIRE(format_screw_share_z(make_screw("front_left", 2.0f, "")) == "2.000");
    REQUIRE(format_screw_share_z(make_screw("front_left", 2.0755f, "")) == "2.076");
    REQUIRE(format_screw_share_z(make_screw("front_left", -0.125f, "")) == "-0.125");
    REQUIRE(format_screw_share_z(make_screw("front_left", 0.0f, "")) == "0.000");
}

TEST_CASE("format_screw_share_adjustment marks the base screw",
          "[calibration][screws_tilt][share]") {
    SECTION("Reference screw uses the default base label") {
        auto screw = make_screw("front_left", 2.0f, "", true);
        REQUIRE(format_screw_share_adjustment(screw) == "base");
    }

    SECTION("Reference screw honours a caller-supplied (translated) label") {
        auto screw = make_screw("front_left", 2.0f, "", true);
        REQUIRE(format_screw_share_adjustment(screw, "Basis") == "Basis");
    }

    SECTION("Reference screw wins even when an adjustment string is present") {
        auto screw = make_screw("front_left", 2.0f, "CW 00:20", true);
        REQUIRE(format_screw_share_adjustment(screw) == "base");
    }

    SECTION("Normal screw passes the adjustment through verbatim") {
        REQUIRE(format_screw_share_adjustment(make_screw("rear_right", 2.1f, "CCW 01:15")) ==
                "CCW 01:15");
    }

    SECTION("Empty adjustment on a non-reference screw becomes a placeholder") {
        REQUIRE(format_screw_share_adjustment(make_screw("rear_right", 2.1f, "")) == "--");
    }

    SECTION("Whitespace is trimmed") {
        REQUIRE(format_screw_share_adjustment(make_screw("rear_right", 2.1f, "  CW 00:05  ")) ==
                "CW 00:05");
    }

    SECTION("Whitespace-only adjustment is treated as empty") {
        REQUIRE(format_screw_share_adjustment(make_screw("rear_right", 2.1f, "   ")) == "--");
    }
}

TEST_CASE("build_screws_tilt_share_text serializes the full result set",
          "[calibration][screws_tilt][share]") {
    SECTION("One line per screw, base screw marked") {
        std::vector<ScrewTiltResult> results = {
            make_screw("front_left", 2.075f, "", true),
            make_screw("front_right", 2.100f, "CW 00:05"),
            make_screw("rear_right", 1.950f, "CCW 01:30"),
        };

        const std::string text = build_screws_tilt_share_text(results);

        REQUIRE(text == "HelixScreen bed screw results\n"
                        "Front Left: z=2.075 base\n"
                        "Front Right: z=2.100 CW 00:05\n"
                        "Rear Right: z=1.950 CCW 01:30");
    }

    SECTION("No trailing newline") {
        auto text = build_screws_tilt_share_text({make_screw("front_left", 1.0f, "CW 00:05")});
        REQUIRE(text.back() != '\n');
    }

    SECTION("Empty result set still identifies itself") {
        REQUIRE(build_screws_tilt_share_text({}) == "HelixScreen bed screw results\n(no results)");
    }

    SECTION("Payload stays well inside QR byte capacity for a 6-screw bed") {
        std::vector<ScrewTiltResult> results;
        for (int i = 0; i < 6; i++) {
            results.push_back(make_screw("rear_right", 2.125f, "CCW 01:30"));
        }
        // Version-10 byte-mode QR at ECC-M holds 271 bytes; stay under it.
        REQUIRE(build_screws_tilt_share_text(results).size() < 271);
    }

    SECTION("Screw names are prettified, not raw snake_case") {
        auto text = build_screws_tilt_share_text({make_screw("rear_left", 2.0f, "CW 00:10")});
        REQUIRE(text.find("Rear Left:") != std::string::npos);
        REQUIRE(text.find("rear_left") == std::string::npos);
    }
}
