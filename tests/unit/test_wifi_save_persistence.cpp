// Copyright (C) 2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#if !defined(__APPLE__) && !defined(__ANDROID__)

#include "../../include/wifi_backend_wpa_supplicant.h"

#include "../catch_amalgamated.hpp"

/**
 * WiFi credential persistence verification.
 *
 * Device-verified on a Snapmaker U1 (2026-07-29): wpa_supplicant answers
 * SAVE_CONFIG with "OK" and never writes the config file. Reply "OK", config
 * mtime unchanged, zero `network={` blocks afterwards, and the path provably
 * writable (a direct append succeeded). The credentials lived in the daemon's
 * memory only, so every power-off silently lost the user's WiFi — matching the
 * field report "WiFi disconnects every time I turn off the printer".
 *
 * The old code took `save_result == "OK\n"` as proof and logged "Configuration
 * saved to disk". These tests pin the rule that replaced it: the config file's
 * contents are the only authority on whether credentials will survive a reboot.
 */

using helix::wifi::detail::classify_save_result;
using helix::wifi::detail::SavePersistence;
using helix::wifi::detail::wpa_config_has_network;

namespace {

// A wpa_supplicant config the way the U1 leaves it: headers, no networks.
constexpr const char* kEmptyConfig = "ctrl_interface=/var/run/wpa_supplicant\n"
                                     "ap_scan=1\n"
                                     "update_config=1\n";

constexpr const char* kConfigWithNetwork = "ctrl_interface=/var/run/wpa_supplicant\n"
                                           "ap_scan=1\n"
                                           "update_config=1\n"
                                           "\n"
                                           "network={\n"
                                           "\tssid=\"HomeNet\"\n"
                                           "\tpsk=\"secretpass\"\n"
                                           "}\n";

} // namespace

TEST_CASE("SAVE_CONFIG OK with an unwritten config is not persistence",
          "[network][wpa][persistence][regression]") {
    // THE U1 REGRESSION. If this ever returns Persisted again, HelixScreen is
    // back to telling users their WiFi is saved when it is not.
    REQUIRE(classify_save_result("OK\n", kEmptyConfig, "HomeNet") ==
            SavePersistence::NotPersisted);
}

TEST_CASE("SAVE_CONFIG persistence classification", "[network][wpa][persistence]") {
    SECTION("OK plus the SSID actually on disk is persisted") {
        REQUIRE(classify_save_result("OK\n", kConfigWithNetwork, "HomeNet") ==
                SavePersistence::Persisted);
    }

    SECTION("explicit FAIL is not persisted") {
        REQUIRE(classify_save_result("FAIL\n", kConfigWithNetwork, "HomeNet") ==
                SavePersistence::NotPersisted);
    }

    SECTION("empty reply is not persisted") {
        REQUIRE(classify_save_result("", kConfigWithNetwork, "HomeNet") ==
                SavePersistence::NotPersisted);
    }

    SECTION("a different SSID on disk does not count as ours") {
        REQUIRE(classify_save_result("OK\n", kConfigWithNetwork, "SomeOtherNet") ==
                SavePersistence::NotPersisted);
    }

    SECTION("OK without a trailing newline still parses") {
        REQUIRE(classify_save_result("OK", kConfigWithNetwork, "HomeNet") ==
                SavePersistence::Persisted);
    }
}

TEST_CASE("wpa_config_has_network matches whole SSID tokens", "[network][wpa][persistence]") {
    SECTION("exact match") {
        REQUIRE(wpa_config_has_network(kConfigWithNetwork, "HomeNet"));
    }

    SECTION("a prefix of a longer SSID must not match") {
        // ssid="HomeNet" on disk must not satisfy a request for "Home".
        REQUIRE_FALSE(wpa_config_has_network(kConfigWithNetwork, "Home"));
    }

    SECTION("a longer SSID than the one on disk must not match") {
        REQUIRE_FALSE(wpa_config_has_network(kConfigWithNetwork, "HomeNetExtra"));
    }

    SECTION("scan_ssid= must not be mistaken for ssid=") {
        const std::string cfg = "network={\n\tscan_ssid=\"Decoy\"\n\tssid=\"Real\"\n}\n";
        REQUIRE_FALSE(wpa_config_has_network(cfg, "Decoy"));
        REQUIRE(wpa_config_has_network(cfg, "Real"));
    }

    SECTION("empty SSID never matches") {
        REQUIRE_FALSE(wpa_config_has_network(kConfigWithNetwork, ""));
        REQUIRE_FALSE(wpa_config_has_network(kEmptyConfig, ""));
    }

    SECTION("empty config never matches") {
        REQUIRE_FALSE(wpa_config_has_network("", "HomeNet"));
    }

    SECTION("multiple networks — finds the one asked for") {
        const std::string cfg = "network={\n\tssid=\"First\"\n}\n"
                                "network={\n\tssid=\"Second\"\n}\n";
        REQUIRE(wpa_config_has_network(cfg, "First"));
        REQUIRE(wpa_config_has_network(cfg, "Second"));
        REQUIRE_FALSE(wpa_config_has_network(cfg, "Third"));
    }

    SECTION("SSID containing spaces") {
        const std::string cfg = "network={\n\tssid=\"My Home Net\"\n}\n";
        REQUIRE(wpa_config_has_network(cfg, "My Home Net"));
    }
}

#endif // !__APPLE__ && !__ANDROID__
