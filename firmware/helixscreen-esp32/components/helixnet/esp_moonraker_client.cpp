// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "esp_moonraker_client.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <utility>
#include <vector>

namespace helix {

namespace {
constexpr const char* TAG = "helixnet";

int64_t now_us() {
    return esp_timer_get_time();
}

// WS text/continuation opcodes we reassemble; everything else (binary, ping,
// pong, close) is handled by the component or ignored.
constexpr uint8_t kOpText = 0x01;
constexpr uint8_t kOpContinuation = 0x00;
} // namespace

EspMoonrakerClient::EspMoonrakerClient() {
    const esp_timer_create_args_t targs = {
        .callback = &EspMoonrakerClient::housekeeping_trampoline,
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "helixnet_hk",
        .skip_unhandled_events = true,
    };
    if (esp_timer_create(&targs, &housekeeping_timer_) == ESP_OK) {
        esp_timer_start_periodic(housekeeping_timer_, kHousekeepingPeriodUs);
    } else {
        ESP_LOGE(TAG, "failed to create housekeeping timer");
        housekeeping_timer_ = nullptr;
    }
}

EspMoonrakerClient::~EspMoonrakerClient() {
    // Destruction order is safety-critical (desktop precedent). Flip alive FIRST
    // so any event already queued on either task early-outs before touching
    // members.
    alive_.store(false);

    // Tear the housekeeping timer down BEFORE the transport. esp_timer_stop()/
    // esp_timer_delete() prevent future dispatches but do NOT join a callback
    // that is already running on the ESP_TIMER_TASK — so after deleting we spin
    // until the in-flight tick (if any) clears timer_in_flight_. Without this the
    // WS stop + member teardown below could race process_timeouts() mid-walk of
    // the tracker map (UAF on a destroyed mutex/map).
    if (housekeeping_timer_) {
        esp_timer_stop(housekeeping_timer_);
        esp_timer_delete(housekeeping_timer_);
        housekeeping_timer_ = nullptr;
    }
    while (timer_in_flight_.load()) {
        vTaskDelay(1);
    }

    // Now stop the transport (blocks until the WS task drains); callback maps /
    // tracker are freed last by the member dtors.
    if (ws_) {
        esp_websocket_client_stop(ws_);
        esp_websocket_client_destroy(ws_);
        ws_ = nullptr;
    }
}

// ---------------------------------------------------------------------------
// Connection lifecycle
// ---------------------------------------------------------------------------

int EspMoonrakerClient::connect(const char* url, std::function<void()> on_connected,
                                std::function<void()> on_disconnected) {
    if (!url) {
        ESP_LOGE(TAG, "connect: null url");
        return -1;
    }
    url_ = url;
    // v1 is plain LAN ws:// only; TLS is a later cert-bundle decision.
    if (url_.rfind("wss://", 0) == 0) {
        ESP_LOGE(TAG, "connect: wss:// not supported in v1 (%s)", url_.c_str());
        return -1;
    }

    on_connected_ = std::move(on_connected);
    on_disconnected_ = std::move(on_disconnected);

    // A prior connect()/probe may have left a live client or a suspended
    // reconnect timeout. Start clean and unconditionally re-arm reconnection —
    // a transient set_auto_reconnect(false) is reset here by contract.
    if (ws_) {
        esp_websocket_client_stop(ws_);
        esp_websocket_client_destroy(ws_);
        ws_ = nullptr;
    }
    auto_reconnect_.store(true);
    next_reconnect_delay_ms_ = reconnect_min_delay_ms_;
    was_connected_ = false;

    esp_websocket_client_config_t cfg = {};
    cfg.uri = url_.c_str();
    // 4096 default is too small once nlohmann is on the callback path.
    cfg.task_stack = 8192;
    // Bounds the per-DATA-event chunk, not the message; we reassemble.
    cfg.buffer_size = 32768;
    cfg.network_timeout_ms = static_cast<int>(connection_timeout_ms_);
    cfg.ping_interval_sec = 10;
    // Auto-reconnect stays ON (fixed 10s); we drive exponential backoff manually
    // via esp_websocket_client_set_reconnect_timeout() from the disconnect handler.
    cfg.disable_auto_reconnect = false;
    cfg.reconnect_timeout_ms = next_reconnect_delay_ms_;

    ws_ = esp_websocket_client_init(&cfg);
    if (!ws_) {
        ESP_LOGE(TAG, "esp_websocket_client_init failed");
        return -1;
    }

    esp_websocket_register_events(ws_, WEBSOCKET_EVENT_ANY,
                                  &EspMoonrakerClient::ws_event_trampoline, this);

    set_state(ConnectionState::CONNECTING);

    esp_err_t err = esp_websocket_client_start(ws_);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_websocket_client_start failed: %s", esp_err_to_name(err));
        set_state(ConnectionState::FAILED);
        return -1;
    }
    return 0;
}

