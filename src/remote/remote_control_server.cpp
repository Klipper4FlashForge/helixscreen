// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "remote_control_server.h"

#include "panel_factory.h"
#include "ui_nav_manager.h"
#include "ui_update_queue.h"

#include "app_globals.h"
#include "mock_scenarios.h"
#include "printer_state.h"
#include "screenshot.h"
#include "subject_debug_registry.h"

// LVGL XML subject lookup
#include "helix-xml/src/xml/lv_xml.h"
#include "helix-xml/src/xml/lv_xml_component.h"
#include "helix-xml/src/xml/lv_xml_component_private.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cerrno>
#include <condition_variable>
#include <cstring>
#include <future>
#include <mutex>
#include <optional>

// POSIX socket headers
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

static constexpr size_t MAX_CLIENT_BUFFER = 65536; // 64KB max per request line

namespace helix {

// Panel name ↔ ID mappings, derived dynamically from the panel registry
// (PanelFactory::PANEL_NAMES) so new panels are picked up with no edits here.
// The registry stores widget names ("home_panel"); the CLI protocol uses short
// names ("home", "print-select") — strip a trailing "_panel" and map '_' -> '-'.
static std::string panel_short_name(int idx) {
    std::string n = PanelFactory::PANEL_NAMES[idx];
    static const std::string kSuffix = "_panel";
    if (n.size() > kSuffix.size() &&
        n.compare(n.size() - kSuffix.size(), kSuffix.size(), kSuffix) == 0) {
        n.erase(n.size() - kSuffix.size());
    }
    std::replace(n.begin(), n.end(), '_', '-');
    return n;
}

static std::string panel_id_to_name(helix::PanelId id) {
    int idx = static_cast<int>(id);
    if (idx < 0 || idx >= UI_PANEL_COUNT) {
        return "unknown";
    }
    return panel_short_name(idx);
}

static std::optional<helix::PanelId> name_to_panel_id(const std::string& name) {
    std::string norm = name;
    std::replace(norm.begin(), norm.end(), '_', '-');
    for (int i = 0; i < UI_PANEL_COUNT; ++i) {
        if (name == PanelFactory::PANEL_NAMES[i] || norm == panel_short_name(i)) {
            return static_cast<helix::PanelId>(i);
        }
    }
    return std::nullopt;
}

RemoteControlServer& RemoteControlServer::instance() {
    static RemoteControlServer instance;
    return instance;
}

RemoteControlServer::~RemoteControlServer() {
    stop();
}

std::string resolve_socket_path(const std::string& override_path) {
    if (!override_path.empty()) {
        return override_path;
    }

    const char* xdg_runtime = getenv("XDG_RUNTIME_DIR");
    if (xdg_runtime && xdg_runtime[0] != '\0') {
        return std::string(xdg_runtime) + "/helixscreen-control.sock";
    }

    return "/tmp/helixscreen-control.sock";
}

bool RemoteControlServer::start(const std::string& socket_path) {
    if (running_.load()) {
        spdlog::warn("[RemoteControl] Server already running");
        return false;
    }

    socket_path_ = socket_path;

    // Create self-pipe for shutdown signaling
    if (pipe(shutdown_pipe_) < 0) {
        spdlog::error("[RemoteControl] Failed to create shutdown pipe: {}", strerror(errno));
        return false;
    }

    // Remove stale socket file
    unlink(socket_path_.c_str());

    // Create Unix domain socket
    server_fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd_ < 0) {
        spdlog::error("[RemoteControl] Failed to create socket: {}", strerror(errno));
        close(shutdown_pipe_[0]);
        close(shutdown_pipe_[1]);
        shutdown_pipe_[0] = shutdown_pipe_[1] = -1;
        return false;
    }

    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;

    if (socket_path_.length() >= sizeof(addr.sun_path)) {
        spdlog::error("[RemoteControl] Socket path too long: {}", socket_path_);
        close(server_fd_);
        server_fd_ = -1;
        close(shutdown_pipe_[0]);
        close(shutdown_pipe_[1]);
        shutdown_pipe_[0] = shutdown_pipe_[1] = -1;
        return false;
    }
    strncpy(addr.sun_path, socket_path_.c_str(), sizeof(addr.sun_path) - 1);

