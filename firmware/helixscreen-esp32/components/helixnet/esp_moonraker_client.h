// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
//
// helixnet — the ESP32 implementation of helix::IMoonrakerClient over
// esp_websocket_client. Mirrors the desktop MoonrakerClient's consumer contract
// (src/api/moonraker_client.cpp) but replaces libhv with the ESP-IDF managed
// esp_websocket_client component. See docs task-9-brief for the full contract.
//
// Threading model: esp_websocket_client owns a FreeRTOS task ("websocket_task").
// All WEBSOCKET_EVENT_* callbacks, and therefore message dispatch and every user
// callback we invoke, run ON that task. This IS the "background thread" contract
// desktop consumers expect: they hop to the LVGL main thread via ui_queue_update
// exactly as they do behind libhv. The request tracker and callback maps are
// therefore mutex-guarded with two-phase locking (copy the callback out under the
// lock, invoke it outside).

#pragma once

#include "i_moonraker_client.h"

#include "esp_timer.h"
#include "esp_websocket_client.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>

namespace helix {

class EspMoonrakerClient final : public IMoonrakerClient {
  public:
    EspMoonrakerClient();
    ~EspMoonrakerClient() override;

    EspMoonrakerClient(const EspMoonrakerClient&) = delete;
    EspMoonrakerClient& operator=(const EspMoonrakerClient&) = delete;

    // --- Connection lifecycle ---
    int connect(const char* url, std::function<void()> on_connected,
                std::function<void()> on_disconnected) override;
    void disconnect() override;
    const std::string& get_last_url() const override {
        return url_;
    }
    void set_auto_reconnect(bool enabled) override;

    // --- JSON-RPC protocol ---
    int send_jsonrpc(const std::string& method) override;
    int send_jsonrpc(const std::string& method, const json& params) override;
    RequestId send_jsonrpc(const std::string& method, const json& params,
                           std::function<void(const json&)> cb) override;
    RequestId send_jsonrpc(const std::string& method, const json& params,
                           std::function<void(const json&)> success_cb,
                           std::function<void(const MoonrakerError&)> error_cb,
                           uint32_t timeout_ms = 0, bool silent = false) override;
    int gcode_script(const std::string& gcode) override;
    void get_gcode_store(int count,
                         std::function<void(const std::vector<GcodeStoreEntry>&)> on_success,
                         std::function<void(const MoonrakerError&)> on_error) override;
    void get_temperature_store(std::function<void(const TemperatureStore&)> on_success,
                               std::function<void(const MoonrakerError&)> on_error) override;

    // --- Discovery (Plan 4 fills the real sequence) ---
    void discover_printer(std::function<void()> on_complete,
                          std::function<void(const std::string& reason)> on_error = nullptr) override;
    PrinterDiscovery hardware() const override;
    void parse_objects(const json& objects) override;
    void clear_discovery_cache() override;

    // --- Subscriptions & method callbacks ---
    SubscriptionId register_notify_update(std::function<void(const json&)> cb) override;
    bool unsubscribe_notify_update(SubscriptionId id) override;
    void register_method_callback(const std::string& method, const std::string& handler_name,
                                  std::function<void(const json&)> cb) override;
    bool unregister_method_callback(const std::string& method,
                                    const std::string& handler_name) override;

    // --- Connection state & observers ---
    ConnectionState get_connection_state() const override;
    void add_connected_observer(const std::string& handler_name,
                                std::function<void()> cb) override;
    bool remove_connected_observer(const std::string& handler_name) override;
    void force_reconnect() override;

    // --- Events & modal suppression ---
    void register_event_handler(MoonrakerEventCallback cb) override;
    void suppress_disconnect_modal(uint32_t duration_ms = 10000) override;
    bool is_disconnect_modal_suppressed() const override;

    // --- Request management ---
    bool cancel_request(RequestId id) override;

    // --- Owner wiring & configuration ---
    void set_state_change_callback(
        std::function<void(ConnectionState, ConnectionState)> cb) override;
    void set_connection_timeout(uint32_t timeout_ms) override;
    void set_default_request_timeout(uint32_t timeout_ms) override;
    void configure_timeouts(uint32_t connection_timeout_ms, uint32_t request_timeout_ms,
                            uint32_t keepalive_interval_ms, uint32_t reconnect_min_delay_ms,
                            uint32_t reconnect_max_delay_ms) override;
    void process_timeouts() override;

    // --- Simulation hooks ---
    void toggle_filament_runout_simulation() override;

    // --- Lifetime guard ---
    std::weak_ptr<bool> lifetime_weak() const override;