void EspMoonrakerClient::disconnect() {
    if (ws_) {
        esp_websocket_client_stop(ws_);
    }
    set_state(ConnectionState::DISCONNECTED);
}

bool EspMoonrakerClient::is_connected() const {
    return ws_ && esp_websocket_client_is_connected(ws_);
}

// ---------------------------------------------------------------------------
// State + events
// ---------------------------------------------------------------------------

void EspMoonrakerClient::set_state(ConnectionState next) {
    ConnectionState prev;
    std::function<void(ConnectionState, ConnectionState)> cb;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        if (state_ == next) {
            return;
        }
        prev = state_;
        state_ = next;
        cb = state_change_cb_;
        if (next == ConnectionState::RECONNECTING) {
            reconnecting_since_us_ = now_us();
        }
    }
    if (cb) {
        try {
            cb(prev, next);
        } catch (const std::exception& e) {
            ESP_LOGE(TAG, "state_change_cb threw: %s", e.what());
        } catch (...) {
            ESP_LOGE(TAG, "state_change_cb threw unknown");
        }
    }
}

ConnectionState EspMoonrakerClient::get_connection_state() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return state_;
}

void EspMoonrakerClient::emit_event(MoonrakerEventType type, const std::string& message,
                                    bool is_error, const std::string& details) {
    MoonrakerEventCallback handler;
    {
        std::lock_guard<std::mutex> lock(event_mutex_);
        handler = event_handler_;
    }
    if (!handler) {
        return;
    }
    MoonrakerEvent ev{type, message, details, is_error};
    try {
        handler(ev);
    } catch (const std::exception& e) {
        ESP_LOGE(TAG, "event handler threw: %s", e.what());
    } catch (...) {
        ESP_LOGE(TAG, "event handler threw unknown");
    }
}

// ---------------------------------------------------------------------------
// WebSocket event handling (runs on the websocket_task)
// ---------------------------------------------------------------------------

void EspMoonrakerClient::ws_event_trampoline(void* arg, esp_event_base_t /*base*/,
                                             int32_t event_id, void* event_data) {
    auto* self = static_cast<EspMoonrakerClient*>(arg);
    if (!self || !self->alive_.load()) {
        return;
    }
    switch (event_id) {
    case WEBSOCKET_EVENT_CONNECTED:
        self->on_ws_connected();
        break;
    case WEBSOCKET_EVENT_DISCONNECTED:
    case WEBSOCKET_EVENT_CLOSED:
        self->on_ws_disconnected();
        break;
    case WEBSOCKET_EVENT_DATA:
        self->on_ws_data(static_cast<const esp_websocket_event_data_t*>(event_data));
        break;
    case WEBSOCKET_EVENT_ERROR:
        ESP_LOGW(TAG, "websocket transport error");
        break;
    default:
        break;
    }
}

void EspMoonrakerClient::on_ws_connected() {
    ESP_LOGI(TAG, "connected to %s", url_.c_str());
    // Reset exponential backoff for the next disconnect.
    next_reconnect_delay_ms_ = reconnect_min_delay_ms_;
    if (ws_) {
        esp_websocket_client_set_reconnect_timeout(ws_, next_reconnect_delay_ms_);
    }
    // shrink the reassembly buffer back down after a session's peak.
    rx_buf_.clear();
    rx_buf_.shrink_to_fit();
    rx_skip_ = false;

    set_state(ConnectionState::CONNECTED);
    // Only a genuine reconnection emits RECONNECTED; the first-ever connect is
    // silent (desktop was_connected_ guard, moonraker_client.cpp:483-485).
    if (was_connected_) {
        emit_event(MoonrakerEventType::RECONNECTED, "Connected to Moonraker", false);
    }
    was_connected_ = true;

    if (on_connected_) {
        try {
            on_connected_();
        } catch (const std::exception& e) {
            ESP_LOGE(TAG, "on_connected threw: %s", e.what());
        } catch (...) {
            ESP_LOGE(TAG, "on_connected threw unknown");
        }
    }

    std::vector<std::pair<std::string, std::function<void()>>> observers;
    {
        std::lock_guard<std::mutex> lock(observers_mutex_);
        observers.reserve(connected_observers_.size());
        for (const auto& [name, fn] : connected_observers_) {
            observers.emplace_back(name, fn);
        }
    }
    for (auto& [name, fn] : observers) {
        if (!fn) {
            continue;
        }
        try {
            fn();
        } catch (const std::exception& e) {
            ESP_LOGE(TAG, "connected observer '%s' threw: %s", name.c_str(), e.what());
        } catch (...) {
            ESP_LOGE(TAG, "connected observer '%s' threw unknown", name.c_str());
        }
    }
}