    if (bind(server_fd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        spdlog::error("[RemoteControl] Failed to bind socket at {}: {}", socket_path_,
                      strerror(errno));
        close(server_fd_);
        server_fd_ = -1;
        close(shutdown_pipe_[0]);
        close(shutdown_pipe_[1]);
        shutdown_pipe_[0] = shutdown_pipe_[1] = -1;
        return false;
    }

    // Restrict socket access to owner only
    chmod(socket_path_.c_str(), 0600);

    if (listen(server_fd_, 1) < 0) {
        spdlog::error("[RemoteControl] Failed to listen on socket: {}", strerror(errno));
        close(server_fd_);
        server_fd_ = -1;
        unlink(socket_path_.c_str());
        close(shutdown_pipe_[0]);
        close(shutdown_pipe_[1]);
        shutdown_pipe_[0] = shutdown_pipe_[1] = -1;
        return false;
    }

    register_builtin_handlers();

    running_.store(true);
    accept_thread_ = std::thread(&RemoteControlServer::accept_loop, this);

    spdlog::info("[RemoteControl] Server started on {}", socket_path_);
    return true;
}

void RemoteControlServer::stop() {
    if (!running_.load()) {
        return;
    }

    spdlog::info("[RemoteControl] Stopping server...");
    running_.store(false);

    // Signal the accept loop to wake up
    if (shutdown_pipe_[1] >= 0) {
        char c = 'x';
        (void)write(shutdown_pipe_[1], &c, 1);
    }

    // Close server socket to unblock accept()
    if (server_fd_ >= 0) {
        close(server_fd_);
        server_fd_ = -1;
    }

    // Wait for accept thread
    if (accept_thread_.joinable()) {
        accept_thread_.join();
    }

    // Clean up shutdown pipe
    if (shutdown_pipe_[0] >= 0) {
        close(shutdown_pipe_[0]);
        shutdown_pipe_[0] = -1;
    }
    if (shutdown_pipe_[1] >= 0) {
        close(shutdown_pipe_[1]);
        shutdown_pipe_[1] = -1;
    }

    // Clean up socket file
    if (!socket_path_.empty()) {
        unlink(socket_path_.c_str());
        spdlog::debug("[RemoteControl] Removed socket file: {}", socket_path_);
    }

    handlers_.clear();
    spdlog::info("[RemoteControl] Server stopped");
}

void RemoteControlServer::register_handler(const std::string& method, CommandHandler handler) {
    handlers_[method] = std::move(handler);
}

void RemoteControlServer::accept_loop() {
    spdlog::debug("[RemoteControl] Accept loop started");

    while (running_.load()) {
        // Use poll() to wait for either a new connection or shutdown signal
        struct pollfd fds[2];
        fds[0].fd = server_fd_;
        fds[0].events = POLLIN;
        fds[1].fd = shutdown_pipe_[0];
        fds[1].events = POLLIN;

        int ret = poll(fds, 2, -1); // Block indefinitely
        if (ret < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (running_.load()) {
                spdlog::error("[RemoteControl] poll() failed: {}", strerror(errno));
            }
            break;
        }

        // Check shutdown pipe
        if (fds[1].revents & POLLIN) {
            break;
        }

        // Check for new connection
        if (fds[0].revents & POLLIN) {
            int client_fd = accept(server_fd_, nullptr, nullptr);
            if (client_fd < 0) {
                if (running_.load() && errno != EINVAL) {
                    spdlog::warn("[RemoteControl] accept() failed: {}", strerror(errno));
                }
                continue;
            }

            spdlog::debug("[RemoteControl] Client connected (fd={})", client_fd);
            handle_client(client_fd);
            close(client_fd);
            spdlog::debug("[RemoteControl] Client disconnected");
        }
    }

    spdlog::debug("[RemoteControl] Accept loop ended");
}

static bool write_all(int fd, const char* buf, size_t len) {
    while (len > 0) {
        ssize_t n = write(fd, buf, len);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return false;
        }
        buf += n;
        len -= static_cast<size_t>(n);
    }
    return true;
}

void RemoteControlServer::handle_client(int client_fd) {
    // Read newline-delimited JSON-RPC messages
    std::string buffer;
    char chunk[4096];

    // Set a read timeout so we don't hang forever on a bad client
    struct timeval tv;
    tv.tv_sec = 30;
    tv.tv_usec = 0;
    setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    while (running_.load()) {
        ssize_t n = read(client_fd, chunk, sizeof(chunk) - 1);
        if (n <= 0) {
            break; // Client disconnected or error
        }
        chunk[n] = '\0';
        buffer.append(chunk, static_cast<size_t>(n));

        // Guard against unbounded buffer growth from malicious/buggy clients
        if (buffer.size() > MAX_CLIENT_BUFFER) {
            spdlog::warn("[RemoteControl] Client buffer overflow (>64KB), disconnecting");
            return;
        }

        // Process complete lines
        size_t pos;
        while ((pos = buffer.find('\n')) != std::string::npos) {
            std::string line = buffer.substr(0, pos);
            buffer.erase(0, pos + 1);

            if (line.empty()) {
                continue;
            }

            std::string response = process_request(line);
            response += '\n';

            if (!write_all(client_fd, response.c_str(), response.length())) {
                spdlog::warn("[RemoteControl] Failed to write response: {}", strerror(errno));
                return;
            }
        }
    }
}

