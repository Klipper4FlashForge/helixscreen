// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Compile-only drift protection for WifiBackend.

#include "wifi_backend.h"

#include "../catch_amalgamated.hpp"

#ifdef HELIX_ENABLE_MOCKS
#include "wifi_backend_mock.h"

TEST_CASE("WifiBackendMock satisfies WifiBackend interface", "[compile][drift]") {
    std::unique_ptr<WifiBackend> p = std::make_unique<WifiBackendMock>();
    REQUIRE(p != nullptr);
    // Exercise every pure-virtual. The body isn't semantically tested — the point
    // is to force every override to be present and callable through the base pointer.
    (void)p->is_running();
    (void)p->supports_5ghz();
    p->register_event_callback("SCAN_COMPLETE", [](const std::string&) {});
    (void)p->trigger_scan();
    std::vector<WiFiNetwork> networks;
    (void)p->get_scan_results(networks);
    (void)p->connect_network("test-ssid", "test-password");
    (void)p->disconnect_network();
    (void)p->get_status();
    (void)p->start();
    p->stop();
}

TEST_CASE("Mock backend tracks radio state independently of running state", "[wifi][radio]") {
    WifiBackendMock backend;
    REQUIRE(backend.start().success());

    CHECK(backend.is_radio_enabled());

    REQUIRE(backend.set_radio_enabled(false).success());
    CHECK_FALSE(backend.is_radio_enabled());
    // Radio off must NOT tear the backend down — the control path stays up so
    // state can be reported and the radio switched back on.
    CHECK(backend.is_running());

    REQUIRE(backend.set_radio_enabled(true).success());
    CHECK(backend.is_radio_enabled());
}
#endif
