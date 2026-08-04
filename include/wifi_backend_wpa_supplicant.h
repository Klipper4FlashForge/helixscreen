// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "wifi_backend.h" // Base class
#include "wifi_interface.h"

#include <functional>
#include <map>
#include <string>
#include <vector>

namespace helix::wifi::detail {
/// Locate an existing network id for @p ssid in a raw LIST_NETWORKS reply
/// ("network id / ssid / bssid / flags\n" header, then tab-separated
/// "id\tssid\tbssid\tflags" per line). Returns "" when @p ssid has no saved
/// entry (including when @p list_networks_reply is empty, e.g. the control
/// connection is down). Splitting is tab-only — SSIDs may contain spaces, and
/// splitting on generic whitespace would break "my home net" apart.
///
/// Used by connect_network() to reuse a saved entry instead of stacking a
/// fresh ADD_NETWORK on every connect attempt: a real user's wpa_supplicant
/// had reached network id 7 for a handful of networks, and duplicate
/// all-enabled entries give wpa_supplicant more candidates to roam between
/// after a reboot. Exposed for unit testing.
std::string find_network_id(const std::string& list_networks_reply, const std::string& ssid);
} // namespace helix::wifi::detail

#ifndef __APPLE__
// ============================================================================
// Linux Implementation: Full wpa_supplicant integration
// ============================================================================

#include "hv/EventLoop.h"
#include "hv/EventLoopThread.h"
#include "hv/hloop.h"

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <optional>

// Forward declaration - avoid including wpa_ctrl.h in header
struct wpa_ctrl;

namespace helix::wifi::detail {
// Parse a wpa_supplicant config file's `ctrl_interface=` directive and return
// the control-socket directory it points to ("" if absent, unreadable, or not
// an absolute path). Exposed for unit testing; used by the /proc cmdline scan
// to honour `wpa_supplicant -c <conf>` launches whose ctrl_interface lives in
// the config file rather than on the command line.
std::string read_ctrl_interface_from_conf(const std::string& conf_path);

/// True if @p config_contents declares a `network={...}` block whose `ssid=`
/// matches @p ssid. Used to confirm a SAVE_CONFIG actually landed on disk.
/// Exposed for unit testing.
bool wpa_config_has_network(const std::string& config_contents, const std::string& ssid);

enum class SavePersistence {
    Persisted,    ///< Credentials are on disk; they will survive a reboot.
    NotPersisted, ///< They are not. The user's WiFi dies at the next power-off.
};

/// Decide whether a SAVE_CONFIG actually persisted @p ssid.
///
/// An "OK" reply is NOT proof. The Snapmaker U1's wpa_supplicant answers OK to
/// SAVE_CONFIG and never writes the file — device-verified 2026-07-29: reply
/// "OK", config mtime unchanged, zero `network={` blocks, path provably
/// writable. Trusting the reply is why WiFi there dies on every power-off.
/// Only the config file's actual contents settle it.
///
/// @param save_reply       Raw SAVE_CONFIG reply ("OK\n", "FAIL\n", …).
/// @param config_contents  The wpa config file re-read AFTER the save.
/// @param ssid             The SSID that was supposed to be written.
SavePersistence classify_save_result(const std::string& save_reply,
                                     const std::string& config_contents, const std::string& ssid);
} // namespace helix::wifi::detail

/**
 * @brief wpa_supplicant backend using libhv async event loop
 *
 * Provides asynchronous communication with wpa_supplicant daemon via
 * Unix socket control interface. Uses libhv's EventLoopThread for
 * non-blocking socket I/O.
 *
 * Architecture:
 * - Inherits privately from hv::EventLoopThread for async I/O
 * - Dual wpa_ctrl connections: control (commands) + monitor (events)
 * - Event callbacks broadcast to registered handlers
 * - Commands sent synchronously via wpa_ctrl_request()
 *
 * Usage:
 * @code
 *   WifiBackendWpaSupplicant backend;
 *   backend.register_callback("scan", [](const std::string& event) {
 *       // Handle scan complete events
 *   });
 *   backend.start();  // Connects to wpa_supplicant, starts event loop
 *   std::string result = backend.send_command("SCAN");
 *   backend.stop();   // Clean shutdown
 * @endcode
 */
class WifiBackendWpaSupplicant : public WifiBackend, private hv::EventLoopThread {
  public:
    /**
     * @brief Construct WiFi backend
     *
     * Does NOT connect to wpa_supplicant. Call start() to initialize.
     */
    WifiBackendWpaSupplicant();

    /**
     * @brief Destructor - ensures clean shutdown
     */
    ~WifiBackendWpaSupplicant();

    // ========================================================================
    // WifiBackend Interface Implementation
    // ========================================================================