std::string RemoteControlServer::process_request(const std::string& request_line) {
    nlohmann::json response;

    try {
        auto request = nlohmann::json::parse(request_line);

        // Validate JSON-RPC 2.0
        if (!request.contains("jsonrpc") || request["jsonrpc"] != "2.0") {
            response = {
                {"jsonrpc", "2.0"},
                {"error", {{"code", -32600}, {"message", "Invalid Request: missing jsonrpc 2.0"}}},
                {"id", nullptr}};
            return response.dump();
        }

        if (!request.contains("method") || !request["method"].is_string()) {
            response = {
                {"jsonrpc", "2.0"},
                {"error", {{"code", -32600}, {"message", "Invalid Request: missing method"}}},
                {"id", request.value("id", nlohmann::json(nullptr))}};
            return response.dump();
        }

        std::string method = request["method"];
        nlohmann::json params = request.value("params", nlohmann::json::object());
        nlohmann::json id = request.value("id", nlohmann::json(nullptr));

        spdlog::debug("[RemoteControl] Request: method={}, id={}", method, id.dump());

        response = dispatch(method, params, id);

    } catch (const nlohmann::json::parse_error& e) {
        response = {
            {"jsonrpc", "2.0"},
            {"error", {{"code", -32700}, {"message", std::string("Parse error: ") + e.what()}}},
            {"id", nullptr}};
    } catch (const std::exception& e) {
        response = {
            {"jsonrpc", "2.0"},
            {"error", {{"code", -32603}, {"message", std::string("Internal error: ") + e.what()}}},
            {"id", nullptr}};
    }

    return response.dump();
}

nlohmann::json RemoteControlServer::dispatch(const std::string& method,
                                             const nlohmann::json& params,
                                             const nlohmann::json& id) {
    auto it = handlers_.find(method);
    if (it == handlers_.end()) {
        return {{"jsonrpc", "2.0"},
                {"error", {{"code", -32601}, {"message", "Method not found: " + method}}},
                {"id", id}};
    }

    try {
        nlohmann::json result = it->second(params);
        return {{"jsonrpc", "2.0"}, {"result", result}, {"id", id}};
    } catch (const std::exception& e) {
        return {
            {"jsonrpc", "2.0"},
            {"error", {{"code", -32603}, {"message", std::string("Handler error: ") + e.what()}}},
            {"id", id}};
    }
}

nlohmann::json RemoteControlServer::execute_on_ui_thread(std::function<nlohmann::json()> fn) {
    auto promise = std::make_shared<std::promise<nlohmann::json>>();
    auto future = promise->get_future();

    helix::ui::queue_update([promise, fn = std::move(fn)]() {
        try {
            promise->set_value(fn());
        } catch (const std::exception& e) {
            promise->set_exception(std::current_exception());
        }
    });

    // Wait with timeout
    auto status = future.wait_for(std::chrono::seconds(10));
    if (status == std::future_status::timeout) {
        throw std::runtime_error("UI thread timeout (10s)");
    }

    return future.get();
}

// =============================================================================
// Built-in handlers
// =============================================================================

void RemoteControlServer::register_builtin_handlers() {
    handlers_["ping"] = [this](const nlohmann::json& p) { return handle_ping(p); };
    handlers_["navigate"] = [this](const nlohmann::json& p) { return handle_navigate(p); };
    handlers_["go_back"] = [this](const nlohmann::json& p) { return handle_go_back(p); };
    handlers_["list_panels"] = [this](const nlohmann::json& p) { return handle_list_panels(p); };
    handlers_["get_current"] = [this](const nlohmann::json& p) { return handle_get_current(p); };
    handlers_["screenshot"] = [this](const nlohmann::json& p) { return handle_screenshot(p); };
    handlers_["status"] = [this](const nlohmann::json& p) { return handle_status(p); };

    // Phase 2: Subject get/set/list/wait_for
    handlers_["get"] = [this](const nlohmann::json& p) { return handle_get_subject(p); };
    handlers_["set"] = [this](const nlohmann::json& p) { return handle_set_subject(p); };
    handlers_["list_subjects"] = [this](const nlohmann::json& p) {
        return handle_list_subjects(p);
    };
    handlers_["wait_for"] = [this](const nlohmann::json& p) { return handle_wait_for(p); };

    // Phase 3: Widget interaction + scenarios
    handlers_["click"] = [this](const nlohmann::json& p) { return handle_click(p); };
    handlers_["set_widget_value"] = [this](const nlohmann::json& p) {
        return handle_set_widget_value(p);
    };
    handlers_["scenario"] = [this](const nlohmann::json& p) { return handle_scenario(p); };
    handlers_["list_scenarios"] = [this](const nlohmann::json& p) {
        return handle_list_scenarios(p);
    };

    // Introspection
    handlers_["describe_screen"] = [this](const nlohmann::json& p) {
        return handle_describe_screen(p);
    };
}