void EspMoonrakerClient::on_ws_disconnected() {
    ESP_LOGW(TAG, "disconnected from %s", url_.c_str());

    if (auto_reconnect_.load()) {
        // Manual exponential backoff: apply the current delay, then double up to
        // max for the following attempt. Safe to call from the disconnect handler.
        if (ws_) {
            esp_websocket_client_set_reconnect_timeout(ws_, next_reconnect_delay_ms_);
        }
        next_reconnect_delay_ms_ = std::min(next_reconnect_delay_ms_ * 2, reconnect_max_delay_ms_);
        set_state(ConnectionState::RECONNECTING);
    } else {
        // Reconnection suspended (probe flow): neutralize the component's own
        // fixed-interval auto-reconnect by pushing the next attempt effectively
        // never, and report a terminal DISCONNECTED rather than RECONNECTING.
        if (ws_) {
            esp_websocket_client_set_reconnect_timeout(ws_, INT32_MAX);
        }
        set_state(ConnectionState::DISCONNECTED);
    }
    emit_event(MoonrakerEventType::CONNECTION_LOST, "Connection to Moonraker lost", true);

    // Fail every in-flight request with connection_lost (two-phase).
    std::vector<std::function<void()>> cleanup;
    {
        std::lock_guard<std::mutex> lock(requests_mutex_);
        cleanup.reserve(pending_.size());
        for (auto& [id, req] : pending_) {
            if (req.error_cb) {
                MoonrakerError err = MoonrakerError::connection_lost(req.method);
                auto cb = req.error_cb;
                cleanup.emplace_back([cb, err]() {
                    try {
                        cb(err);
                    } catch (...) {
                    }
                });
            }
        }
        pending_.clear();
    }
    for (auto& fn : cleanup) {
        fn();
    }

    if (on_disconnected_) {
        try {
            on_disconnected_();
        } catch (const std::exception& e) {
            ESP_LOGE(TAG, "on_disconnected threw: %s", e.what());
        } catch (...) {
            ESP_LOGE(TAG, "on_disconnected threw unknown");
        }
    }
}

void EspMoonrakerClient::on_ws_data(const esp_websocket_event_data_t* d) {
    if (!d) {
        return;
    }
    // Only text (0x01) and its continuation frames (0x00) carry JSON-RPC.
    if (d->op_code != kOpText && d->op_code != kOpContinuation) {
        return;
    }

    if (d->payload_offset == 0) {
        rx_buf_.clear();
        rx_skip_ = (static_cast<size_t>(d->payload_len) > kMaxMessageBytes);
        if (rx_skip_) {
            ESP_LOGE(TAG, "dropping %d-byte message (cap 256KB)", d->payload_len);
        } else {
            rx_buf_.reserve(
                std::min(static_cast<size_t>(d->payload_len), kMaxMessageBytes));
        }
    }

    if (!rx_skip_ && d->data_ptr && d->data_len > 0) {
        rx_buf_.append(d->data_ptr, static_cast<size_t>(d->data_len));
    }

    // Message complete when this chunk reaches the declared payload length.
    const bool complete =
        (d->payload_offset + d->data_len) >= d->payload_len;
    if (complete) {
        if (!rx_skip_ && !rx_buf_.empty()) {
            dispatch_message(rx_buf_.data(), rx_buf_.size());
        }
        rx_buf_.clear();
        rx_skip_ = false;
    }
}

