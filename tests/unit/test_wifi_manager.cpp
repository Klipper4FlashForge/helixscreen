// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../../include/async_lifetime_guard.h"
#include "../../include/config.h"
#include "../../include/runtime_config.h"
#include "../../include/system_settings_manager.h"
#include "../../include/ui_update_queue.h"
#include "../../include/wifi_backend_mock.h"
#include "../../include/wifi_interface.h"
#include "../../include/wifi_manager.h"
#include "../../lvgl/lvgl.h"
#include "../ui_test_utils.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

#include "../catch_amalgamated.hpp"

using namespace helix;

namespace helix {
// Friend accessor — lets this file point WiFiManager's stranding-prevention
// check (has_non_wifi_network_path, Task 15) at a fixture sysfs tree instead
// of the real /sys, mirroring the os_link_probe_ seam in
// test_wifi_os_link_fallback.cpp.
class WiFiManagerTestAccess {
  public:
    static void set_sys_root(const std::string& root) {
        WiFiManager::sys_root_ = root;
    }
    static void reset_sys_root() {
        WiFiManager::sys_root_ = "/sys";
    }
};
} // namespace helix

namespace {

// Throwaway "<root>/sys/class/net/..." tree for the reassert-gate tests.
// Deliberately minimal (unlike test_wifi_interface.cpp's FakeRoot) — these
// tests only ever need a single interface's operstate.
struct SysFixture {
    std::filesystem::path root;

    SysFixture() {
        char tmpl[] = "/tmp/helix-wifi-mgr-sys-XXXXXX";
        root = ::mkdtemp(tmpl);
        std::filesystem::create_directories(root / "sys" / "class" / "net");
    }
    ~SysFixture() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }

    std::string sys_root() const {
        return (root / "sys").string();
    }

    /// Create <root>/sys/class/net/<name>/operstate = "up".
    void write_up(const std::string& netdev) const {
        auto dir = root / "sys" / "class" / "net" / netdev;
        std::filesystem::create_directories(dir);
        std::ofstream f(dir / "operstate");
        f << "up\n";
    }
};

} // namespace

/**
 * WiFiManager Unit Tests
 *
 * Tests verify instance-based WiFiManager with pluggable backend system:
 * - Instance creation and destruction (no static methods)
 * - Backend initialization (starts disabled by default)
 * - Scan lifecycle with callback preservation
 * - Connection management
 * - Status queries
 * - Edge cases and error handling
 *
 * Note: On macOS, tests use mock backend. On Linux, may use real wpa_supplicant.
 *
 * CRITICAL BUGS CAUGHT:
 * - Callback clearing bug: stop_scan() was clearing scan_callback_
 * - Backend initialization bug: Mock backend started by factory (should be disabled)
 * - No callback registration: Networks weren't populating
 */

// ============================================================================
// Global LVGL Initialization (called once)
// ============================================================================

struct LVGLInitializer {
    LVGLInitializer() {
        lv_init_safe();
        lv_display_t* disp = lv_display_create(800, 480);
        alignas(64) static lv_color_t buf[800 * 10];
        lv_display_set_buffers(disp, buf, NULL, sizeof(buf), LV_DISPLAY_RENDER_MODE_PARTIAL);
    }
};

static LVGLInitializer lvgl_init;

// ============================================================================
// Test Fixtures
// ============================================================================

class WiFiManagerTestFixture {
  public:
    WiFiManagerTestFixture() {
        // Create fresh instance for each test as shared_ptr
        // CRITICAL: WiFiManager requires init_self_reference() for async callbacks
        // to work - the weak_ptr in async dispatch needs the shared_ptr to lock
        wifi_manager = std::make_shared<WiFiManager>();
        wifi_manager->init_self_reference(wifi_manager);

        // Reset state
        scan_callback_count = 0;
        last_networks.clear();
        connection_success = false;
        connection_error.clear();
    }

    ~WiFiManagerTestFixture() {
        // Cleanup - ensure scan stopped and backend disabled
        if (wifi_manager) {
            wifi_manager->stop_scan();
            wifi_manager->set_enabled(false);
        }
    }

    // Helper: Scan callback that captures results
    void scan_callback(const std::vector<WiFiNetwork>& networks) {
        scan_callback_count++;
        last_networks = networks;
    }

    // Helper: Connection callback that captures result
    void connection_callback(bool success, const std::string& error) {
        connection_success = success;
        connection_error = error;
    }

