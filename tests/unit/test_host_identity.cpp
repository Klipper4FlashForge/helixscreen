// Copyright (C) 2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "host_identity.h"

#include <cctype>
#include <string>
#include <unistd.h>

#include "../catch_amalgamated.hpp"

TEST_CASE("host_identity — localhost strings", "[host_identity]") {
    REQUIRE(helix::is_moonraker_on_same_host("localhost"));
    REQUIRE(helix::is_moonraker_on_same_host("127.0.0.1"));
    REQUIRE(helix::is_moonraker_on_same_host("::1"));
    REQUIRE(helix::is_moonraker_on_same_host(""));
}

TEST_CASE("host_identity — gethostname matches", "[host_identity]") {
    char buf[256] = {};
    REQUIRE(gethostname(buf, sizeof(buf)) == 0);
    REQUIRE(helix::is_moonraker_on_same_host(buf));

    std::string upper = buf;
    for (auto& c : upper)
        c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
    REQUIRE(helix::is_moonraker_on_same_host(upper));
}

TEST_CASE("host_identity — local interface IP matches", "[host_identity]") {
    REQUIRE(helix::is_moonraker_on_same_host("127.0.0.1"));
}

TEST_CASE("host_identity — clearly remote is not same-host", "[host_identity]") {
    REQUIRE_FALSE(helix::is_moonraker_on_same_host("192.0.2.1"));
    REQUIRE_FALSE(helix::is_moonraker_on_same_host("printer.invalid"));
}

TEST_CASE("extract_host_from_websocket_url parses scheme URLs", "[host_identity]") {
    REQUIRE(helix::extract_host_from_websocket_url("ws://192.168.1.100:7125/websocket") ==
            "192.168.1.100");
    REQUIRE(helix::extract_host_from_websocket_url("wss://printer.local:7125/websocket") ==
            "printer.local");
    REQUIRE(helix::extract_host_from_websocket_url("ws://[::1]:7125/websocket") == "::1");
    REQUIRE(helix::extract_host_from_websocket_url("ws://myhost/websocket") == "myhost");
    REQUIRE(helix::extract_host_from_websocket_url("ws://10.0.0.5:7125") == "10.0.0.5");
    REQUIRE(helix::extract_host_from_websocket_url("").empty());
    REQUIRE(helix::extract_host_from_websocket_url("http://192.168.1.100:8080/")
                .empty()); // unknown scheme
}