void EspMoonrakerClient::dispatch_message(const char* buf, size_t len) {
    json msg = json::parse(buf, buf + len, nullptr, /*allow_exceptions=*/false);
    if (msg.is_discarded()) {
        ESP_LOGW(TAG, "dropping unparseable %zu-byte message", len);
        return;
    }

    // Response (has "id") → request tracker.
    if (msg.contains("id") && msg["id"].is_number_integer()) {
        uint64_t id = msg["id"].get<uint64_t>();
        std::function<void(const json&)> success_cb;
        std::function<void(const MoonrakerError&)> error_cb;
        std::string method;
        bool silent = false;
        bool found = false;
        bool has_error = msg.contains("error");
        {
            std::lock_guard<std::mutex> lock(requests_mutex_);
            auto it = pending_.find(id);
            if (it != pending_.end()) {
                found = true;
                method = it->second.method;
                silent = it->second.silent;
                if (has_error) {
                    error_cb = it->second.error_cb;
                } else {
                    success_cb = it->second.success_cb;
                }
                pending_.erase(it);
            }
        }
        if (found) {
            if (has_error) {
                MoonrakerError err = MoonrakerError::from_json_rpc(msg["error"], method);
                if (!silent && !error_cb) {
                    emit_event(MoonrakerEventType::RPC_ERROR,
                               "Printer command '" + method + "' failed: " + err.message, true,
                               method);
                }
                if (error_cb) {
                    try {
                        error_cb(err);
                    } catch (const std::exception& e) {
                        ESP_LOGE(TAG, "error cb for '%s' threw: %s", method.c_str(), e.what());
                    } catch (...) {
                    }
                }
            } else if (success_cb) {
                try {
                    success_cb(msg);
                } catch (const std::exception& e) {
                    ESP_LOGE(TAG, "success cb for '%s' threw: %s", method.c_str(), e.what());
                } catch (...) {
                }
            }
        }
    }

    // Notification (has "method") → notify + method + bed-mesh callbacks.
    if (msg.contains("method") && msg["method"].is_string()) {
        dispatch_notification(msg, /*include_method_callbacks=*/true);
    }
}

void EspMoonrakerClient::dispatch_notification(const json& msg, bool include_method_callbacks) {
    if (!msg.contains("method") || !msg["method"].is_string()) {
        return;
    }
    std::string method = msg["method"].get<std::string>();

    std::vector<std::function<void(const json&)>> to_invoke;
    std::function<void(const json&)> bed_mesh_cb;
    {
        std::lock_guard<std::mutex> lock(callbacks_mutex_);
        if (method == "notify_status_update" || method == "notify_filelist_changed") {
            to_invoke.reserve(notify_callbacks_.size());
            for (const auto& [id, cb] : notify_callbacks_) {
                to_invoke.push_back(cb);
            }
        }
        // Method-specific handlers fire only for genuine inbound notifications;
        // the synthetic dispatch_status_update path is notify-only (desktop parity).
        if (include_method_callbacks) {
            auto it = method_callbacks_.find(method);
            if (it != method_callbacks_.end()) {
                for (const auto& [handler, cb] : it->second) {
                    to_invoke.push_back(cb);
                }
            }
        }
        bed_mesh_cb = bed_mesh_callback_;
    }

    // Extract bed mesh before user callbacks (mirrors desktop ordering): a
    // notify_status_update carries params[0].bed_mesh on the containing object.
    if (bed_mesh_cb && method == "notify_status_update" && msg.contains("params") &&
        msg["params"].is_array() && !msg["params"].empty()) {
        const json& params0 = msg["params"][0];
        if (params0.contains("bed_mesh") && params0["bed_mesh"].is_object()) {
            try {
                bed_mesh_cb(params0["bed_mesh"]);
            } catch (const std::exception& e) {
                ESP_LOGE(TAG, "bed_mesh callback threw: %s", e.what());
            } catch (...) {
            }
        }
    }

    for (auto& cb : to_invoke) {
        if (!cb) {
            continue;
        }
        try {
            cb(msg);
        } catch (const std::exception& e) {
            ESP_LOGE(TAG, "callback for '%s' threw: %s", method.c_str(), e.what());
        } catch (...) {
        }
    }
}

// ---------------------------------------------------------------------------
// Housekeeping timer (timeouts + informational FAILED transition)
// ---------------------------------------------------------------------------

void EspMoonrakerClient::housekeeping_trampoline(void* arg) {
    auto* self = static_cast<EspMoonrakerClient*>(arg);
    if (!self) {
        return;
    }
    // Mark the tick in-flight BEFORE reading any member so the dtor's quiesce
    // loop (which runs after esp_timer_delete) always observes an overlapping
    // callback and waits it out.
    self->timer_in_flight_.store(true);
    if (!self->alive_.load()) {
        self->timer_in_flight_.store(false);
        return;
    }

    self->process_timeouts();

    // 60s of RECONNECTING → FAILED (purely informational; reconnect continues).
    bool to_failed = false;
    {
        std::lock_guard<std::mutex> lock(self->state_mutex_);
        if (self->state_ == ConnectionState::RECONNECTING &&
            (now_us() - self->reconnecting_since_us_) > kReconnectingToFailedUs) {
            to_failed = true;
        }
    }
    if (to_failed) {
        self->set_state(ConnectionState::FAILED);
        self->emit_event(MoonrakerEventType::CONNECTION_FAILED,
                         "Reconnection has not succeeded", true);
    }

    self->timer_in_flight_.store(false);
}