nlohmann::json RemoteControlServer::handle_ping(const nlohmann::json& /*params*/) {
    return "pong";
}

nlohmann::json RemoteControlServer::handle_navigate(const nlohmann::json& params) {
    if (!params.contains("panel") || !params["panel"].is_string()) {
        throw std::invalid_argument("Missing required parameter: panel");
    }

    std::string target = params["panel"];

    // 1. Base panel -> switch to it.
    auto panel_id = name_to_panel_id(target);
    if (panel_id) {
        return execute_on_ui_thread([panel_id]() -> nlohmann::json {
            NavigationManager::instance().set_active(*panel_id);
            return {{"navigated_to", panel_id_to_name(*panel_id)}, {"kind", "panel"}};
        });
    }

    // 2. Otherwise treat it as a named, clickable widget on the current screen
    //    and click it. This descends into whatever overlay/modal that widget's
    //    real handler opens, running the full production lifecycle
    //    (init_subjects/create/on_activate) -- never a raw lv_xml_create shell.
    //    It is the fs-metaphor "cd into something ls showed you".
    return execute_on_ui_thread([target]() -> nlohmann::json {
        lv_obj_t* widget = nullptr;
        if (lv_obj_t* screen = lv_screen_active()) {
            widget = lv_obj_find_by_name(screen, target.c_str());
        }
        if (!widget) {
            // Overlays/modals can live on the top layer -- look there too.
            widget = lv_obj_find_by_name(lv_layer_top(), target.c_str());
        }
        if (!widget) {
            throw std::invalid_argument("Not a panel, and no widget named '" + target +
                                        "' on screen");
        }
        if (!lv_obj_has_flag(widget, LV_OBJ_FLAG_CLICKABLE)) {
            throw std::invalid_argument("Widget '" + target + "' is not clickable");
        }
        lv_obj_send_event(widget, LV_EVENT_CLICKED, nullptr);
        return {{"navigated_to", target}, {"kind", "widget"}};
    });
}

nlohmann::json RemoteControlServer::handle_go_back(const nlohmann::json& /*params*/) {
    return execute_on_ui_thread([]() -> nlohmann::json {
        bool result = NavigationManager::instance().go_back();
        return {{"success", result}};
    });
}

nlohmann::json RemoteControlServer::handle_list_panels(const nlohmann::json& /*params*/) {
    nlohmann::json panels = nlohmann::json::array();
    for (int i = 0; i < UI_PANEL_COUNT; ++i) {
        panels.push_back(panel_short_name(i));
    }
    return {{"panels", panels}};
}

nlohmann::json RemoteControlServer::handle_get_current(const nlohmann::json& /*params*/) {
    return execute_on_ui_thread([]() -> nlohmann::json {
        auto& nav = NavigationManager::instance();
        std::string current_panel = panel_id_to_name(nav.get_active());
        return {{"panel", current_panel}};
    });
}

nlohmann::json RemoteControlServer::handle_screenshot(const nlohmann::json& /*params*/) {
    return execute_on_ui_thread([]() -> nlohmann::json {
        helix::save_screenshot();
        return {{"saved", true}};
    });
}

nlohmann::json RemoteControlServer::handle_status(const nlohmann::json& /*params*/) {
    return execute_on_ui_thread([]() -> nlohmann::json {
        auto& nav = NavigationManager::instance();
        std::string current_panel = panel_id_to_name(nav.get_active());

        auto& ps = get_printer_state();
        int conn_state = lv_subject_get_int(ps.get_printer_connection_state_subject());
        int klippy_state = lv_subject_get_int(ps.get_klippy_state_subject());

        return {{"panel", current_panel},
                {"connection_state", conn_state},
                {"klippy_state", klippy_state}};
    });
}

// =============================================================================
// Phase 2: Subject handlers
// =============================================================================

