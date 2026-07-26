// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/**
 * @file remote_control_server.h
 * @brief Unix domain socket server for remote control of HelixScreen
 *
 * Provides a JSON-RPC 2.0 interface over a Unix domain socket for controlling
 * a running HelixScreen instance. Commands are dispatched to the LVGL main
 * thread via ui_queue_update() + std::promise for thread-safe execution.
 *
 * Usage:
 *   RemoteControlServer::instance().start("/tmp/helixscreen-control.sock");
 *   // ... app runs ...
 *   RemoteControlServer::instance().stop();
 */

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "hv/json.hpp"
#include "remote_transport.h"

struct _lv_timer_t;

namespace helix {

/**
 * @brief Configuration for the remote-control server's wire transport.
 *
 * The default is a local Unix domain socket. HTTP/TCP is opt-in (LAN control +
 * the base for the post-1.0 web config UI) and binds loopback unless a bind
 * host is given.
 */
struct RemoteConfig {
    enum class Transport { UnixSocket, Http };

    Transport transport = Transport::UnixSocket;
    std::string socket_path;              // UnixSocket: resolved socket path.
    std::string http_bind = "127.0.0.1";  // Http: bind address.
    int http_port = 7130;                 // Http: TCP port.
};

/**
 * @brief JSON-RPC 2.0 remote control server over a pluggable transport
 *
 * Singleton that listens on a Unix socket for JSON-RPC requests.
 * All UI-affecting commands are dispatched to the LVGL main thread
 * via ui_queue_update() and block on a std::promise until complete.
 *
 * Thread model:
 * - Accept loop runs in a dedicated background thread
 * - Client handling is sequential (one at a time, sufficient for CLI model)
 * - Command handlers post work to UI thread and wait for result
 */
class RemoteControlServer {
  public:
    static RemoteControlServer& instance();

    // Non-copyable
    RemoteControlServer(const RemoteControlServer&) = delete;
    RemoteControlServer& operator=(const RemoteControlServer&) = delete;

    /**
     * @brief Start the server on the configured transport
     * @param config Transport selection and parameters
     * @return true on success, false on error
     */
    bool start(const RemoteConfig& config);

    /**
     * @brief Stop the server and release the transport
     */
    void stop();

    /**
     * @brief Check if server is running
     */
    bool is_running() const {
        return running_.load();
    }

    /**
     * @brief Human-readable endpoint of the active transport (empty if stopped)
     */
    std::string endpoint() const {
        return transport_ ? transport_->endpoint() : std::string();
    }

    using CommandHandler = std::function<nlohmann::json(const nlohmann::json& params)>;

    /**
     * @brief Register a custom command handler
     * @param method JSON-RPC method name
     * @param handler Function that takes params and returns result
     * @note Must be called before start() — not thread-safe with concurrent dispatch
     */
    void register_handler(const std::string& method, CommandHandler handler);

  private:
    RemoteControlServer() = default;
    ~RemoteControlServer();

    // Process a single JSON-RPC request and return the response string.
    // Passed to the transport as its RequestHandler.
    std::string process_request(const std::string& request_line);

    // Dispatch a JSON-RPC method call
    nlohmann::json dispatch(const std::string& method, const nlohmann::json& params,
                            const nlohmann::json& id);

    // Register built-in command handlers
    void register_builtin_handlers();

    // Phase 1 handlers
    nlohmann::json handle_ping(const nlohmann::json& params);
    nlohmann::json handle_navigate(const nlohmann::json& params);
    nlohmann::json handle_go_back(const nlohmann::json& params);
    nlohmann::json handle_list_panels(const nlohmann::json& params);
    nlohmann::json handle_list_components(const nlohmann::json& params);
    nlohmann::json handle_list_callbacks(const nlohmann::json& params);
    nlohmann::json handle_get_current(const nlohmann::json& params);
    nlohmann::json handle_screenshot(const nlohmann::json& params);
    nlohmann::json handle_status(const nlohmann::json& params);
    nlohmann::json handle_demo(const nlohmann::json& params);

    // Phase 2 handlers
    nlohmann::json handle_get_subject(const nlohmann::json& params);
    nlohmann::json handle_set_subject(const nlohmann::json& params);
    nlohmann::json handle_list_subjects(const nlohmann::json& params);
    nlohmann::json handle_wait_for(const nlohmann::json& params);
    nlohmann::json handle_wait_idle(const nlohmann::json& params);

    // Determinism toggle: stop/resume animations + periodic timers for
    // reproducible captures. See docs/devel/HELIXCTL.md "Diagnostics & lifecycle".
    nlohmann::json handle_freeze(const nlohmann::json& params);
    nlohmann::json handle_unfreeze(const nlohmann::json& params);

    // Phase 3 handlers
    nlohmann::json handle_click(const nlohmann::json& params);
    nlohmann::json handle_set_widget_value(const nlohmann::json& params);
    nlohmann::json handle_scenario(const nlohmann::json& params);
    nlohmann::json handle_list_scenarios(const nlohmann::json& params);

    // Introspection: enumerate named, interactable widgets on the live screen
    nlohmann::json handle_describe_screen(const nlohmann::json& params);

    // Scroll a named widget into view, or scroll a container by a delta
    nlohmann::json handle_scroll(const nlohmann::json& params);
    nlohmann::json handle_geom(const nlohmann::json& params);
    nlohmann::json handle_get_const(const nlohmann::json& params);

    // Reset the inactivity timer / dismiss the screensaver (like a real touch)
    nlohmann::json handle_wake(const nlohmann::json& params);

    // Ask the app to exit its main loop (app_request_quit)
    nlohmann::json handle_shutdown(const nlohmann::json& params);

    // Tail the in-memory log ring buffer
    nlohmann::json handle_log(const nlohmann::json& params);

    // Execute a function on the UI thread and wait for result
    nlohmann::json execute_on_ui_thread(std::function<nlohmann::json()> fn);

    // State
    std::atomic<bool> running_{false};
    std::unique_ptr<IRemoteTransport> transport_;

    // Command registry
    std::unordered_map<std::string, CommandHandler> handlers_;

    /// Timers `freeze` paused, so `unfreeze` resumes exactly that set and
    /// never resumes one that was already paused by its own owner.
    std::vector<_lv_timer_t*> paused_timers_;

    /// Guards against a second `freeze` re-scanning while already frozen,
    /// which would find every timer already paused, track none of them, and
    /// make `unfreeze` forget the original set — leaving it paused forever.
    bool frozen_ = false;

    /// The real animations_enabled value at the moment `freeze` was called,
    /// so `unfreeze` restores it exactly rather than assuming "on". Captured
    /// once per freeze/unfreeze cycle, not on an idempotent re-freeze.
    bool pre_freeze_animations_enabled_ = true;
};

/**
 * @brief Resolve the socket path using standard fallback logic
 *
 * Priority: override > $XDG_RUNTIME_DIR/helixscreen-control.sock > /tmp/helixscreen-control.sock
 *
 * @param override User-specified path (empty = use defaults)
 * @return Resolved socket path
 */
std::string resolve_socket_path(const std::string& override_path = "");

} // namespace helix