void EspMoonrakerClient::process_timeouts() {
    struct TimedOut {
        std::string method;
        bool silent;
        std::function<void(const MoonrakerError&)> cb;
        MoonrakerError err;
    };
    std::vector<TimedOut> timed_out;
    const int64_t now = now_us();
    {
        std::lock_guard<std::mutex> lock(requests_mutex_);
        // Defense in depth: bail under the lock if teardown began after the
        // trampoline's entry check (the dtor's quiesce loop still waits on us).
        if (!alive_.load()) {
            return;
        }
        for (auto it = pending_.begin(); it != pending_.end();) {
            const int64_t age_us = now - it->second.sent_us;
            if (age_us > static_cast<int64_t>(it->second.timeout_ms) * 1000) {
                TimedOut t;
                t.method = it->second.method;
                t.silent = it->second.silent;
                t.cb = it->second.error_cb;
                t.err = MoonrakerError::timeout(it->second.method, it->second.timeout_ms);
                timed_out.push_back(std::move(t));
                it = pending_.erase(it);
            } else {
                ++it;
            }
        }
    }
    for (auto& t : timed_out) {
        if (!t.silent) {
            emit_event(MoonrakerEventType::REQUEST_TIMEOUT,
                       "Printer command '" + t.method + "' timed out", false, t.method);
        }
        if (t.cb) {
            try {
                t.cb(t.err);
            } catch (...) {
            }
        }
    }
}

// ---------------------------------------------------------------------------
// JSON-RPC send path
// ---------------------------------------------------------------------------

int EspMoonrakerClient::send_envelope(const json& envelope) {
    if (!is_connected()) {
        return -1;
    }
    std::string payload = envelope.dump();
    int sent = esp_websocket_client_send_text(ws_, payload.data(),
                                              static_cast<int>(payload.size()),
                                              pdMS_TO_TICKS(connection_timeout_ms_));
    return sent;
}

int EspMoonrakerClient::send_jsonrpc(const std::string& method) {
    return send_jsonrpc(method, json());
}

int EspMoonrakerClient::send_jsonrpc(const std::string& method, const json& params) {
    if (!is_connected()) {
        return -1;
    }
    json rpc;
    rpc["jsonrpc"] = "2.0";
    rpc["method"] = method;
    rpc["id"] = next_request_id_.fetch_add(1) + 1;
    if (!params.is_null() && !params.empty()) {
        rpc["params"] = params;
    }
    int result = send_envelope(rpc);
    return result < 0 ? result : 0;
}

RequestId EspMoonrakerClient::send_jsonrpc(const std::string& method, const json& params,
                                           std::function<void(const json&)> cb) {
    return send_jsonrpc(method, params, std::move(cb), nullptr, 0, false);
}

RequestId EspMoonrakerClient::send_jsonrpc(const std::string& method, const json& params,
                                           std::function<void(const json&)> success_cb,
                                           std::function<void(const MoonrakerError&)> error_cb,
                                           uint32_t timeout_ms, bool silent) {
    if (!is_connected()) {
        // Fail fast so callers don't wait on a request that never times out.
        if (error_cb) {
            try {
                error_cb(MoonrakerError::connection_lost(method));
            } catch (...) {
            }
        }
        return INVALID_REQUEST_ID;
    }
    return track_and_send(method, params, std::move(success_cb), std::move(error_cb), timeout_ms,
                          silent);
}

