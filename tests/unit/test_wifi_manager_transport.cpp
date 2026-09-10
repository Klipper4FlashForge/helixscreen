// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * WiFiManager's handling of a backend that refuses a join with
 * TRANSPORT_IN_USE (netd single-transport: Ethernet holds the link and the
 * daemon never answers a Wi-Fi join). The manager is where the refusal gets
 * its user-facing translation - the backends stay free of the translation
 * layer - so the mapping is pinned here with the mock's refusal knob.
 */

#include "../test_fixtures.h"
#include "wifi_backend.h"
#include "wifi_backend_mock.h"
#include "wifi_manager.h"

#include <memory>
#include <string>

#include "../catch_amalgamated.hpp"

TEST_CASE_METHOD(LVGLTestFixture,
                 "WiFiManager translates a TRANSPORT_IN_USE refusal into the user message",
                 "[1542][wifi]") {
    auto backend = std::make_unique<WifiBackendMock>();
    REQUIRE(backend->start().success());
    backend->set_transport_in_use_for_test(true);

    helix::WiFiManager manager(std::move(backend), /*silent=*/true);

    bool success = true;
    std::string error;
    manager.connect("SomeNet", "pw", [&](bool ok, const std::string& msg) {
        success = ok;
        error = msg;
    });

    REQUIRE_FALSE(success);
    REQUIRE(error == lv_tr("Ethernet is connected. Disconnect it to join a Wi-Fi network."));
}

TEST_CASE_METHOD(LVGLTestFixture,
                 "WiFiManager passes through an ordinary backend refusal untouched",
                 "[1542][wifi]") {
    auto backend = std::make_unique<WifiBackendMock>();
    REQUIRE(backend->start().success());
    // Transport knob OFF: the mock's normal behavior runs (an unknown SSID
    // refuses with NETWORK_NOT_FOUND), and the manager must not substitute
    // the transport message for it.
    backend->set_transport_in_use_for_test(false);

    helix::WiFiManager manager(std::move(backend), /*silent=*/true);

    bool success = true;
    std::string error;
    manager.connect("NotInScanResults", "", [&](bool ok, const std::string& msg) {
        success = ok;
        error = msg;
    });

    REQUIRE_FALSE(success);
    REQUIRE_FALSE(error == lv_tr("Ethernet is connected. Disconnect it to join a Wi-Fi network."));
}