    /**
     * @brief Initialize and start wpa_supplicant backend
     *
     * Discovers wpa_supplicant socket, establishes dual connections
     * (control + monitor), and starts libhv event loop thread.
     *
     * @return true if initialization succeeded
     */
    WiFiError start() override;

    /**
     * @brief Non-blocking start — runs start() on a worker thread and
     * fires "READY" or "INIT_FAILED" events on completion.
     */
    void start_async() override;

    /**
     * @brief Stop wpa_supplicant backend
     *
     * Blocks until event loop thread terminates.
     */
    void stop() override;

    /**
     * @brief Check if backend is running
     *
     * @return true if event loop is active
     */
    bool is_running() const override;

    /**
     * @brief Register event callback
     *
     * Translates standard event names to wpa_supplicant-specific events:
     * - "SCAN_COMPLETE" → "CTRL-EVENT-SCAN-RESULTS"
     * - "CONNECTED" → "CTRL-EVENT-CONNECTED"
     * - "DISCONNECTED" → "CTRL-EVENT-DISCONNECTED"
     * - "AUTH_FAILED" → "CTRL-EVENT-SSID-TEMP-DISABLED"
     *
     * @param name Standard event name
     * @param callback Handler function
     */
    void register_event_callback(const std::string& name,
                                 std::function<void(const std::string&)> callback) override;

    /**
     * @brief Send synchronous command to wpa_supplicant
     *
     * Blocks until response received or timeout (usually <100ms).
     *
     * Common commands:
     * - "SCAN" - Trigger network scan
     * - "SCAN_RESULTS" - Get scan results (tab-separated format)
     * - "ADD_NETWORK" - Add network configuration (returns network ID)
     * - "SET_NETWORK <id> ssid \"<ssid>\"" - Set network SSID
     * - "SET_NETWORK <id> psk \"<password>\"" - Set WPA password
     * - "ENABLE_NETWORK <id>" - Connect to network
     * - "STATUS" - Get connection status
     *
     * @param cmd Command string (see wpa_supplicant control interface docs)
     * @return Response string (may contain newlines), or empty on error
     */
    std::string send_command(const std::string& cmd);

    // ========================================================================
    // Clean Abstraction API - Hides wpa_supplicant ugliness
    // ========================================================================

    WiFiError trigger_scan() override;
    WiFiError get_scan_results(std::vector<WiFiNetwork>& networks) override;
    WiFiError connect_network(const std::string& ssid, const std::string& password) override;
    WiFiError disconnect_network() override;
    ConnectionStatus get_status() override;
    bool supports_5ghz() const override;
    WiFiError set_radio_enabled(bool on) override;
    bool is_radio_enabled() const override;

  private:
    // ========================================================================
    // System Validation and Permission Checking
    // ========================================================================

    /**
     * @brief Check system prerequisites before starting backend
     *
     * Performs comprehensive validation:
     * - WiFi hardware detection
     * - wpa_supplicant socket availability
     * - Permission checking for socket access
     * - RF-kill status validation
     *
     * @return WiFiError with detailed status
     */
    WiFiError check_system_prerequisites();

    /**
     * @brief Check if user has permission to access wpa_supplicant sockets
     *
     * @param socket_path Path to test socket access
     * @return WiFiError indicating permission status
     */
    WiFiError check_socket_permissions(const std::string& socket_path);

    /**
     * @brief Detect WiFi hardware interfaces
     *
     * @return WiFiError with hardware status
     */
    WiFiError check_wifi_hardware();

    // ========================================================================
    // wpa_supplicant Communication
    // ========================================================================

    /**
     * @brief Initialize wpa_supplicant connection (runs in event loop thread)
     *
     * Called by start() in the context of the libhv event loop thread.
     * Discovers socket, opens connections, registers I/O callbacks.
     */
    void init_wpa();

    /**
     * @brief Cleanup wpa_supplicant connections
     *
     * Closes both control and monitor connections, detaches from events.
     * Called from destructor to prevent resource leaks.
     */
    void cleanup_wpa();

    /**
     * @brief Handle incoming wpa_supplicant events
     *
     * Broadcasts event to all registered callbacks.
     *
     * @param data Raw event data from wpa_supplicant
     * @param len Length of event data in bytes
     */
    void handle_wpa_events(void* data, int len);

    /**
     * @brief Static trampoline for C callback compatibility
     *
     * libhv uses C-style function pointers for I/O callbacks.
     * This static method extracts the instance pointer from hio_context()
     * and forwards to the member function handle_wpa_events().
     *
     * @param io libhv I/O handle
     * @param data Event data buffer
     * @param readbyte Number of bytes read
     */
    static void _handle_wpa_events(hio_t* io, void* data, int readbyte);