RequestId EspMoonrakerClient::track_and_send(const std::string& method, const json& params,
                                             std::function<void(const json&)> success_cb,
                                             std::function<void(const MoonrakerError&)> error_cb,
                                             uint32_t timeout_ms, bool silent) {
    RequestId id = next_request_id_.fetch_add(1) + 1;

    bool queue_full = false;
    {
        std::lock_guard<std::mutex> lock(requests_mutex_);
        if (pending_.size() >= kMaxPendingRequests) {
            queue_full = true;
        } else {
            Pending req;
            req.method = method;
            req.success_cb = success_cb;
            req.error_cb = error_cb;
            req.sent_us = now_us();
            req.timeout_ms = (timeout_ms > 0) ? timeout_ms : default_request_timeout_ms_;
            req.silent = silent;
            pending_.emplace(id, std::move(req));
        }
    }

    if (queue_full) {
        ESP_LOGW(TAG, "request queue full (%zu), rejecting %s", kMaxPendingRequests,
                 method.c_str());
        if (error_cb) {
            MoonrakerError err;
            err.type = MoonrakerErrorType::CONNECTION_LOST;
            err.method = method;
            err.message = "Request queue full — too many pending requests";
            try {
                error_cb(err);
            } catch (...) {
            }
        }
        return INVALID_REQUEST_ID;
    }

    json rpc;
    rpc["jsonrpc"] = "2.0";
    rpc["method"] = method;
    rpc["id"] = id;
    if (!params.is_null() && !params.empty()) {
        rpc["params"] = params;
    }

    int result = send_envelope(rpc);
    if (result < 0) {
        // Send failed — drop the pending entry and report connection_lost.
        std::function<void(const MoonrakerError&)> cb_copy;
        {
            std::lock_guard<std::mutex> lock(requests_mutex_);
            auto it = pending_.find(id);
            if (it != pending_.end()) {
                cb_copy = it->second.error_cb;
                pending_.erase(it);
            }
        }
        if (cb_copy) {
            try {
                cb_copy(MoonrakerError::connection_lost(method));
            } catch (...) {
            }
        }
        return INVALID_REQUEST_ID;
    }
    return id;
}

int EspMoonrakerClient::gcode_script(const std::string& gcode) {
    json params = {{"script", gcode}};
    int result = send_jsonrpc("printer.gcode.script", params);
    return result < 0 ? result : 0;
}

void EspMoonrakerClient::get_gcode_store(
    int count, std::function<void(const std::vector<GcodeStoreEntry>&)> on_success,
    std::function<void(const MoonrakerError&)> on_error) {
    json params = {{"count", count}};
    send_jsonrpc(
        "server.gcode_store", params,
        [on_success](const json& response) {
            std::vector<GcodeStoreEntry> entries;
            if (response.contains("result") && response["result"].contains("gcode_store")) {
                const auto& store = response["result"]["gcode_store"];
                entries.reserve(store.size());
                for (const auto& item : store) {
                    GcodeStoreEntry entry;
                    entry.message = item.value("message", "");
                    entry.time = item.value("time", 0.0);
                    entry.type = item.value("type", "response");
                    entries.push_back(std::move(entry));
                }
            }
            if (on_success) {
                on_success(entries);
            }
        },
        on_error);
}

void EspMoonrakerClient::get_temperature_store(
    std::function<void(const TemperatureStore&)> on_success,
    std::function<void(const MoonrakerError&)> on_error) {
    json params = {{"include_monitors", false}};
    send_jsonrpc(
        "server.temperature_store", params,
        [on_success](const json& response) {
            TemperatureStore store;
            if (response.contains("result") && response["result"].is_object()) {
                for (const auto& [key, series_json] : response["result"].items()) {
                    if (!series_json.is_object()) {
                        continue;
                    }
                    TemperatureStoreSeries series;
                    auto load = [&series_json](const char* field, std::vector<float>& out) {
                        if (series_json.contains(field) && series_json[field].is_array()) {
                            for (const auto& v : series_json[field]) {
                                if (v.is_number()) {
                                    out.push_back(v.get<float>());
                                }
                            }
                        }
                    };
                    load("temperatures", series.temperatures);
                    load("targets", series.targets);
                    load("powers", series.powers);
                    store.emplace(key, std::move(series));
                }
            }
            if (on_success) {
                on_success(store);
            }
        },
        on_error);
}

// ---------------------------------------------------------------------------
// Discovery (Plan 4 fills the real sequence; v1 = a server.info round-trip)
// ---------------------------------------------------------------------------