static const char* subject_type_name(lv_subject_type_t type) {
    switch (type) {
    case LV_SUBJECT_TYPE_INT:
        return "INT";
    case LV_SUBJECT_TYPE_STRING:
        return "STRING";
    case LV_SUBJECT_TYPE_FLOAT:
        return "FLOAT";
    case LV_SUBJECT_TYPE_POINTER:
        return "POINTER";
    case LV_SUBJECT_TYPE_COLOR:
        return "COLOR";
    case LV_SUBJECT_TYPE_GROUP:
        return "GROUP";
    default:
        return "UNKNOWN";
    }
}

// Look up a subject by name using the LVGL XML subject registry (which has all subjects).
// Must be called on the UI thread.
static lv_subject_t* find_subject_by_name(const std::string& name) {
    return lv_xml_get_subject(nullptr, name.c_str());
}

nlohmann::json RemoteControlServer::handle_get_subject(const nlohmann::json& params) {
    if (!params.contains("name") || !params["name"].is_string()) {
        throw std::invalid_argument("Missing required parameter: name");
    }

    std::string name = params["name"];

    return execute_on_ui_thread([name]() -> nlohmann::json {
        lv_subject_t* subject = find_subject_by_name(name);
        if (!subject) {
            throw std::invalid_argument("Unknown subject: " + name);
        }

        auto type = static_cast<lv_subject_type_t>(subject->type);
        if (type == LV_SUBJECT_TYPE_INT) {
            return {{"name", name},
                    {"type", subject_type_name(type)},
                    {"value", lv_subject_get_int(subject)}};
        } else if (type == LV_SUBJECT_TYPE_STRING) {
            const char* s = lv_subject_get_string(subject);
            return {{"name", name}, {"type", subject_type_name(type)}, {"value", s ? s : ""}};
        } else {
            return {
                {"name", name}, {"type", subject_type_name(type)}, {"value", "<unsupported type>"}};
        }
    });
}

nlohmann::json RemoteControlServer::handle_set_subject(const nlohmann::json& params) {
    if (!params.contains("name") || !params["name"].is_string()) {
        throw std::invalid_argument("Missing required parameter: name");
    }
    if (!params.contains("value")) {
        throw std::invalid_argument("Missing required parameter: value");
    }

    std::string name = params["name"];
    auto value_json = params["value"];

    return execute_on_ui_thread([name, value_json]() -> nlohmann::json {
        lv_subject_t* subject = find_subject_by_name(name);
        if (!subject) {
            throw std::invalid_argument("Unknown subject: " + name);
        }

        auto type = static_cast<lv_subject_type_t>(subject->type);

        if (type == LV_SUBJECT_TYPE_INT) {
            int value;
            if (value_json.is_number()) {
                value = value_json.get<int>();
            } else if (value_json.is_string()) {
                try {
                    value = std::stoi(value_json.get<std::string>());
                } catch (...) {
                    throw std::invalid_argument("Cannot convert value to int: " +
                                                value_json.dump());
                }
            } else {
                throw std::invalid_argument("Invalid value type for INT subject");
            }
            lv_subject_set_int(subject, value);
            return {{"name", name}, {"set", value}};

        } else if (type == LV_SUBJECT_TYPE_STRING) {
            std::string value = value_json.get<std::string>();
            lv_subject_copy_string(subject, value.c_str());
            return {{"name", name}, {"set", value}};

        } else {
            throw std::invalid_argument(std::string("Cannot set subject of type ") +
                                        subject_type_name(type));
        }
    });
}

nlohmann::json RemoteControlServer::handle_list_subjects(const nlohmann::json& /*params*/) {
    return execute_on_ui_thread([]() -> nlohmann::json {
        nlohmann::json subjects = nlohmann::json::array();

        // Get the global XML scope which holds all globally registered subjects
        auto* scope = lv_xml_component_get_scope("globals");
        if (scope) {
            lv_xml_subject_t* xml_sub =
                static_cast<lv_xml_subject_t*>(_lv_ll_get_head(&scope->subjects_ll));
            while (xml_sub) {
                if (xml_sub->name && xml_sub->subject) {
                    auto type = static_cast<lv_subject_type_t>(xml_sub->subject->type);
                    subjects.push_back(
                        {{"name", xml_sub->name}, {"type", subject_type_name(type)}});
                }
                xml_sub =
                    static_cast<lv_xml_subject_t*>(_lv_ll_get_next(&scope->subjects_ll, xml_sub));
            }
        }

        // Sort by name
        std::sort(subjects.begin(), subjects.end(),
                  [](const nlohmann::json& a, const nlohmann::json& b) {
                      return a["name"].get<std::string>() < b["name"].get<std::string>();
                  });

        return {{"subjects", subjects}, {"count", subjects.size()}};
    });
}

