// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_temperature_store_seed.cpp
 * @brief TDD unit tests for seeding temperature history from Moonraker (#944)
 *
 * Covers:
 * - TemperatureHistoryManager::seed_from_store: fill-empty, decidegree
 *   conversion, 1 Hz backfill timestamps, sensor-only (target 0), merge with an
 *   existing out-of-order live sample, and the HISTORY_SIZE cap.
 * - MoonrakerClient temperature_store RPC: empty-store default through the mock,
 *   and direct parse of a populated canned payload.
 *
 * These tests were authored BEFORE the implementation (TDD).
 */

#include "../../include/moonraker_client_mock.h"
#include "../../include/printer_state.h"
#include "../../include/temperature_history_manager.h"
#include "../../include/ui_update_queue.h"
#include "../../lvgl/lvgl.h"
#include "../test_helpers/update_queue_test_access.h"
#include "../ui_test_utils.h"

// Grants tests access to add_sample_internal via the friend declared in
// temperature_history_manager.h. Definition is token-identical to the one in
// test_temperature_history_manager.cpp so the ODR is satisfied across TUs.
class TemperatureHistoryManagerTestAccess {
  public:
    static bool add_sample(TemperatureHistoryManager& m, const std::string& heater_name,
                           int temp_deci, int target_deci, int64_t timestamp_ms) {
        bool stored;
        {
            std::lock_guard<std::mutex> lock(m.mutex_);
            stored = m.add_sample_internal(heater_name, temp_deci, target_deci, timestamp_ms);
        }
        if (stored) {
            m.notify_observers(heater_name);
        }
        return stored;
    }
};

#include <algorithm>
#include <chrono>
#include <cmath>
#include <string>
#include <vector>

#include "../catch_amalgamated.hpp"

using namespace helix;
using namespace helix::ui;

// ============================================================================
// Global LVGL Initialization
// ============================================================================

namespace {
struct LVGLInitializerStoreSeed {
    LVGLInitializerStoreSeed() {
        static bool initialized = false;
        if (!initialized) {
            lv_init_safe();
            lv_display_t* disp = lv_display_create(800, 480);
            alignas(64) static lv_color_t buf[800 * 10];
            lv_display_set_buffers(disp, buf, NULL, sizeof(buf), LV_DISPLAY_RENDER_MODE_PARTIAL);
            initialized = true;
        }
    }
};

static LVGLInitializerStoreSeed lvgl_init;

// A fixed wall-clock base (Unix ms) so timestamp math is deterministic.
constexpr int64_t kNow = 1700000000000LL;
} // namespace

// ============================================================================
// Test Fixture
// ============================================================================

class TempStoreSeedTestFixture {
    static bool queue_initialized;

  public:
    TempStoreSeedTestFixture() {
        if (!queue_initialized) {
            helix::ui::update_queue_init();
            queue_initialized = true;
        }
        printer_state_.init_subjects(false);
        manager_ = std::make_unique<TemperatureHistoryManager>(printer_state_);
    }

    ~TempStoreSeedTestFixture() {
        manager_.reset();
        UpdateQueueTestAccess::drain(helix::ui::UpdateQueue::instance());
        helix::ui::update_queue_shutdown();
        queue_initialized = false;
    }

  protected:
    PrinterState printer_state_;
    std::unique_ptr<TemperatureHistoryManager> manager_;
};

bool TempStoreSeedTestFixture::queue_initialized = false;

// ============================================================================
// seed_from_store — fill an empty manager
// ============================================================================

TEST_CASE_METHOD(TempStoreSeedTestFixture, "seed_from_store fills empty history", "[temp][seed]") {
    TemperatureStore store;
    store["extruder"].temperatures = {200.0f, 201.0f, 202.5f, 203.0f, 205.4f};
    store["extruder"].targets = {210.0f, 210.0f, 210.0f, 210.0f, 210.0f};
    // Sensor-only key: temperatures but no targets/powers.
    store["temperature_sensor foo"].temperatures = {30.0f, 31.2f, 32.0f};

    manager_->seed_from_store(store, kNow);

    auto ex = manager_->get_samples_since("extruder", 0);
    REQUIRE(ex.size() == 5);

    // Timestamps: 1 Hz spacing, newest == kNow, oldest first.
    REQUIRE(ex.back().timestamp_ms == kNow);
    REQUIRE(ex.front().timestamp_ms == kNow - 4000);
    for (size_t i = 0; i < ex.size(); ++i) {
        int64_t expected = kNow - static_cast<int64_t>(4 - i) * 1000;
        REQUIRE(ex[i].timestamp_ms == expected);
    }

    // Decidegree conversion (×10) for temps and targets.
    REQUIRE(ex.front().temp_deci == 2000);
    REQUIRE(ex[2].temp_deci == 2025);
    REQUIRE(ex.back().temp_deci == 2054);
    REQUIRE(ex.front().target_deci == 2100);
    REQUIRE(ex.back().target_deci == 2100);

    auto foo = manager_->get_samples_since("temperature_sensor foo", 0);
    REQUIRE(foo.size() == 3);
    REQUIRE(foo.back().timestamp_ms == kNow);
    REQUIRE(foo.front().timestamp_ms == kNow - 2000);
    REQUIRE(foo[1].temp_deci == 312); // 31.2 -> 312
    // No targets provided -> target_deci defaults to 0.
    for (const auto& s : foo) {
        REQUIRE(s.target_deci == 0);
    }
}

// ============================================================================
// seed_from_store — merge with an existing out-of-order (newer) live sample
// ============================================================================

TEST_CASE_METHOD(TempStoreSeedTestFixture, "seed_from_store merges with existing samples",
                 "[temp][seed]") {
    // A live sample already recorded for "extruder" that is NEWER than the
    // seed range — simulates the fetch returning after a live sample landed.
    const int64_t live_ts = kNow + 5000;
    REQUIRE(TemperatureHistoryManagerTestAccess::add_sample(*manager_, "extruder", 2100, 2100,
                                                            live_ts));

    TemperatureStore store;
    store["extruder"].temperatures = {100.0f, 101.0f, 102.0f}; // 3 older samples
    manager_->seed_from_store(store, kNow);

    auto s = manager_->get_samples_since("extruder", 0);
    REQUIRE(s.size() == 4);

    // Strictly time-ordered oldest-first (no inversion despite late insert).
    for (size_t i = 1; i < s.size(); ++i) {
        REQUIRE(s[i].timestamp_ms > s[i - 1].timestamp_ms);
    }

    // Seed samples occupy the older slots; the live sample is newest.
    REQUIRE(s.front().timestamp_ms == kNow - 2000);
    REQUIRE(s[2].timestamp_ms == kNow);        // newest seed sample
    REQUIRE(s.back().timestamp_ms == live_ts); // live sample stays newest
    REQUIRE(s.back().temp_deci == 2100);
}

// ============================================================================
// seed_from_store — HISTORY_SIZE cap keeps the newest samples
// ============================================================================

TEST_CASE_METHOD(TempStoreSeedTestFixture, "seed_from_store caps at HISTORY_SIZE", "[temp][seed]") {
    const size_t n = TemperatureHistoryManager::HISTORY_SIZE + 100; // 1300
    std::vector<float> temps;
    temps.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        temps.push_back(static_cast<float>((i % 300) + 1)); // positive, varied
    }

    TemperatureStore store;
    store["extruder"].temperatures = temps;
    manager_->seed_from_store(store, kNow);

    REQUIRE(manager_->get_sample_count("extruder") == TemperatureHistoryManager::HISTORY_SIZE);

    auto s = manager_->get_samples("extruder");
    REQUIRE(s.size() == static_cast<size_t>(TemperatureHistoryManager::HISTORY_SIZE));

    // Newest kept == kNow; oldest kept dropped the first 100 synthesized points.
    REQUIRE(s.back().timestamp_ms == kNow);
    REQUIRE(s.front().timestamp_ms ==
            kNow - static_cast<int64_t>(TemperatureHistoryManager::HISTORY_SIZE - 1) * 1000);

    // Value identity of the retained window: newest is temps.back(), oldest
    // kept is the original index (n - HISTORY_SIZE) == 100.
    REQUIRE(s.back().temp_deci == static_cast<int>(std::lround(temps.back() * 10.0f)));
    REQUIRE(
        s.front().temp_deci ==
        static_cast<int>(std::lround(temps[n - TemperatureHistoryManager::HISTORY_SIZE] * 10.0f)));
}