void EspMoonrakerClient::discover_printer(std::function<void()> on_complete,
                                          std::function<void(const std::string&)> on_error) {
    if (!is_connected()) {
        if (on_error) {
            on_error("not connected");
        }
        return;
    }
    send_jsonrpc(
        "server.info", json(),
        [this, on_complete](const json& response) {
            // v1 stub: no object parsing yet (Plan 4), but honor the registered
            // discovery callbacks so consumers wire the same way as on desktop.
            std::function<void(const helix::PrinterDiscovery&)> hw_cb;
            std::function<void(const helix::PrinterDiscovery&, const json&)> done_cb;
            {
                std::lock_guard<std::mutex> lock(callbacks_mutex_);
                hw_cb = on_hardware_discovered_;
                done_cb = on_discovery_complete_;
            }
            if (hw_cb) {
                try {
                    hw_cb(hardware_);
                } catch (const std::exception& e) {
                    ESP_LOGE(TAG, "on_hardware_discovered callback threw: %s", e.what());
                } catch (...) {
                }
            }
            if (done_cb) {
                try {
                    done_cb(hardware_, response);
                } catch (const std::exception& e) {
                    ESP_LOGE(TAG, "on_discovery_complete callback threw: %s", e.what());
                } catch (...) {
                }
            }
            if (on_complete) {
                on_complete();
            }
        },
        [on_error](const MoonrakerError& err) {
            if (on_error) {
                on_error(err.message);
            }
        });
}

PrinterDiscovery EspMoonrakerClient::hardware() const {
    return hardware_;
}

void EspMoonrakerClient::parse_objects(const json& /*objects*/) {
    // Real object parsing lands with discovery in Plan 4.
    spdlog::debug("[helixnet] parse_objects: no-op in v1 (Plan 4)");
}

void EspMoonrakerClient::clear_discovery_cache() {
    spdlog::debug("[helixnet] clear_discovery_cache: no-op in v1 (Plan 4)");
}

void EspMoonrakerClient::set_on_hardware_discovered(
    std::function<void(const helix::PrinterDiscovery&)> cb) {
    std::lock_guard<std::mutex> lock(callbacks_mutex_);
    on_hardware_discovered_ = std::move(cb);
}

void EspMoonrakerClient::set_on_discovery_complete(
    std::function<void(const helix::PrinterDiscovery&, const json&)> cb) {
    std::lock_guard<std::mutex> lock(callbacks_mutex_);
    on_discovery_complete_ = std::move(cb);
}

void EspMoonrakerClient::set_bed_mesh_callback(std::function<void(const json&)> callback) {
    std::lock_guard<std::mutex> lock(callbacks_mutex_);
    bed_mesh_callback_ = std::move(callback);
}

// ---------------------------------------------------------------------------
// Subscriptions & method callbacks
// ---------------------------------------------------------------------------

SubscriptionId EspMoonrakerClient::register_notify_update(std::function<void(const json&)> cb) {
    if (!cb) {
        return INVALID_SUBSCRIPTION_ID;
    }
    SubscriptionId id = next_subscription_id_.fetch_add(1) + 1;
    std::lock_guard<std::mutex> lock(callbacks_mutex_);
    notify_callbacks_.emplace(id, std::move(cb));
    return id;
}

bool EspMoonrakerClient::unsubscribe_notify_update(SubscriptionId id) {
    std::lock_guard<std::mutex> lock(callbacks_mutex_);
    return notify_callbacks_.erase(id) > 0;
}

void EspMoonrakerClient::register_method_callback(const std::string& method,
                                                  const std::string& handler_name,
                                                  std::function<void(const json&)> cb) {
    std::lock_guard<std::mutex> lock(callbacks_mutex_);
    method_callbacks_[method][handler_name] = std::move(cb);
}

bool EspMoonrakerClient::unregister_method_callback(const std::string& method,
                                                    const std::string& handler_name) {
    std::lock_guard<std::mutex> lock(callbacks_mutex_);
    auto it = method_callbacks_.find(method);
    if (it == method_callbacks_.end()) {
        return false;
    }
    bool removed = it->second.erase(handler_name) > 0;
    if (it->second.empty()) {
        method_callbacks_.erase(it);
    }
    return removed;
}

void EspMoonrakerClient::dispatch_status_update(const json& status) {
    // Wrap raw status into the notify_status_update envelope and route it through
    // the same fan-out an incoming WS notification would take. [status, eventtime].
    json wrapped;
    wrapped["method"] = "notify_status_update";
    wrapped["params"] = json::array({status, 0.0});
    // Notify-only fan-out (no method_callbacks_), matching desktop
    // dispatch_status_update semantics.
    dispatch_notification(wrapped, /*include_method_callbacks=*/false);
}

// ---------------------------------------------------------------------------
// Observers & reconnection
// ---------------------------------------------------------------------------

void EspMoonrakerClient::add_connected_observer(const std::string& handler_name,
                                                std::function<void()> cb) {
    bool fire_now = false;
    std::function<void()> immediate;
    {
        std::lock_guard<std::mutex> lock(observers_mutex_);
        connected_observers_[handler_name] = cb;
    }
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        if (state_ == ConnectionState::CONNECTED) {
            fire_now = true;
            immediate = cb;
        }
    }
    if (fire_now && immediate) {
        try {
            immediate();
        } catch (...) {
        }
    }
}