nlohmann::json RemoteControlServer::handle_wait_for(const nlohmann::json& params) {
    if (!params.contains("name") || !params["name"].is_string()) {
        throw std::invalid_argument("Missing required parameter: name");
    }
    if (!params.contains("value")) {
        throw std::invalid_argument("Missing required parameter: value");
    }

    std::string name = params["name"];
    int timeout_secs = params.value("timeout", 30);
    std::string target_str;
    if (params["value"].is_string()) {
        target_str = params["value"].get<std::string>();
    } else {
        target_str = params["value"].dump();
    }

    // Shared state between observer callback (UI thread) and waiting thread
    struct WaitState {
        std::mutex mtx;
        std::condition_variable cv;
        bool matched = false;
        bool cancelled = false;
        lv_observer_t* observer = nullptr;
        // Target values (set once during setup, read-only after)
        int target_int = 0;
        std::string target_str;
        bool is_int = false;
        bool parse_as_int = false;
    };
    auto state = std::make_shared<WaitState>();

    // Parse target int value
    state->target_str = target_str;
    try {
        state->target_int = std::stoi(target_str);
        state->parse_as_int = true;
    } catch (...) {
        state->parse_as_int = false;
    }

    // Post observer creation to UI thread — resolves subject and checks value there
    auto setup_promise = std::make_shared<std::promise<bool>>();
    auto setup_future = setup_promise->get_future();

    // Prevent WaitState from being destroyed while observer is alive by capturing
    // shared_ptr in the observer's setup closure (ensures ref count stays > 0)
    helix::ui::queue_update([name, state, setup_promise]() {
        lv_subject_t* subject = find_subject_by_name(name);
        if (!subject) {
            setup_promise->set_value(false);
            return;
        }

        auto type = static_cast<lv_subject_type_t>(subject->type);
        state->is_int = (type == LV_SUBJECT_TYPE_INT);

        if (!state->is_int && type != LV_SUBJECT_TYPE_STRING) {
            setup_promise->set_value(false);
            return;
        }

        // Check current value immediately (on UI thread — safe)
        bool already_matched = false;
        if (state->is_int && state->parse_as_int) {
            already_matched = (lv_subject_get_int(subject) == state->target_int);
        } else if (!state->is_int) {
            already_matched = (std::string(lv_subject_get_string(subject)) == state->target_str);
        }

        if (already_matched) {
            std::lock_guard<std::mutex> lock(state->mtx);
            state->matched = true;
            state->cv.notify_one();
            setup_promise->set_value(true);
            return;
        }

        // Create observer — callback runs on UI thread so reads are safe.
        // Capture shared_ptr to keep WaitState alive as long as observer exists.
        auto captured_state = state;
        state->observer = lv_subject_add_observer(
            subject,
            [](lv_observer_t* obs, lv_subject_t* sub) {
                auto* ws = static_cast<WaitState*>(lv_observer_get_user_data(obs));
                if (!ws || ws->cancelled)
                    return;

                // Read value on UI thread (safe) and check match
                bool match = false;
                if (ws->is_int && ws->parse_as_int) {
                    match = (lv_subject_get_int(sub) == ws->target_int);
                } else if (!ws->is_int) {
                    match = (std::string(lv_subject_get_string(sub)) == ws->target_str);
                }

                std::lock_guard<std::mutex> lock(ws->mtx);
                if (match) {
                    ws->matched = true;
                }
                ws->cv.notify_one();
            },
            state.get());

        setup_promise->set_value(true);
    });

    // Wait for setup
    auto setup_status = setup_future.wait_for(std::chrono::seconds(5));
    if (setup_status == std::future_status::timeout) {
        throw std::runtime_error("Timeout setting up observer");
    }

    if (!setup_future.get()) {
        throw std::invalid_argument("Unknown or unsupported subject: " + name);
    }

    if (!state->matched) {
        // Wait on the condition variable with timeout
        // The observer callback sets matched=true on the UI thread when value matches
        std::unique_lock<std::mutex> lock(state->mtx);
        state->cv.wait_for(lock, std::chrono::seconds(timeout_secs),
                           [&state]() { return state->matched; });
    }

    // Signal cancellation so observer callback becomes a no-op
    {
        std::lock_guard<std::mutex> lock(state->mtx);
        state->cancelled = true;
    }

    // Remove observer on UI thread
    if (state->observer) {
        auto cleanup_promise = std::make_shared<std::promise<void>>();
        auto cleanup_future = cleanup_promise->get_future();

        helix::ui::queue_update([state, cleanup_promise]() {
            if (state->observer) {
                lv_observer_remove(state->observer);
                state->observer = nullptr;
            }
            cleanup_promise->set_value();
        });

        // Block until cleanup completes — don't let state die with observer still attached
        cleanup_future.wait_for(std::chrono::seconds(10));
    }

    if (state->matched) {
        return {{"matched", true}, {"name", name}, {"value", target_str}};
    } else {
        throw std::runtime_error("Timeout waiting for " + name + " == " + target_str + " (waited " +
                                 std::to_string(timeout_secs) + "s)");
    }
}

