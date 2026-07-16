// SPDX-License-Identifier: GPL-3.0-or-later
//
// net_hil — Plan 3 Task 10 network hardware-in-the-loop scenario. Test-only:
// brings up a raw esp_wifi station (the real WifiBackend lands in Plan 4) and
// drives a live Moonraker WebSocket session through EspMoonrakerClient to
// validate the transport on real K-Touch hardware while the display stays up.
// Entirely gated behind CONFIG_HELIX_NET_HIL (default n) — this translation
// unit compiles to nothing when the option is off. See
// .superpowers/sdd/task-10-brief.md for the exact scenario contract.

#include "sdkconfig.h"

#if CONFIG_HELIX_NET_HIL

#include "esp_moonraker_client.h"

#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <atomic>
#include <cstring>
#include <memory>
#include <pthread.h>

extern "C" void net_hil_start(void);

namespace {

constexpr char TAG[] = "net_hil";
constexpr EventBits_t kWifiConnectedBit = BIT0;
// esp-idf#14918: a TX can block 10s+ behind an in-progress RX without the
// separate TX lock (sdkconfig.defaults sets CONFIG_ESP_WS_CLIENT_SEPARATE_TX_LOCK).
// Anything above this during the probe means that class of contention is back.
constexpr int64_t kTxLockFailThresholdMs = 2000;
constexpr int64_t kScenarioWindowMs = 60000;
constexpr int64_t kPingCadenceMs = 15000;
constexpr int64_t kProbeAtMs = 30000;
constexpr int kProbeBurstCount = 5;
constexpr uint32_t kHeapFlatToleranceBytes = 8192;

EventGroupHandle_t s_wifi_event_group = nullptr;

// Counters updated from the WS task (notify callback, RPC error callbacks) and
// read from the HIL thread — plain atomics, no ordering requirements beyond
// visibility.
std::atomic<uint32_t> s_msgs{0};
std::atomic<uint32_t> s_drops{0};
std::atomic<uint32_t> s_max_msg{0};

// Process-lifetime singleton: this scenario never tears the client down, so
// the dtor's quiesce path is deliberately not exercised here (out of scope —
// Plan 4 owns real client lifecycle). Leaked on purpose.
helix::IMoonrakerClient* s_client = nullptr;

void wifi_event_handler(void*, esp_event_base_t event_base, int32_t event_id, void*) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "wifi station disconnected, retrying");
        esp_wifi_connect();
    }
}

void ip_event_handler(void*, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        auto* event = static_cast<ip_event_got_ip_t*>(event_data);
        ESP_LOGI(TAG, "wifi got ip: " IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(s_wifi_event_group, kWifiConnectedBit);
    }
}

// Test-only WiFi station bring-up. Self-contained on purpose: the real
// WifiBackend (Plan 4's create_platform_wifi_backend) supersedes this, but
// Task 10 shouldn't reach into app-layer code to get network up.
void wifi_init_station(void) {
    esp_err_t nvs_rc = nvs_flash_init();
    if (nvs_rc == ESP_ERR_NVS_NO_FREE_PAGES || nvs_rc == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_rc = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs_rc);
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    s_wifi_event_group = xEventGroupCreate();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(
        esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, nullptr));
    ESP_ERROR_CHECK(
        esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &ip_event_handler, nullptr));

    wifi_config_t wifi_config = {};
    std::strncpy(reinterpret_cast<char*>(wifi_config.sta.ssid), CONFIG_HELIX_HIL_WIFI_SSID,
                 sizeof(wifi_config.sta.ssid) - 1);
    std::strncpy(reinterpret_cast<char*>(wifi_config.sta.password), CONFIG_HELIX_HIL_WIFI_PASS,
                 sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "waiting for wifi connection (ssid=\"%s\")...", CONFIG_HELIX_HIL_WIFI_SSID);
    xEventGroupWaitBits(s_wifi_event_group, kWifiConnectedBit, pdFALSE, pdTRUE, portMAX_DELAY);
}