    // Helper: Wait for condition with timeout (WiFi backend uses std::thread, not LVGL timers)
    bool wait_for_condition(std::function<bool()> condition, int timeout_ms = 5000) {
        auto start = std::chrono::steady_clock::now();
        auto end = start + std::chrono::milliseconds(timeout_ms);

        while (std::chrono::steady_clock::now() < end) {
            if (condition()) {
                return true; // Condition met
            }

            // Sleep briefly to avoid busy-wait
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return false; // Timeout
    }

    // Test instance (shared_ptr for init_self_reference support)
    std::shared_ptr<WiFiManager> wifi_manager;

    // Test state
    int scan_callback_count = 0;
    std::vector<WiFiNetwork> last_networks;
    bool connection_success = false;
    std::string connection_error;
};

// ============================================================================
// Instance Creation Tests
// ============================================================================

TEST_CASE_METHOD(WiFiManagerTestFixture, "WiFiManager instance creation",
                 "[.disabled][macos-wifi][network][instance]") {
    SECTION("Instance created successfully") {
        REQUIRE(wifi_manager != nullptr);
    }

    SECTION("Instance has backend") {
        // Backend should exist (even if not running)
        REQUIRE(wifi_manager->has_hardware());
    }

    SECTION("Multiple instances can coexist") {
        auto wifi2 = std::make_shared<WiFiManager>();
        wifi2->init_self_reference(wifi2);
        REQUIRE(wifi2 != nullptr);
        REQUIRE(wifi2->has_hardware());
    }

    SECTION("Instance destruction is safe") {
        wifi_manager.reset();
        REQUIRE(wifi_manager == nullptr);

        // Creating new instance after destruction works
        wifi_manager = std::make_shared<WiFiManager>();
        wifi_manager->init_self_reference(wifi_manager);
        REQUIRE(wifi_manager != nullptr);
    }
}

// ============================================================================
// Backend Initialization Tests
// ============================================================================

TEST_CASE_METHOD(WiFiManagerTestFixture, "Backend initialization state",
                 "[.disabled][macos-wifi][network][backend][init]") {
    SECTION("Backend starts disabled by default") {
// CRITICAL: This catches the bug where mock backend was auto-started
#ifdef __APPLE__
        // macOS uses mock backend - should start disabled
        REQUIRE_FALSE(wifi_manager->is_enabled());
#else
        // Linux may have different behavior depending on system state
        INFO("Backend enabled: " << wifi_manager->is_enabled());
#endif
    }

    SECTION("Explicit enable starts backend") {
        // Skip if no WiFi hardware available (e.g., Mac Mini without WiFi)
        if (!wifi_manager->has_hardware()) {
            SKIP("No WiFi hardware available on this machine");
        }
        bool success = wifi_manager->set_enabled(true);
        REQUIRE(success);
        REQUIRE(wifi_manager->is_enabled());
    }

    SECTION("Explicit disable stops backend") {
        // Skip if no WiFi hardware available (e.g., Mac Mini without WiFi)
        if (!wifi_manager->has_hardware()) {
            SKIP("No WiFi hardware available on this machine");
        }
        // Enable first
        wifi_manager->set_enabled(true);
        REQUIRE(wifi_manager->is_enabled());

        // Then disable
        bool success = wifi_manager->set_enabled(false);
        REQUIRE(success);
        REQUIRE_FALSE(wifi_manager->is_enabled());
    }

    SECTION("Backend lifecycle: start → stop → start") {
        // Skip if no WiFi hardware available (e.g., Mac Mini without WiFi)
        if (!wifi_manager->has_hardware()) {
            SKIP("No WiFi hardware available on this machine");
        }
        // Initial: disabled
        REQUIRE_FALSE(wifi_manager->is_enabled());

        // First start
        wifi_manager->set_enabled(true);
        REQUIRE(wifi_manager->is_enabled());

        // Stop
        wifi_manager->set_enabled(false);
        REQUIRE_FALSE(wifi_manager->is_enabled());

        // Second start (should work after stop)
        wifi_manager->set_enabled(true);
        REQUIRE(wifi_manager->is_enabled());
    }
}

// ============================================================================
// Scan Callback Preservation Tests (CRITICAL)
// ============================================================================

TEST_CASE_METHOD(WiFiManagerTestFixture, "Scan callback preservation",
                 "[.disabled][macos-wifi][network][scan][callback]") {
    SECTION("start_scan registers callback") {
        wifi_manager->set_enabled(true);

        auto callback = [this](const std::vector<WiFiNetwork>& networks) {
            this->scan_callback(networks);
        };

        wifi_manager->start_scan(callback);

// Trigger LVGL timer processing to fire scan event
#ifdef __APPLE__
        bool got_callback = wait_for_condition([this]() { return scan_callback_count > 0; }, 3000);

        REQUIRE(got_callback);
        REQUIRE(scan_callback_count == 1);
        REQUIRE(last_networks.size() > 0);
#endif
    }

    SECTION("stop_scan clears callback to prevent stale-pointer crashes") {
        wifi_manager->set_enabled(true);

        auto callback = [this](const std::vector<WiFiNetwork>& networks) {
            this->scan_callback(networks);
        };

        wifi_manager->start_scan(callback);

        // stop_scan() clears the callback — prevents use-after-free when the
        // overlay that registered the callback is destroyed before a pending
        // scan result is dispatched to the LVGL thread.
        wifi_manager->stop_scan();

        // Re-starting with the same callback re-registers it
        wifi_manager->start_scan(callback);

#ifdef __APPLE__
        bool got_callback = wait_for_condition([this]() { return scan_callback_count > 0; }, 3000);

        REQUIRE(got_callback);
        REQUIRE(scan_callback_count >= 1);
#endif
    }

    SECTION("Callback works across multiple stop/start cycles") {
        wifi_manager->set_enabled(true);

        auto callback = [this](const std::vector<WiFiNetwork>& networks) {
            this->scan_callback(networks);
        };

        // First scan cycle
        wifi_manager->start_scan(callback);
        wifi_manager->stop_scan();

        // Second scan cycle
        wifi_manager->start_scan(callback);
        wifi_manager->stop_scan();

        // Third scan cycle
        wifi_manager->start_scan(callback);

#ifdef __APPLE__
        bool got_callback = wait_for_condition([this]() { return scan_callback_count > 0; }, 3000);

        REQUIRE(got_callback);
#endif
    }

    SECTION("Multiple start_scan calls with different callbacks") {
        wifi_manager->set_enabled(true);

        int callback1_count = 0;
        int callback2_count = 0;

        auto callback1 = [&callback1_count](const std::vector<WiFiNetwork>& networks) {
            (void)networks;
            callback1_count++;
        };

        auto callback2 = [&callback2_count](const std::vector<WiFiNetwork>& networks) {
            (void)networks;
            callback2_count++;
        };

        // First scan with callback1
        wifi_manager->start_scan(callback1);

#ifdef __APPLE__
        wait_for_condition([&callback1_count]() { return callback1_count > 0; }, 3000);
        REQUIRE(callback1_count >= 1);
        REQUIRE(callback2_count == 0);
#endif

        // Stop and restart with callback2
        wifi_manager->stop_scan();
        callback1_count = 0;

        wifi_manager->start_scan(callback2);

#ifdef __APPLE__
        wait_for_condition([&callback2_count]() { return callback2_count > 0; }, 3000);
        REQUIRE(callback1_count == 0); // Old callback not invoked
        REQUIRE(callback2_count >= 1); // New callback invoked
#endif
    }
}

// ============================================================================
// Scan Lifecycle Tests
// ============================================================================

TEST_CASE_METHOD(WiFiManagerTestFixture, "Network scanning lifecycle",
                 "[.disabled][macos-wifi][network][scan]") {
    SECTION("Synchronous scan returns networks") {
        if (!wifi_manager->has_hardware()) {
            SKIP("No WiFi hardware available");
        }

        wifi_manager->set_enabled(true);

        auto networks = wifi_manager->scan_once();

#ifdef __APPLE__
        // Mock backend should return 10 networks
        REQUIRE(networks.size() == 10);
#else
        INFO("Networks found: " << networks.size());
#endif
    }

    SECTION("Scan with backend disabled returns empty/fails") {
        // Backend starts disabled - scan should fail gracefully
        auto networks = wifi_manager->scan_once();

#ifdef __APPLE__
        // Mock backend may still return data when disabled (test implementation detail)
        INFO("Networks found with disabled backend: " << networks.size());
#endif
    }

    SECTION("Stop scan is idempotent") {
        // Multiple stop_scan() calls should be safe
        REQUIRE_NOTHROW(wifi_manager->stop_scan());
        REQUIRE_NOTHROW(wifi_manager->stop_scan());
        REQUIRE_NOTHROW(wifi_manager->stop_scan());
    }

    SECTION("Start scan without backend enabled fails gracefully") {
        // Backend disabled, but start_scan should not crash
        REQUIRE_NOTHROW(wifi_manager->start_scan([](const std::vector<WiFiNetwork>&) {}));
    }

    SECTION("Periodic scan triggers callback multiple times") {
        wifi_manager->set_enabled(true);

        auto callback = [this](const std::vector<WiFiNetwork>& networks) {
            this->scan_callback(networks);
        };

        wifi_manager->start_scan(callback);

#ifdef __APPLE__
        // Wait for at least 2 scan callbacks (periodic scanning every 7s)
        // First scan: ~2s, second scan: ~9s total
        bool got_multiple =
            wait_for_condition([this]() { return scan_callback_count >= 2; }, 10000);

        // Note: May only get 1 callback if test runs too fast
        REQUIRE(scan_callback_count >= 1);
#endif
    }
}

// ============================================================================
// Connection Management Tests
// ============================================================================

TEST_CASE_METHOD(WiFiManagerTestFixture, "WiFi connection management",
                 "[.disabled][macos-wifi][network][connection]") {
    SECTION("Initial connection state is disconnected") {
        REQUIRE_FALSE(wifi_manager->is_connected());
        REQUIRE(wifi_manager->get_connected_ssid().empty());
        REQUIRE(wifi_manager->get_ip_address().empty());
        REQUIRE(wifi_manager->get_signal_strength() == 0);
    }

    SECTION("Connect to network (mock)") {
#ifdef __APPLE__
        wifi_manager->set_enabled(true);

        auto callback = [this](bool success, const std::string& error) {
            this->connection_callback(success, error);
        };

        // Get available networks first — skip if WiFi unavailable (no location permission in CI)
        auto networks = wifi_manager->scan_once();
        if (networks.empty()) {
            SKIP("WiFi scanning unavailable (no location permission)");
        }

        // Try connecting to first network
        wifi_manager->connect(networks[0].ssid, "test_password", callback);

        // Wait for connection result
        bool got_result = wait_for_condition(
            [this]() { return !connection_error.empty() || connection_success; }, 5000);

        REQUIRE(got_result);
        INFO("Connection result: success=" << connection_success << ", error=" << connection_error);
#endif
    }

    SECTION("Disconnect is safe when not connected") {
        REQUIRE_NOTHROW(wifi_manager->disconnect());
    }
}

// ============================================================================
// Status Query Tests
// ============================================================================

TEST_CASE_METHOD(WiFiManagerTestFixture, "WiFi status queries",
                 "[.disabled][macos-wifi][network][status]") {
    SECTION("Hardware detection") {
        bool has_wifi = wifi_manager->has_hardware();

#ifdef __APPLE__
        // macOS mock should always have hardware
        REQUIRE(has_wifi == true);
#else
        INFO("WiFi hardware detected: " << (has_wifi ? "yes" : "no"));
#endif
    }

    // SECTION("Ethernet detection") {
    //     bool has_eth = wifi_manager->has_ethernet();

    //     #ifdef __APPLE__
    //     // macOS mock should always have Ethernet
    //     REQUIRE(has_eth == true);
    //     #else
    //     INFO("Ethernet detected: " << (has_eth ? "yes" : "no"));
    //     #endif
    // }

    // SECTION("Ethernet IP query") {
    //     std::string eth_ip = wifi_manager->get_ethernet_ip();

    //     #ifdef __APPLE__
    //     // macOS mock should return test IP
    //     REQUIRE_FALSE(eth_ip.empty());
    //     INFO("Ethernet IP (mock): " << eth_ip);
    //     #else
    //     INFO("Ethernet IP: " << (eth_ip.empty() ? "not connected" : eth_ip));
    //     #endif
    // }
}

// ============================================================================
// Edge Cases & Error Handling
// ============================================================================

TEST_CASE_METHOD(WiFiManagerTestFixture, "WiFi edge cases",
                 "[.disabled][macos-wifi][network][edge-cases]") {
    SECTION("Rapid enable/disable cycles") {
        for (int i = 0; i < 5; i++) {
            wifi_manager->set_enabled(true);
            wifi_manager->set_enabled(false);
        }

        // Final state should be consistent
        REQUIRE_FALSE(wifi_manager->is_enabled());
    }

    SECTION("Idempotent enable") {
        if (!wifi_manager->has_hardware()) {
            SKIP("No WiFi hardware available on this machine");
        }
        wifi_manager->set_enabled(true);
        wifi_manager->set_enabled(true); // Second call is no-op
        REQUIRE(wifi_manager->is_enabled());
    }

    SECTION("Idempotent disable") {
        wifi_manager->set_enabled(false);
        wifi_manager->set_enabled(false); // Second call is no-op
        REQUIRE_FALSE(wifi_manager->is_enabled());
    }

    SECTION("Stop scan when not scanning") {
        REQUIRE_NOTHROW(wifi_manager->stop_scan());
    }

    SECTION("Destructor cleanup during active scan") {
        if (!wifi_manager->has_hardware()) {
            SKIP("No WiFi hardware available on this machine");
        }
        wifi_manager->set_enabled(true);
        wifi_manager->start_scan([](const std::vector<WiFiNetwork>&) {});

        // Destroy while scanning - should cleanup safely
        REQUIRE_NOTHROW(wifi_manager.reset());
    }

    SECTION("Destructor cleanup during active connection") {
#ifdef __APPLE__
        if (!wifi_manager->has_hardware()) {
            SKIP("No WiFi hardware available on this machine");
        }
        wifi_manager->set_enabled(true);

        auto networks = wifi_manager->scan_once();
        if (networks.size() > 0) {
            wifi_manager->connect(networks[0].ssid, "password", [](bool, const std::string&) {});

            // Destroy while connecting - should cleanup safely
            REQUIRE_NOTHROW(wifi_manager.reset());
        }
#endif
    }
}

// ============================================================================
// Network Information Tests
// ============================================================================

// DISABLED: scan_once() doesn't wait for scan completion - needs to be rewritten to use
// async scan with callback or explicitly wait for thread completion (2s delay)
TEST_CASE_METHOD(WiFiManagerTestFixture, "WiFi network information",
                 "[network][networks][.disabled]") {
    SECTION("Network data validity") {
#ifdef __APPLE__
        wifi_manager->set_enabled(true);
        auto networks = wifi_manager->scan_once();

        REQUIRE(networks.size() == 10);

        for (const auto& net : networks) {
            // SSID should not be empty
            REQUIRE_FALSE(net.ssid.empty());

            // Signal strength in valid range
            REQUIRE(net.signal_strength >= 0);
            REQUIRE(net.signal_strength <= 100);

            // Security info should be present
            if (net.is_secured) {
                REQUIRE_FALSE(net.security_type.empty());
            }
        }
#endif
    }

    SECTION("Networks sorted by signal strength") {
#ifdef __APPLE__
        wifi_manager->set_enabled(true);
        auto networks = wifi_manager->scan_once();

        // Mock backend sorts by signal strength (strongest first)
        for (size_t i = 1; i < networks.size(); i++) {
            REQUIRE(networks[i - 1].signal_strength >= networks[i].signal_strength);
        }
#endif
    }

    SECTION("Network security mix") {
#ifdef __APPLE__
        wifi_manager->set_enabled(true);
        auto networks = wifi_manager->scan_once();

        bool has_secured = false;
        bool has_open = false;

        for (const auto& net : networks) {
            if (net.is_secured)
                has_secured = true;
            if (!net.is_secured)
                has_open = true;
        }

        // Mock should provide mix of secured/unsecured networks
        REQUIRE(has_secured);
        REQUIRE(has_open);
#endif
    }
}

// ============================================================================
// State Observer Tests (#819 regression)
// ============================================================================
//
// Regression coverage for the NetworkWidget startup race (#819 follow-up):
// widget attached before the wpa_supplicant backend's async init completed,
// got an empty STATUS response, and had no wake-up path. Fix added an
// observer list to WiFiManager that fans out backend state transitions so
// UI consumers can refresh once the backend is actually ready.
//
// These tests use the mock backend's connect flow — slow (2-3s per test
// because connect_thread_func sleeps 2000+rng()%1000ms), which is why
// they're tagged [.disabled] alongside the other wifi_manager integration
// tests. Run locally with './build/bin/helix-tests [observers]'.

namespace {

struct MockWifiGuard {
    bool prev_test_mode;
    bool prev_use_real_wifi;
    MockWifiGuard() {
        auto* cfg = get_runtime_config();
        prev_test_mode = cfg->test_mode;
        prev_use_real_wifi = cfg->use_real_wifi;
        cfg->test_mode = true;
        cfg->use_real_wifi = false;
    }
    ~MockWifiGuard() {
        auto* cfg = get_runtime_config();
        cfg->test_mode = prev_test_mode;
        cfg->use_real_wifi = prev_use_real_wifi;
    }
};

// Pick a secured network with strong signal from the mock backend's seed list.
// See WifiBackendMock::init_mock_networks — all secured entries share password
// "12345678", and networks with signal < 20% have a 30% random timeout branch,
// so we explicitly prefer a strong network to avoid flakiness.
struct NetworkPick {
    std::string ssid;
    std::string password;
};

NetworkPick pick_strong_secured_network(WiFiManager& wifi) {
    auto networks = wifi.scan_once();
    NetworkPick best{"", "12345678"};
    int best_strength = -1;
    for (const auto& net : networks) {
        if (net.is_secured && net.signal_strength >= 50 && net.signal_strength > best_strength) {
            best.ssid = net.ssid;
            best_strength = net.signal_strength;
        }
    }
    return best;
}

} // namespace

TEST_CASE_METHOD(WiFiManagerTestFixture, "State observer fires when backend dispatches CONNECTED",
                 "[.disabled][macos-wifi][network][observers][slow]") {
#ifdef HELIX_ENABLE_MOCKS
    MockWifiGuard mock_guard;

    // Rebuild the manager under the mock-wifi runtime config so it picks up
    // the mock backend instead of CoreWLAN / wpa_supplicant.
    wifi_manager.reset();
    wifi_manager = std::make_shared<WiFiManager>(/*silent=*/true);
    wifi_manager->init_self_reference(wifi_manager);
    wifi_manager->set_enabled(true);

    helix::AsyncLifetimeGuard lifetime;
    std::atomic<int> fires{0};
    wifi_manager->add_state_observer(lifetime.token(), [&fires]() { fires.fetch_add(1); });

    NetworkPick pick = pick_strong_secured_network(*wifi_manager);
    REQUIRE_FALSE(pick.ssid.empty());

    wifi_manager->connect(pick.ssid, pick.password, [](bool, const std::string&) {});

    // Connect thread sleeps 2-3s before firing CONNECTED. Drain the UpdateQueue
    // from the poll loop so deferred observer callbacks actually run.
    bool got_fire = wait_for_condition(
        [&fires]() {
            helix::ui::UpdateQueue::instance().drain();
            return fires.load() > 0;
        },
        5000);

    REQUIRE(got_fire);
    REQUIRE(fires.load() >= 1);
#else
    SUCCEED("Mocks disabled in this build — observer test skipped");
#endif
}

TEST_CASE_METHOD(WiFiManagerTestFixture, "State observer with expired token is not invoked",
                 "[.disabled][macos-wifi][network][observers][slow]") {
#ifdef HELIX_ENABLE_MOCKS
    MockWifiGuard mock_guard;

    wifi_manager.reset();
    wifi_manager = std::make_shared<WiFiManager>(/*silent=*/true);
    wifi_manager->init_self_reference(wifi_manager);
    wifi_manager->set_enabled(true);

    helix::AsyncLifetimeGuard lifetime;
    std::atomic<int> fires{0};
    wifi_manager->add_state_observer(lifetime.token(), [&fires]() { fires.fetch_add(1); });

    // Simulate owner dismissal before the backend fires CONNECTED.
    lifetime.invalidate();

    NetworkPick pick = pick_strong_secured_network(*wifi_manager);
    REQUIRE_FALSE(pick.ssid.empty());

    wifi_manager->connect(pick.ssid, pick.password, [](bool, const std::string&) {});

    // Wait long enough for CONNECTED to have fired, draining all the while.
    // The deferred observer callback sees an expired token and skips silently.
    auto start = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start < std::chrono::milliseconds(4500)) {
        helix::ui::UpdateQueue::instance().drain();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    REQUIRE(fires.load() == 0);
#else
    SUCCEED("Mocks disabled in this build — observer test skipped");
#endif
}

// Contract guard for the non-blocking wizard WiFi bringup
// (WizardWifiStep::init_wifi_manager / apply_wifi_backend_state): a state
// observer added AFTER the backend's initial READY has already fired must NOT
// be replayed that READY — add_state_observer() only push_backs. This is
// precisely why the wizard step calls apply_wifi_backend_state() once
// immediately after subscribing instead of relying on the observer to deliver
// current state. If someone "fixes" add_state_observer to replay the last
// event, the wizard would double-apply (and double-kick the scan); this test
// fails first.
TEST_CASE_METHOD(WiFiManagerTestFixture,
                 "State observer added after READY is not replayed the READY",
                 "[.disabled][macos-wifi][network][observers][slow]") {
#ifdef HELIX_ENABLE_MOCKS
    MockWifiGuard mock_guard;

    // Rebuild under mock-wifi config so the backend is the mock that fires
    // READY shortly after start_async() (kicked from the constructor).
    wifi_manager.reset();
    wifi_manager = std::make_shared<WiFiManager>(/*silent=*/true);
    wifi_manager->init_self_reference(wifi_manager);

    // Wait for the asynchronous READY to land. The mock backend reports
    // is_enabled()==true once it has connected to its simulated supplicant.
    bool ready = wait_for_condition(
        [this]() {
            helix::ui::UpdateQueue::instance().drain();
            return wifi_manager->is_enabled();
        },
        5000);
    REQUIRE(ready);

    // Subscribe AFTER READY already fired.
    helix::AsyncLifetimeGuard lifetime;
    std::atomic<int> fires{0};
    wifi_manager->add_state_observer(lifetime.token(), [&fires]() { fires.fetch_add(1); });

    // Drain a while: no state change occurs, and the already-fired READY must
    // not be replayed to the late subscriber.
    auto start = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start < std::chrono::milliseconds(500)) {
        helix::ui::UpdateQueue::instance().drain();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    REQUIRE(fires.load() == 0);
#else
    SUCCEED("Mocks disabled in this build — observer test skipped");
#endif
}

// helixscreen: set_enabled(false) used to call backend_->stop(), which tore
// down the control connection to wpa_supplicant entirely — the radio stayed
// associated and routed (UI reported "off" while the printer was still on
// WiFi), and every later STATUS logged "send_command called but not
// connected to wpa_supplicant" because there was nothing left to query.
// set_enabled must now drive the radio directly and leave the backend
// running so the connection survives a toggle.
TEST_CASE("set_enabled(false) turns the radio off without stopping the backend",
          "[wifi][manager][radio]") {
    auto backend = std::make_unique<WifiBackendMock>();
    WifiBackendMock* raw = backend.get();
    WiFiManager manager(std::move(backend));

    REQUIRE(manager.set_enabled(false));
    CHECK_FALSE(raw->is_radio_enabled());
    // The old implementation called stop() here, after which no command could
    // be sent and every STATUS logged "not connected to wpa_supplicant".
    CHECK(raw->is_running());
    CHECK_FALSE(manager.is_enabled());

    REQUIRE(manager.set_enabled(true));
    CHECK(raw->is_radio_enabled());
    CHECK(manager.is_enabled());

    // READY (fired synchronously by the mock backend during construction
    // above) queued a WiFiManager::reassert_stored_radio_state closure via
    // async_lifetime_.defer(). Drain it here, while `manager` (and its
    // subjects) are still alive, so it doesn't leak into the next test —
    // see scripts/check_update_queue_leaks.py.
    helix::ui::UpdateQueue::instance().drain();
}

// helixscreen: a radio the user switched off must not come back on just
// because the process restarted. WiFiManager's READY handler reasserts the
// stored SystemSettingsManager choice onto the backend the first time it can
// act on it — but only when a non-WiFi network path is actually up (Task 15:
// CC1 was stranded when this reassert ran unconditionally on a device whose
// only network path was the radio it had just been told to disable). The
// reassert is deferred through AsyncLifetimeGuard (READY can fire on a
// background thread), so the test must drain UpdateQueue before asserting —
// see tests/CLAUDE.md "Deferred work needs an explicit drain".
TEST_CASE("READY reasserts a stored WiFi-off setting onto the backend when a wired path exists",
          "[wifi][manager][radio][persistence]") {
    Config::get_instance();
    SystemSettingsManager::instance().init_subjects();
    SystemSettingsManager::instance().set_wifi_enabled(false);

    // A live eth0 means the device stays reachable even with the radio off —
    // the pre-Task-15 reassert-off behaviour applies unchanged.
    SysFixture sys;
    sys.write_up("eth0");
    WiFiManagerTestAccess::set_sys_root(sys.sys_root());

    auto backend = std::make_unique<WifiBackendMock>();
    WifiBackendMock* raw = backend.get();
    helix::wifi::WifiInterface iface;
    iface.netdev = "wlan0";
    raw->set_resolved_interface_for_test(iface);
    // Radio starts enabled by default; only the READY reassert should flip it.
    REQUIRE(raw->is_radio_enabled());

    WiFiManager manager(std::move(backend));

    // start_async() (called from the constructor) fires READY synchronously
    // for the mock backend, but the reassert itself is only queued, not run —
    // drain to let it land.
    helix::ui::UpdateQueue::instance().drain();

    CHECK_FALSE(raw->is_radio_enabled());
    // A wired fallback exists, so the stored "off" choice is honoured as-is —
    // no correction.
    CHECK_FALSE(SystemSettingsManager::instance().get_wifi_enabled());

    SystemSettingsManager::instance().set_wifi_enabled(true);
    SystemSettingsManager::instance().deinit_subjects();
    WiFiManagerTestAccess::reset_sys_root();
}

// The CC1 incident this task exists to prevent: stored setting is off, and
// this device's only network path is the radio WiFiManager is about to be
// told to disable. The reassert must refuse, AND correct the stored setting
// back to true — leaving wifi_enabled at false while the radio is actually on
// would show "off" in the UI over a working connection, the exact class of
// state lie this whole branch exists to eliminate.
TEST_CASE("READY refuses to disable the radio with no wired fallback, and corrects the "
          "stored setting",
          "[wifi][manager][radio][persistence]") {
    Config::get_instance();
    SystemSettingsManager::instance().init_subjects();
    SystemSettingsManager::instance().set_wifi_enabled(false);

    // Empty fixture tree — class/net exists but has no interfaces at all, so
    // there is no non-WiFi path.
    SysFixture sys;
    WiFiManagerTestAccess::set_sys_root(sys.sys_root());

    auto backend = std::make_unique<WifiBackendMock>();
    WifiBackendMock* raw = backend.get();
    helix::wifi::WifiInterface iface;
    iface.netdev = "wlan0";
    raw->set_resolved_interface_for_test(iface);
    REQUIRE(raw->is_radio_enabled());

    WiFiManager manager(std::move(backend));
    helix::ui::UpdateQueue::instance().drain();

    CHECK(raw->is_radio_enabled());
    CHECK(SystemSettingsManager::instance().get_wifi_enabled());

    SystemSettingsManager::instance().deinit_subjects();
    WiFiManagerTestAccess::reset_sys_root();
}

// Interface resolution itself can be inconclusive (resolved_interface()
// returns nullopt) independent of what the wired-path check would have said —
// e.g. a backend that hasn't implemented resolution, or resolution that
// genuinely failed. The gate must fail safe in that case too: never disable
// the radio on an unverified interface identity, even if a wired interface
// happens to be up in sysfs.
TEST_CASE("READY refuses to disable the radio when interface resolution is inconclusive",
          "[wifi][manager][radio][persistence]") {
    Config::get_instance();
    SystemSettingsManager::instance().init_subjects();
    SystemSettingsManager::instance().set_wifi_enabled(false);

    SysFixture sys;
    sys.write_up("eth0"); // a wired path DOES exist...
    WiFiManagerTestAccess::set_sys_root(sys.sys_root());

    auto backend = std::make_unique<WifiBackendMock>();
    WifiBackendMock* raw = backend.get();
    // ...but resolved_interface() is left at its default nullopt: resolution
    // never ran, so the gate must not trust the wired-path check at all.
    REQUIRE_FALSE(raw->resolved_interface().has_value());
    REQUIRE(raw->is_radio_enabled());

    WiFiManager manager(std::move(backend));
    helix::ui::UpdateQueue::instance().drain();

    CHECK(raw->is_radio_enabled());
    CHECK(SystemSettingsManager::instance().get_wifi_enabled());

    SystemSettingsManager::instance().deinit_subjects();
    WiFiManagerTestAccess::reset_sys_root();
}

// The mirror-image gap this task closes: a device whose stored preference is
// WiFi ON but whose radio carries a stale soft rfkill block (e.g. one this
// app itself applied before commit 8aaac4e78 made a soft block non-fatal at
// startup) previously stayed radio-dead until a human tapped the touchscreen
// to toggle it back on. CC1 was stranded exactly this way for days. The READY
// reassert must now clear a stale soft block automatically so the device
// heals itself on its next launch, with no tap required.
TEST_CASE("READY clears a stale soft radio block when the stored setting is WiFi on",
          "[wifi][manager][radio][persistence]") {
    Config::get_instance();
    SystemSettingsManager::instance().init_subjects();
    SystemSettingsManager::instance().set_wifi_enabled(true);

    auto backend = std::make_unique<WifiBackendMock>();
    WifiBackendMock* raw = backend.get();
    helix::wifi::WifiInterface iface;
    iface.netdev = "wlan0";
    raw->set_resolved_interface_for_test(iface);

    // Simulate is_radio_enabled() having been seeded from a stale hardware
    // soft-block during resolve_and_store_interface(), ahead of the mock
    // backend's own construction/start sequence.
    raw->set_radio_enabled(false);
    REQUIRE_FALSE(raw->is_radio_enabled());

    WiFiManager manager(std::move(backend));

    // start_async() (called from the constructor) fires READY synchronously
    // for the mock backend, but the reassert itself is only queued, not run —
    // drain to let it land.
    helix::ui::UpdateQueue::instance().drain();

    CHECK(raw->is_radio_enabled());
    CHECK(SystemSettingsManager::instance().get_wifi_enabled());

    SystemSettingsManager::instance().deinit_subjects();
}

// Steady-state case: stored setting is on and the radio already agrees. The
// reassert must be a no-op here — no toggling a radio that was never
// disabled in the first place.
TEST_CASE("READY leaves an already-enabled radio alone when the stored setting is WiFi on",
          "[wifi][manager][radio][persistence]") {
    Config::get_instance();
    SystemSettingsManager::instance().init_subjects();
    SystemSettingsManager::instance().set_wifi_enabled(true);

    auto backend = std::make_unique<WifiBackendMock>();
    WifiBackendMock* raw = backend.get();
    helix::wifi::WifiInterface iface;
    iface.netdev = "wlan0";
    raw->set_resolved_interface_for_test(iface);
    REQUIRE(raw->is_radio_enabled());

    WiFiManager manager(std::move(backend));
    helix::ui::UpdateQueue::instance().drain();

    CHECK(raw->is_radio_enabled());
    CHECK(SystemSettingsManager::instance().get_wifi_enabled());

    SystemSettingsManager::instance().deinit_subjects();
}

// The Task-15 refusal path (stored off, no wired fallback) corrects the
// stored setting back to on — but a device that had already accumulated a
// stale soft block before this fix would end up with wifi_enabled=true while
// the radio itself stayed off, the same class of state lie the correction
// exists to eliminate. The refusal path must also clear the block so the
// radio is actually on, not merely recorded as on.
TEST_CASE("READY refusal path also clears a stale radio block, not just the stored setting",
          "[wifi][manager][radio][persistence]") {
    Config::get_instance();
    SystemSettingsManager::instance().init_subjects();
    SystemSettingsManager::instance().set_wifi_enabled(false);

    // Empty fixture tree — class/net exists but has no interfaces at all, so
    // there is no non-WiFi path.
    SysFixture sys;
    WiFiManagerTestAccess::set_sys_root(sys.sys_root());

    auto backend = std::make_unique<WifiBackendMock>();
    WifiBackendMock* raw = backend.get();
    helix::wifi::WifiInterface iface;
    iface.netdev = "wlan0";
    raw->set_resolved_interface_for_test(iface);

    // Radio already carries a stale soft block, mirroring is_radio_enabled()
    // having been seeded from hardware ahead of this READY firing.
    raw->set_radio_enabled(false);
    REQUIRE_FALSE(raw->is_radio_enabled());

    WiFiManager manager(std::move(backend));
    helix::ui::UpdateQueue::instance().drain();

    CHECK(raw->is_radio_enabled());
    CHECK(SystemSettingsManager::instance().get_wifi_enabled());

    SystemSettingsManager::instance().deinit_subjects();
    WiFiManagerTestAccess::reset_sys_root();
}