// =============================================================================
// Phase 3: Widget interaction + scenario handlers
// =============================================================================

nlohmann::json RemoteControlServer::handle_click(const nlohmann::json& params) {
    if (!params.contains("name") || !params["name"].is_string()) {
        throw std::invalid_argument("Missing required parameter: name");
    }

    std::string widget_name = params["name"];

    return execute_on_ui_thread([widget_name]() -> nlohmann::json {
        lv_obj_t* screen = lv_screen_active();
        if (!screen) {
            throw std::runtime_error("No active screen");
        }

        lv_obj_t* widget = lv_obj_find_by_name(screen, widget_name.c_str());
        if (!widget) {
            throw std::invalid_argument("Widget not found: " + widget_name);
        }

        lv_obj_send_event(widget, LV_EVENT_CLICKED, nullptr);
        return {{"clicked", widget_name}};
    });
}

nlohmann::json RemoteControlServer::handle_set_widget_value(const nlohmann::json& params) {
    if (!params.contains("name") || !params["name"].is_string()) {
        throw std::invalid_argument("Missing required parameter: name");
    }
    if (!params.contains("value")) {
        throw std::invalid_argument("Missing required parameter: value");
    }

    std::string widget_name = params["name"];
    auto value_json = params["value"];

    return execute_on_ui_thread([widget_name, value_json]() -> nlohmann::json {
        lv_obj_t* screen = lv_screen_active();
        if (!screen) {
            throw std::runtime_error("No active screen");
        }

        lv_obj_t* widget = lv_obj_find_by_name(screen, widget_name.c_str());
        if (!widget) {
            throw std::invalid_argument("Widget not found: " + widget_name);
        }

        // Try to set value based on widget type
        if (value_json.is_number()) {
            int32_t val = value_json.get<int32_t>();
            // Works for sliders, bars, arcs, switches, spinboxes
            if (lv_obj_check_type(widget, &lv_slider_class)) {
                lv_slider_set_value(widget, val, LV_ANIM_OFF);
            } else if (lv_obj_check_type(widget, &lv_bar_class)) {
                lv_bar_set_value(widget, val, LV_ANIM_OFF);
            } else if (lv_obj_check_type(widget, &lv_arc_class)) {
                lv_arc_set_value(widget, val);
            } else if (lv_obj_check_type(widget, &lv_switch_class)) {
                if (val) {
                    lv_obj_add_state(widget, LV_STATE_CHECKED);
                } else {
                    lv_obj_remove_state(widget, LV_STATE_CHECKED);
                }
            } else {
                throw std::invalid_argument("Widget type does not support numeric value");
            }
            return {{"widget", widget_name}, {"set", val}};

        } else if (value_json.is_string()) {
            std::string val = value_json.get<std::string>();
            if (lv_obj_check_type(widget, &lv_textarea_class)) {
                lv_textarea_set_text(widget, val.c_str());
            } else if (lv_obj_check_type(widget, &lv_label_class)) {
                lv_label_set_text(widget, val.c_str());
            } else {
                throw std::invalid_argument("Widget type does not support string value");
            }
            return {{"widget", widget_name}, {"set", val}};

        } else {
            throw std::invalid_argument("Value must be a number or string");
        }
    });
}

// Classify a widget into a short type string for introspection output.
static const char* describe_widget_type(lv_obj_t* o) {
    if (lv_obj_check_type(o, &lv_switch_class))
        return "switch";
    if (lv_obj_check_type(o, &lv_checkbox_class))
        return "checkbox";
    if (lv_obj_check_type(o, &lv_slider_class))
        return "slider";
    if (lv_obj_check_type(o, &lv_arc_class))
        return "arc";
    if (lv_obj_check_type(o, &lv_bar_class))
        return "bar";
    if (lv_obj_check_type(o, &lv_dropdown_class))
        return "dropdown";
    if (lv_obj_check_type(o, &lv_textarea_class))
        return "textarea";
    if (lv_obj_check_type(o, &lv_button_class))
        return "button";
    if (lv_obj_check_type(o, &lv_label_class))
        return "label";
    if (lv_obj_check_type(o, &lv_image_class))
        return "image";
    return "obj";
}