    /**
     * @brief Dispatch a synthetic event to a specific registered callback
     *
     * Used for internal events like INIT_FAILED that don't come from wpa_supplicant.
     *
     * @param event_name Name of the callback to dispatch to
     * @param message Message to pass to the callback
     */
    void dispatch_event(const std::string& event_name, const std::string& message);

    // Helper methods for clean API (encapsulate wpa_supplicant ugliness)
    std::vector<WiFiNetwork> parse_scan_results(const std::string& raw);
    std::vector<std::string> split_by_tabs(const std::string& str);
    int dbm_to_percentage(int dbm);
    std::string detect_security_type(const std::string& flags, bool& is_secured);

    /**
     * @brief Check if the libhv event loop thread is active
     *
     * Distinct from is_running() which checks if WiFi is logically enabled.
     * The thread may be active but WiFi disabled (after stop()).
     */
    bool event_loop_active() const {
        return const_cast<WifiBackendWpaSupplicant*>(this)->hv::EventLoopThread::isRunning();
    }

    /**
     * @brief Map raw wpa_supplicant event to callback name
     *
     * Parses the event string to determine which callback should handle it.
     * Returns empty string for informational events that don't need handling.
     *
     * @param event Raw wpa_supplicant event string
     * @return Callback name ("SCAN_COMPLETE", "CONNECTED", etc.) or empty string
     */
    std::string map_event_to_callback(const std::string& event);

    // 5GHz support — computed once during init, never changes
    std::atomic<bool> supports_5ghz_cached_{false};
    std::atomic<bool> supports_5ghz_resolved_{false};

    void resolve_5ghz_support();

    struct wpa_ctrl* conn;     ///< Control connection for sending commands
    struct wpa_ctrl* mon_conn; ///< Monitor connection for receiving events (FIXED LEAK)
    hio_t* mon_io_{nullptr};   ///< libhv I/O handle for monitor socket (must cleanup on re-init)

    // Thread safety
    std::mutex cmd_mutex_;       ///< Protects conn from concurrent send_command() calls
    std::mutex callbacks_mutex_; ///< Protects callbacks map from race conditions
    std::map<std::string, std::function<void(const std::string&)>>
        callbacks; ///< Registered event handlers

    // Change detection for status logging (reduces log noise)
    ConnectionStatus last_logged_status_; ///< Previous status for change detection

    // Init synchronization - ensures init_wpa() completes before start() returns
    std::mutex init_mutex_;
    std::condition_variable init_cv_;
    // init_complete_: an init *attempt* finished (success OR failure). Used only
    // to wake start()'s condition-variable wait — NOT a "backend is usable" flag.
    std::atomic<bool> init_complete_{false};
    // init_succeeded_: init_wpa() ran to completion with live control + monitor
    // connections. This is the real "backend is up" signal — is_running(),
    // is_enabled(), and the start()/start_async() retry guards key off it so a
    // failed init (e.g. a fresh-boot race where wpa_supplicant's control socket
    // isn't up yet) does not read as "running" and does not block a later retry.
    std::atomic<bool> init_succeeded_{false};

    // Resolved WiFi interface identity (netdev, ctrl socket, conf path, daemon
    // pid, rfkill node) — computed once in init_wpa() via
    // resolve_and_store_interface(). std::nullopt means resolution was
    // inconclusive; callers fall back to legacy first-match detection.
    mutable std::mutex iface_mutex_;
    std::optional<helix::wifi::WifiInterface> iface_;

    /// Resolve the managed interface and store it. Runs on the event loop
    /// thread during init_wpa(), after the control connection is live.
    void resolve_and_store_interface();

  public:
    /// The resolved interface, or nullopt when resolution was inconclusive and
    /// callers must fall back to legacy first-match behaviour.
    std::optional<helix::wifi::WifiInterface> resolved_interface() const;

  private:
    // Last state requested via set_radio_enabled(). Defaults to true (radio on).
    std::atomic<bool> radio_enabled_{true};

    /// Write "0"/"1" to the resolved rfkill node's `soft` attribute.
    /// @return false when there is no node or the write failed (not fatal —
    ///         DISCONNECT + DISABLE_NETWORK already stopped association).
    bool set_rfkill_soft_block(bool blocked);

    // Shutdown coordination - prevents use-after-free when start() times out
    // (GitHub issue #8: thread still in wpa_ctrl_attach when destructor runs)
    std::atomic<bool> shutdown_requested_{false};

    // Async init worker (used by start_async())
    std::thread async_init_thread_;
    std::atomic<bool> async_init_in_progress_{false};
};

#endif // __APPLE__