  private:
    // Reassembly cap: a single WS message larger than this is dropped whole.
    // Moonraker does not chunk at the protocol level, so an oversized response's
    // RPC will simply time out (see brief). 256 KiB.
    static constexpr size_t kMaxMessageBytes = 262144;
    // Own request-tracker cap — far below desktop's 500 (RAM-bound). On overflow
    // the error callback fires synchronously with a CONNECTION_LOST error.
    static constexpr size_t kMaxPendingRequests = 64;
    static constexpr uint32_t kDefaultRequestTimeoutMs = 60000;
    // How long RECONNECTING persists before the informational FAILED transition.
    static constexpr int64_t kReconnectingToFailedUs = 60LL * 1000 * 1000;
    // Period of the owned esp_timer that drives timeout + FAILED bookkeeping.
    static constexpr uint64_t kHousekeepingPeriodUs = 5LL * 1000 * 1000;

    struct Pending {
        std::string method;
        std::function<void(const json&)> success_cb;
        std::function<void(const MoonrakerError&)> error_cb;
        int64_t sent_us = 0;
        uint32_t timeout_ms = 0;
        bool silent = false;
    };

    // esp_websocket_client event trampoline → instance dispatch.
    static void ws_event_trampoline(void* arg, esp_event_base_t base, int32_t event_id,
                                    void* event_data);
    void on_ws_connected();
    void on_ws_disconnected();
    void on_ws_data(const esp_websocket_event_data_t* data);
    void dispatch_message(const char* buf, size_t len);

    // esp_timer trampoline → instance housekeeping (timeouts + FAILED).
    static void housekeeping_trampoline(void* arg);

    void set_state(ConnectionState next);
    void emit_event(MoonrakerEventType type, const std::string& message, bool is_error,
                    const std::string& details = "");

    // Serialize + send a JSON-RPC envelope over the socket. Returns bytes sent
    // (>=0) or negative on failure. Safe to call from any task.
    int send_envelope(const json& envelope);
    RequestId track_and_send(const std::string& method, const json& params,
                             std::function<void(const json&)> success_cb,
                             std::function<void(const MoonrakerError&)> error_cb,
                             uint32_t timeout_ms, bool silent);
    bool is_connected() const;

    esp_websocket_client_handle_t ws_ = nullptr;
    esp_timer_handle_t housekeeping_timer_ = nullptr;
    std::string url_;

    std::function<void()> on_connected_;
    std::function<void()> on_disconnected_;

    // Flipped false in the dtor BEFORE esp_websocket_client_stop() so any event
    // still in flight on the WS task early-outs instead of touching torn-down
    // members. Mirrors desktop's destruction_guard_ ordering.
    std::atomic<bool> alive_{true};
    // Backing store for lifetime_weak(): identical to desktop's shared_ptr<bool>.
    std::shared_ptr<bool> lifetime_ = std::make_shared<bool>(true);
    // Set while a housekeeping tick is executing on the ESP_TIMER_TASK. The dtor
    // spins on this after esp_timer_delete() because that call does not join an
    // in-flight callback (see the dtor comment).
    std::atomic<bool> timer_in_flight_{false};

    mutable std::mutex state_mutex_;
    ConnectionState state_ = ConnectionState::DISCONNECTED;
    std::function<void(ConnectionState, ConnectionState)> state_change_cb_;
    int64_t reconnecting_since_us_ = 0;

    // Manual exponential backoff (the component's own auto-reconnect is fixed 10s).
    int reconnect_min_delay_ms_ = 200;
    int reconnect_max_delay_ms_ = 2000;
    int next_reconnect_delay_ms_ = 200;
    // When false, a disconnect goes straight to DISCONNECTED and background
    // reconnection is suspended (connection-test probe flows). Transient: every
    // connect() re-arms it to true.
    std::atomic<bool> auto_reconnect_{true};

    // Fragment reassembly (grows to cap, shrinks on disconnect). WS-task only.
    std::string rx_buf_;
    bool rx_skip_ = false;
    // WS-task only. Gates the RECONNECTED event so the first-ever connect is
    // silent and only genuine reconnections emit (desktop was_connected_).
    bool was_connected_ = false;

    // Bounded request tracker.
    std::mutex requests_mutex_;
    std::map<uint64_t, Pending> pending_;
    std::atomic<uint64_t> next_request_id_{0};
    uint32_t default_request_timeout_ms_ = kDefaultRequestTimeoutMs;
    uint32_t connection_timeout_ms_ = 10000;

    // Callback maps.
    std::mutex callbacks_mutex_;
    std::map<SubscriptionId, std::function<void(const json&)>> notify_callbacks_;
    std::atomic<uint64_t> next_subscription_id_{0};
    std::map<std::string, std::map<std::string, std::function<void(const json&)>>>
        method_callbacks_;

    std::mutex observers_mutex_;
    std::map<std::string, std::function<void()>> connected_observers_;

    std::mutex event_mutex_;
    MoonrakerEventCallback event_handler_;

    std::atomic<int64_t> suppress_modal_until_us_{0};

    PrinterDiscovery hardware_; // default-constructed; Plan 4 populates via discovery
};

} // namespace helix

// Platform factory (Plan 3 Task 8 consumes this). Returns the ESP32 transport as
// the polymorphic interface so the app layer stays platform-agnostic.
namespace helix {
std::unique_ptr<IMoonrakerClient> create_platform_moonraker_client();
} // namespace helix