// Build the descriptor for one named widget: type, what you can do to it, and
// its current value where that's meaningful.
static nlohmann::json describe_one(lv_obj_t* o, const char* name) {
    nlohmann::json entry;
    entry["name"] = name;
    entry["type"] = describe_widget_type(o);

    nlohmann::json actions = nlohmann::json::array();
    if (lv_obj_has_flag(o, LV_OBJ_FLAG_CLICKABLE)) {
        actions.push_back("click");
    }
    if (lv_obj_check_type(o, &lv_textarea_class)) {
        actions.push_back("fill");
        const char* txt = lv_textarea_get_text(o);
        entry["value"] = txt ? txt : "";
    } else if (lv_obj_check_type(o, &lv_switch_class) ||
               lv_obj_check_type(o, &lv_checkbox_class)) {
        actions.push_back("toggle");
        entry["value"] = lv_obj_has_state(o, LV_STATE_CHECKED) ? 1 : 0;
    } else if (lv_obj_check_type(o, &lv_slider_class)) {
        actions.push_back("set");
        entry["value"] = lv_slider_get_value(o);
    } else if (lv_obj_check_type(o, &lv_arc_class)) {
        actions.push_back("set");
        entry["value"] = lv_arc_get_value(o);
    } else if (lv_obj_check_type(o, &lv_bar_class)) {
        entry["value"] = lv_bar_get_value(o);
    } else if (lv_obj_check_type(o, &lv_dropdown_class)) {
        actions.push_back("set");
        entry["value"] = static_cast<int>(lv_dropdown_get_selected(o));
    }
    entry["actions"] = actions;
    return entry;
}

// Recursively collect named, non-hidden widgets under `parent` into `out`.
// Only named widgets are emitted (unnamed ones can't be addressed), but the
// walk still descends through them to reach named descendants.
static void describe_walk(lv_obj_t* parent, nlohmann::json& out) {
    if (!parent) {
        return;
    }
    uint32_t count = lv_obj_get_child_count(parent);
    for (uint32_t i = 0; i < count; ++i) {
        lv_obj_t* child = lv_obj_get_child(parent, i);
        if (!child) {
            continue;
        }
        if (lv_obj_has_flag(child, LV_OBJ_FLAG_HIDDEN)) {
            continue; // hidden subtree — not on screen
        }
        const char* raw = lv_obj_get_name(child);
        if (raw && raw[0] != '\0') {
            // Resolve "foo_#" index placeholders to the concrete "foo_2" name
            // so the reported name is actually addressable via find_by_name.
            char resolved[128];
            lv_obj_get_name_resolved(child, resolved, sizeof(resolved));
            out.push_back(describe_one(child, resolved[0] != '\0' ? resolved : raw));
        }
        describe_walk(child, out);
    }
}

nlohmann::json RemoteControlServer::handle_describe_screen(const nlohmann::json& /*params*/) {
    return execute_on_ui_thread([]() -> nlohmann::json {
        nlohmann::json widgets = nlohmann::json::array();
        // Active screen holds panels + pushed overlays; the top layer holds
        // modals and their backdrops. Walk both so nothing on screen is missed.
        describe_walk(lv_screen_active(), widgets);
        describe_walk(lv_layer_top(), widgets);
        return {{"widgets", widgets}};
    });
}

nlohmann::json RemoteControlServer::handle_scenario(const nlohmann::json& params) {
    if (!params.contains("name") || !params["name"].is_string()) {
        throw std::invalid_argument("Missing required parameter: name");
    }

    std::string scenario_name = params["name"];
    const auto* scenario = find_scenario(scenario_name);
    if (!scenario) {
        throw std::invalid_argument("Unknown scenario: " + scenario_name);
    }

    auto apply_fn = scenario->apply;
    return execute_on_ui_thread([apply_fn, scenario_name]() -> nlohmann::json {
        apply_fn();
        return {{"applied", scenario_name}};
    });
}

nlohmann::json RemoteControlServer::handle_list_scenarios(const nlohmann::json& /*params*/) {
    const auto& scenarios = get_mock_scenarios();

    nlohmann::json result = nlohmann::json::array();
    for (const auto& s : scenarios) {
        result.push_back({{"name", s.name}, {"description", s.description}});
    }

    return {{"scenarios", result}, {"count", scenarios.size()}};
}

} // namespace helix