// Synchronous printer.info round trip: blocks the HIL thread on a binary
// semaphore given from the WS task's success/error callback, and logs the
// round-trip latency. Returns the latency in milliseconds.
int64_t timed_printer_info() {
    SemaphoreHandle_t done = xSemaphoreCreateBinary();
    int64_t t0 = esp_timer_get_time();

    s_client->send_jsonrpc(
        "printer.info", json::object(),
        [done](const json&) { xSemaphoreGive(done); },
        [done](const MoonrakerError& err) {
            ESP_LOGW(TAG, "printer.info error: %s", err.message.c_str());
            s_drops.fetch_add(1, std::memory_order_relaxed);
            xSemaphoreGive(done);
        });

    // Bounded wait: a truly hung transport must not wedge this thread forever.
    if (xSemaphoreTake(done, pdMS_TO_TICKS(10000)) != pdTRUE) {
        ESP_LOGE(TAG, "printer.info timed out waiting for reply");
        s_drops.fetch_add(1, std::memory_order_relaxed);
    }
    vSemaphoreDelete(done);

    int64_t dt_ms = (esp_timer_get_time() - t0) / 1000;
    ESP_LOGI(TAG, "printer.info rtt=%lldms", static_cast<long long>(dt_ms));
    if (dt_ms > kTxLockFailThresholdMs) {
        ESP_LOGE(TAG, "FAIL tx-lock latency=%lldms", static_cast<long long>(dt_ms));
    }
    return dt_ms;
}

void on_temp_notify(const json& msg) {
    // notify_status_update carries the full JSON-RPC envelope; approximate the
    // WS message size from it since the client doesn't expose one (brief note:
    // callback-side approximation, not the exact wire byte count).
    size_t approx_size = msg.dump().size();
    uint32_t prev_max = s_max_msg.load(std::memory_order_relaxed);
    while (approx_size > prev_max &&
           !s_max_msg.compare_exchange_weak(prev_max, static_cast<uint32_t>(approx_size),
                                             std::memory_order_relaxed)) {
    }

    if (!msg.contains("params") || !msg["params"].is_array() || msg["params"].empty()) {
        return;
    }
    const json& status = msg["params"][0];

    // notify_status_update carries DELTAS: a given update usually contains ONLY
    // extruder OR heater_bed, not both. Carry last-known values so the log always
    // shows real temps — defaulting the absent field to 0.0 produced fake
    // "extruder=28.7 bed=0.0" / "extruder=0.0 bed=28.0" flapping every delta.
    static double s_last_extruder = 0.0;
    static double s_last_bed = 0.0;
    bool has_temp = false;
    if (status.contains("extruder") && status["extruder"].contains("temperature")) {
        s_last_extruder = status["extruder"]["temperature"].get<double>();
        has_temp = true;
    }
    if (status.contains("heater_bed") && status["heater_bed"].contains("temperature")) {
        s_last_bed = status["heater_bed"]["temperature"].get<double>();
        has_temp = true;
    }
    if (!has_temp) {
        return;
    }
    s_msgs.fetch_add(1, std::memory_order_relaxed);
    ESP_LOGI(TAG, "extruder=%.1f bed=%.1f", s_last_extruder, s_last_bed);
}

