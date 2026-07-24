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
#include <string>
#include <thread>
#include <unordered_map>

#include "hv/json.hpp"

namespace helix {

/**
 * @brief JSON-RPC 2.0 remote control server over Unix domain socket
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
     * @brief Start the server
     * @param socket_path Path for the Unix domain socket
     * @return true on success, false on error
     */
    bool start(const std::string& socket_path);

    /**
     * @brief Stop the server and clean up the socket
     */
    void stop();

    /**
     * @brief Check if server is running
     */
    bool is_running() const {
        return running_.load();
    }

    /**
     * @brief Get the socket path
     */
    const std::string& socket_path() const {
        return socket_path_;
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

    // Accept loop (runs in background thread)
    void accept_loop();

    // Handle a single connected client
    void handle_client(int client_fd);

    // Process a single JSON-RPC request and return response
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
    nlohmann::json handle_get_current(const nlohmann::json& params);
    nlohmann::json handle_screenshot(const nlohmann::json& params);
    nlohmann::json handle_status(const nlohmann::json& params);

    // Phase 2 handlers
    nlohmann::json handle_get_subject(const nlohmann::json& params);
    nlohmann::json handle_set_subject(const nlohmann::json& params);
    nlohmann::json handle_list_subjects(const nlohmann::json& params);
    nlohmann::json handle_wait_for(const nlohmann::json& params);

    // Phase 3 handlers
    nlohmann::json handle_click(const nlohmann::json& params);
    nlohmann::json handle_set_widget_value(const nlohmann::json& params);
    nlohmann::json handle_scenario(const nlohmann::json& params);
    nlohmann::json handle_list_scenarios(const nlohmann::json& params);

    // Introspection: enumerate named, interactable widgets on the live screen
    nlohmann::json handle_describe_screen(const nlohmann::json& params);

    // Scroll a named widget into view, or scroll a container by a delta
    nlohmann::json handle_scroll(const nlohmann::json& params);

    // Reset the inactivity timer / dismiss the screensaver (like a real touch)
    nlohmann::json handle_wake(const nlohmann::json& params);

    // Execute a function on the UI thread and wait for result
    nlohmann::json execute_on_ui_thread(std::function<nlohmann::json()> fn);

    // State
    std::atomic<bool> running_{false};
    int server_fd_ = -1;
    std::string socket_path_;
    std::thread accept_thread_;

    // Self-pipe for waking up accept() during shutdown
    int shutdown_pipe_[2] = {-1, -1};

    // Command registry
    std::unordered_map<std::string, CommandHandler> handlers_;
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