// ============================================================================
// RPC — mock returns an empty store; direct parse of a populated payload
// ============================================================================

TEST_CASE("get_temperature_store returns a realistic populated store via mock", "[temp][seed]") {
    MoonrakerClientMock mock(MoonrakerClientMock::PrinterType::VORON_24);
    mock.connect("ws://mock/websocket", []() {}, []() {});

    bool got = false;
    TemperatureStore store;
    mock.get_temperature_store(
        [&](const TemperatureStore& s) {
            got = true;
            store = s;
        },
        [](const MoonrakerError& err) { FAIL("Error callback invoked: " << err.message); });

    REQUIRE(got);
    // Mock seeds a heat/hold/cool curve for the core heaters (#944).
    REQUIRE(store.count("extruder") == 1);
    REQUIRE(store.count("heater_bed") == 1);

    const auto& ex = store.at("extruder");
    REQUIRE(ex.temperatures.size() == 600);
    REQUIRE(ex.targets.size() == ex.temperatures.size());
    REQUIRE(ex.powers.size() == ex.temperatures.size());

    // Heater curve reaches a plausible printing temperature at its peak and
    // cools back toward ambient by the end.
    float peak = *std::max_element(ex.temperatures.begin(), ex.temperatures.end());
    REQUIRE(peak > 200.0f);
    REQUIRE(peak < 230.0f);
    REQUIRE(ex.temperatures.back() < 60.0f); // cooled down at the tail

    // A temperature_sensor series carries temperatures but no targets.
    for (const auto& [key, series] : store) {
        if (key.rfind("temperature_sensor ", 0) == 0) {
            REQUIRE(series.temperatures.size() == 600);
            REQUIRE(series.targets.empty());
        }
    }

    mock.stop_temperature_simulation();
    mock.disconnect();
}

TEST_CASE("parse_temperature_store parses a populated payload", "[temp][seed]") {
    json result = {
        {"extruder",
         {{"temperatures", {200.0, 201.0}}, {"targets", {210.0, 210.0}}, {"powers", {0.5, 0.6}}}},
        {"temperature_sensor foo", {{"temperatures", {30.0, 31.0, 32.0}}}}};

    TemperatureStore store = MoonrakerClient::parse_temperature_store(result);
    REQUIRE(store.size() == 2);

    const auto& ex = store.at("extruder");
    REQUIRE(ex.temperatures.size() == 2);
    REQUIRE(ex.targets.size() == 2);
    REQUIRE(ex.powers.size() == 2);
    REQUIRE(ex.temperatures[0] == Catch::Approx(200.0));
    REQUIRE(ex.targets[1] == Catch::Approx(210.0));

    const auto& foo = store.at("temperature_sensor foo");
    REQUIRE(foo.temperatures.size() == 3);
    REQUIRE(foo.targets.empty());
    REQUIRE(foo.powers.empty());
    REQUIRE(foo.temperatures[2] == Catch::Approx(32.0));

    // A non-object result parses to an empty store.
    REQUIRE(MoonrakerClient::parse_temperature_store(json::array()).empty());
}