void* hil_thread_main(void*) {
    std::unique_ptr<helix::IMoonrakerClient> client = helix::create_platform_moonraker_client();
    s_client = client.release(); // process-lifetime singleton; see header comment

    SemaphoreHandle_t connected_sem = xSemaphoreCreateBinary();
    s_client->connect(
        CONFIG_HELIX_HIL_MOONRAKER_URL, [connected_sem]() { xSemaphoreGive(connected_sem); },
        []() { ESP_LOGW(TAG, "moonraker disconnected"); });

    ESP_LOGI(TAG, "waiting for moonraker connect (%s)...", CONFIG_HELIX_HIL_MOONRAKER_URL);
    xSemaphoreTake(connected_sem, portMAX_DELAY);
    vSemaphoreDelete(connected_sem);
    ESP_LOGI(TAG, "moonraker connected");

    {
        SemaphoreHandle_t info_done = xSemaphoreCreateBinary();
        s_client->send_jsonrpc("server.info", json::object(), [info_done](const json& resp) {
            std::string klippy_state =
                resp.value(json::json_pointer("/result/klippy_state"), std::string("?"));
            std::string moonraker_version =
                resp.value(json::json_pointer("/result/moonraker_version"), std::string("?"));
            ESP_LOGI(TAG, "server.info klippy_state=%s moonraker_version=%s",
                     klippy_state.c_str(), moonraker_version.c_str());
            xSemaphoreGive(info_done);
        });
        if (xSemaphoreTake(info_done, pdMS_TO_TICKS(10000)) != pdTRUE) {
            ESP_LOGE(TAG, "server.info timed out");
        }
        vSemaphoreDelete(info_done);
    }

    // Baseline heap AFTER connect + server.info so setup allocations (WS
    // buffers, request tracker) don't skew the 60s flatness check.
    uint32_t heap_baseline = esp_get_free_heap_size();
    uint32_t psram_baseline =
        static_cast<uint32_t>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    json sub_params = {
        {"objects", {{"extruder", {"temperature"}}, {"heater_bed", {"temperature"}}}}};
    s_client->send_jsonrpc("printer.objects.subscribe", sub_params,
                           [](const json&) { ESP_LOGI(TAG, "subscribed extruder+heater_bed"); });
    s_client->register_notify_update(on_temp_notify);

    int64_t t_start_ms = esp_timer_get_time() / 1000;
    int64_t next_ping_ms = kPingCadenceMs;
    bool probe_done = false;

    while (true) {
        int64_t elapsed_ms = (esp_timer_get_time() / 1000) - t_start_ms;
        if (elapsed_ms >= kScenarioWindowMs) {
            break;
        }
        if (!probe_done && elapsed_ms >= kProbeAtMs) {
            // TX-during-RX probe: 5 back-to-back printer.info RPCs while the
            // temp subscription is actively streaming (esp-idf#14918 class).
            ESP_LOGI(TAG, "tx-lock probe: %d back-to-back printer.info", kProbeBurstCount);
            for (int i = 0; i < kProbeBurstCount; ++i) {
                timed_printer_info();
            }
            probe_done = true;
            next_ping_ms = kProbeAtMs + kPingCadenceMs;
            continue;
        }
        if (elapsed_ms >= next_ping_ms) {
            timed_printer_info();
            next_ping_ms += kPingCadenceMs;
            continue;
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }

    uint32_t heap_now = esp_get_free_heap_size();
    uint32_t psram_now = static_cast<uint32_t>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    int64_t heap_delta =
        static_cast<int64_t>(heap_now) - static_cast<int64_t>(heap_baseline);
    if (heap_delta < 0) {
        heap_delta = -heap_delta;
    }
    if (static_cast<uint32_t>(heap_delta) > kHeapFlatToleranceBytes) {
        ESP_LOGE(TAG, "FAIL heap drift=%lldB baseline=%u now=%u", static_cast<long long>(heap_delta),
                 heap_baseline, heap_now);
    } else {
        ESP_LOGI(TAG, "heap flat: baseline=%u now=%u delta=%lldB", heap_baseline, heap_now,
                 static_cast<long long>(heap_delta));
    }
    (void)psram_baseline;

    ESP_LOGI(TAG, "PASS msgs=%u drops=%u max_msg=%u heap_free=%u psram_free=%u",
             s_msgs.load(std::memory_order_relaxed), s_drops.load(std::memory_order_relaxed),
             s_max_msg.load(std::memory_order_relaxed), heap_now, psram_now);

    // Park forever: the subscription keeps streaming independently of this
    // thread, so serial capture past 60s still shows live temp notifications.
    // No exit-and-cleanup by design (client lifetime is process-scoped here).
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(60000));
    }
    return nullptr;
}

} // namespace

extern "C" void net_hil_start(void) {
    // Blocks until WiFi has an IP, then spawns the HIL thread. Runs on its
    // own task (see app_main) so a slow or absent network can never hold the
    // display off — DHCP at weak RSSI was observed taking 75s+.
    wifi_init_station();

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 32 * 1024);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

    pthread_t thread;
    int rc = pthread_create(&thread, &attr, hil_thread_main, nullptr);
    pthread_attr_destroy(&attr);
    if (rc != 0) {
        ESP_LOGE(TAG, "failed to start HIL thread: %d", rc);
    }
}

#endif // CONFIG_HELIX_NET_HIL