bool EspMoonrakerClient::remove_connected_observer(const std::string& handler_name) {
    std::lock_guard<std::mutex> lock(observers_mutex_);
    return connected_observers_.erase(handler_name) > 0;
}

void EspMoonrakerClient::set_auto_reconnect(bool enabled) {
    auto_reconnect_.store(enabled);
    if (!ws_) {
        return; // next connect() installs reconnect settings from scratch
    }
    if (enabled) {
        // Re-arm: restore the exponential-backoff floor.
        next_reconnect_delay_ms_ = reconnect_min_delay_ms_;
        esp_websocket_client_set_reconnect_timeout(ws_, reconnect_min_delay_ms_);
    } else {
        // Suspend background reconnection WITHOUT dropping the current connection
        // (mirrors desktop setReconnect(nullptr)). The component has no runtime
        // disable API, so we push its fixed-interval retry effectively never;
        // set_reconnect_timeout is a plain field write, safe from any task.
        esp_websocket_client_set_reconnect_timeout(ws_, INT32_MAX);
    }
}

void EspMoonrakerClient::force_reconnect() {
    if (!ws_) {
        return;
    }
    next_reconnect_delay_ms_ = reconnect_min_delay_ms_;
    esp_websocket_client_stop(ws_);
    set_state(ConnectionState::CONNECTING);
    esp_websocket_client_start(ws_);
}

// ---------------------------------------------------------------------------
// Events & modal suppression
// ---------------------------------------------------------------------------

void EspMoonrakerClient::register_event_handler(MoonrakerEventCallback cb) {
    std::lock_guard<std::mutex> lock(event_mutex_);
    event_handler_ = std::move(cb);
}

void EspMoonrakerClient::suppress_disconnect_modal(uint32_t duration_ms) {
    suppress_modal_until_us_.store(now_us() + static_cast<int64_t>(duration_ms) * 1000);
}

bool EspMoonrakerClient::is_disconnect_modal_suppressed() const {
    return now_us() < suppress_modal_until_us_.load();
}

// ---------------------------------------------------------------------------
// Request management & configuration
// ---------------------------------------------------------------------------

bool EspMoonrakerClient::cancel_request(RequestId id) {
    if (id == INVALID_REQUEST_ID) {
        return false;
    }
    std::lock_guard<std::mutex> lock(requests_mutex_);
    return pending_.erase(id) > 0;
}

void EspMoonrakerClient::set_state_change_callback(
    std::function<void(ConnectionState, ConnectionState)> cb) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    state_change_cb_ = std::move(cb);
}

void EspMoonrakerClient::set_connection_timeout(uint32_t timeout_ms) {
    connection_timeout_ms_ = timeout_ms;
}

void EspMoonrakerClient::set_default_request_timeout(uint32_t timeout_ms) {
    default_request_timeout_ms_ = timeout_ms;
}

void EspMoonrakerClient::configure_timeouts(uint32_t connection_timeout_ms,
                                            uint32_t request_timeout_ms,
                                            uint32_t /*keepalive_interval_ms*/,
                                            uint32_t reconnect_min_delay_ms,
                                            uint32_t reconnect_max_delay_ms) {
    connection_timeout_ms_ = connection_timeout_ms;
    default_request_timeout_ms_ = request_timeout_ms;
    reconnect_min_delay_ms_ = static_cast<int>(reconnect_min_delay_ms);
    reconnect_max_delay_ms_ = static_cast<int>(reconnect_max_delay_ms);
    next_reconnect_delay_ms_ = reconnect_min_delay_ms_;
}

void EspMoonrakerClient::toggle_filament_runout_simulation() {
    // Simulation hook is a no-op in production (mirrors desktop).
}

std::weak_ptr<bool> EspMoonrakerClient::lifetime_weak() const {
    return lifetime_;
}

// ---------------------------------------------------------------------------
// Platform factory
// ---------------------------------------------------------------------------

std::unique_ptr<IMoonrakerClient> create_platform_moonraker_client() {
    return std::make_unique<EspMoonrakerClient>();
}

} // namespace helix

// Force-link probe: keeps the client in the image so the size gate accounts for
// its real cost even before Task 10 wires it into app_main. Never executed.
extern "C" void helixnet_link_probe(void) {
    auto client = helix::create_platform_moonraker_client();
    (void)client;
}
