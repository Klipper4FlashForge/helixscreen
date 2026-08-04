// Copyright (C) 2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "wifi_backend_wpa_supplicant.h"

#include "../catch_amalgamated.hpp"

/**
 * connect_network() used to ADD_NETWORK unconditionally on every connect. A
 * real user's wpa_supplicant had reached network id 7 for a handful of
 * networks, and duplicate all-enabled entries give wpa_supplicant more
 * candidates to roam between after a reboot — a plausible contributor to that
 * user's printer reassociating to a 5 GHz SSID they had explicitly forgotten.
 *
 * find_network_id() locates an already-saved entry for an SSID in a raw
 * LIST_NETWORKS reply so connect_network() can reuse it instead of stacking a
 * new one. It is declared outside the header's __APPLE__ guard specifically
 * so this parsing logic is unit-tested on every platform, independent of the
 * wpa_ctrl-backed call site that only builds on Linux.
 */

TEST_CASE("find_network_id locates an existing SSID", "[wifi][dedupe]") {
    const std::string reply = "network id / ssid / bssid / flags\n"
                              "0\tOldNet\tany\t[DISABLED]\n"
                              "7\tHomeNet\tany\t[CURRENT]\n";
    CHECK(helix::wifi::detail::find_network_id(reply, "HomeNet") == "7");
    CHECK(helix::wifi::detail::find_network_id(reply, "Missing").empty());
}

TEST_CASE("find_network_id does not prefix-match", "[wifi][dedupe]") {
    const std::string reply = "network id / ssid / bssid / flags\n3\tHome\tany\t[]\n";
    CHECK(helix::wifi::detail::find_network_id(reply, "HomeNet").empty());
}

TEST_CASE("find_network_id handles an SSID containing spaces", "[wifi][dedupe]") {
    // Real reporter SSIDs contain spaces; splitting on whitespace would break.
    const std::string reply = "network id / ssid / bssid / flags\n2\tmy home net\tany\t[]\n";
    CHECK(helix::wifi::detail::find_network_id(reply, "my home net") == "2");
}

TEST_CASE("find_network_id returns empty on an empty reply", "[wifi][dedupe]") {
    // send_command() returns "" when the control connection is down. That
    // must read as "no existing entry", not crash.
    CHECK(helix::wifi::detail::find_network_id("", "HomeNet").empty());
}

TEST_CASE("find_network_id returns empty on a header-only reply", "[wifi][dedupe]") {
    CHECK(helix::wifi::detail::find_network_id("network id / ssid / bssid / flags\n", "HomeNet")
              .empty());
}

TEST_CASE("find_network_id ignores an empty ssid argument", "[wifi][dedupe]") {
    const std::string reply = "network id / ssid / bssid / flags\n0\t\tany\t[DISABLED]\n";
    CHECK(helix::wifi::detail::find_network_id(reply, "").empty());
}

TEST_CASE("find_network_id matches the last line without a trailing newline", "[wifi][dedupe]") {
    const std::string reply = "network id / ssid / bssid / flags\n5\tHomeNet\tany\t[CURRENT]";
    CHECK(helix::wifi::detail::find_network_id(reply, "HomeNet") == "5");
}
