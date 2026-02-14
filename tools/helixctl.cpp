// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file helixctl.cpp
 * @brief CLI tool for remote control of a running HelixScreen instance
 *
 * Sends JSON-RPC 2.0 commands over a Unix domain socket to HelixScreen's
 * RemoteControlServer. Standalone binary — no LVGL or libhv dependencies.
 *
 * Usage:
 *   helixctl ping
 *   helixctl navigate controls
 *   helixctl screenshot
 *   helixctl status
 */

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "hv/json.hpp"

// POSIX socket headers
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

static const char* PROGRAM_NAME = "helixctl";

static void print_usage() {
    printf("Usage: %s [options] <command> [args...]\n", PROGRAM_NAME);
    printf("\nNavigation:\n");
    printf("  ping                    Health check\n");
    printf("  navigate <panel>        Switch to panel/overlay\n");
    printf("  go_back                 Pop current overlay\n");
    printf("  list_panels             List available panels\n");
    printf("  current                 Show current panel and overlay stack\n");
    printf("  screenshot              Take a screenshot\n");
    printf("  status                  Show panel, connection state, printer status\n");
    printf("\nSubjects:\n");
    printf("  get <subject>           Read current value of named subject\n");
    printf("  set <subject> <value>   Set subject value\n");
    printf("  list_subjects           List all registered subjects\n");
    printf("  wait_for <subject> <value> [--timeout N]\n");
    printf("                          Block until subject matches value (default 30s)\n");
    printf("\nWidgets:\n");
    printf("  click <widget>          Send click event to named widget\n");
    printf("  set_value <widget> <v>  Set widget value (slider, switch, textarea)\n");
    printf("\nScenarios:\n");
    printf("  scenario <name>         Apply named mock scenario\n");
    printf("  list_scenarios          List available mock scenarios\n");
    printf("\nOptions:\n");
    printf("  -s, --socket <path>     Socket path (default: auto-detect)\n");
    printf("  -h, --help              Show this help\n");
    printf("\nSocket path resolution:\n");
    printf("  1. --socket <path>  (explicit)\n");
    printf("  2. $XDG_RUNTIME_DIR/helixscreen-control.sock\n");
    printf("  3. /tmp/helixscreen-control.sock\n");
}

static std::string resolve_socket_path(const std::string& override_path) {
    if (!override_path.empty()) {
        return override_path;
    }

    const char* xdg_runtime = getenv("XDG_RUNTIME_DIR");
    if (xdg_runtime && xdg_runtime[0] != '\0') {
        std::string path = std::string(xdg_runtime) + "/helixscreen-control.sock";
        // Check if the socket exists
        if (access(path.c_str(), F_OK) == 0) {
            return path;
        }
    }

    return "/tmp/helixscreen-control.sock";
}

static int connect_to_server(const std::string& socket_path) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        fprintf(stderr, "Error: Failed to create socket: %s\n", strerror(errno));
        return -1;
    }

    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;

    if (socket_path.length() >= sizeof(addr.sun_path)) {
        fprintf(stderr, "Error: Socket path too long\n");
        close(fd);
        return -1;
    }
    strncpy(addr.sun_path, socket_path.c_str(), sizeof(addr.sun_path) - 1);

    if (connect(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        if (errno == ENOENT || errno == ECONNREFUSED) {
            fprintf(stderr, "Error: No HelixScreen instance found at %s\n", socket_path.c_str());
            fprintf(stderr, "Is HelixScreen running with --remote or --test?\n");
        } else {
            fprintf(stderr, "Error: Failed to connect: %s\n", strerror(errno));
        }
        close(fd);
        return -1;
    }

    return fd;
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

static bool send_request(int fd, const nlohmann::json& request) {
    std::string data = request.dump() + "\n";
    if (!write_all(fd, data.c_str(), data.length())) {
        fprintf(stderr, "Error: Failed to send request: %s\n", strerror(errno));
        return false;
    }
    return true;
}

static std::string read_response(int fd) {
    std::string buffer;
    char chunk[4096];

    // Set a read timeout
    struct timeval tv;
    tv.tv_sec = 30;
    tv.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    while (true) {
        ssize_t n = read(fd, chunk, sizeof(chunk) - 1);
        if (n <= 0) {
            break;
        }
        chunk[n] = '\0';
        buffer.append(chunk, static_cast<size_t>(n));

        // Check for newline (end of response)
        if (buffer.find('\n') != std::string::npos) {
            break;
        }
    }

    // Trim trailing newline
    while (!buffer.empty() && buffer.back() == '\n') {
        buffer.pop_back();
    }

    return buffer;
}

static nlohmann::json build_request(const std::string& method,
                                    const nlohmann::json& params = nlohmann::json::object()) {
    return {{"jsonrpc", "2.0"}, {"method", method}, {"params", params}, {"id", 1}};
}

static int handle_response(const std::string& raw_response) {
    if (raw_response.empty()) {
        fprintf(stderr, "Error: Empty response from server\n");
        return 1;
    }

    try {
        auto response = nlohmann::json::parse(raw_response);

        if (response.contains("error")) {
            auto& error = response["error"];
            fprintf(stderr, "Error: %s (code %d)\n",
                    error.value("message", "Unknown error").c_str(), error.value("code", -1));
            return 1;
        }

        if (response.contains("result")) {
            auto& result = response["result"];
            if (result.is_string()) {
                printf("%s\n", result.get<std::string>().c_str());
            } else {
                printf("%s\n", result.dump(2).c_str());
            }
            return 0;
        }

        fprintf(stderr, "Error: Unexpected response format\n");
        return 1;

    } catch (const nlohmann::json::parse_error& e) {
        fprintf(stderr, "Error: Failed to parse response: %s\n", e.what());
        return 1;
    }
}

int main(int argc, char** argv) {
    std::string socket_path;
    int arg_start = 1;

    // Parse options
    while (arg_start < argc) {
        if (strcmp(argv[arg_start], "-h") == 0 || strcmp(argv[arg_start], "--help") == 0) {
            print_usage();
            return 0;
        }
        if (strcmp(argv[arg_start], "-s") == 0 || strcmp(argv[arg_start], "--socket") == 0) {
            if (arg_start + 1 >= argc) {
                fprintf(stderr, "Error: --socket requires a path argument\n");
                return 1;
            }
            socket_path = argv[++arg_start];
            arg_start++;
        } else {
            break; // Start of command
        }
    }

    if (arg_start >= argc) {
        print_usage();
        return 1;
    }

    std::string command = argv[arg_start];

    // Build JSON-RPC request
    nlohmann::json request;

    if (command == "ping") {
        request = build_request("ping");
    } else if (command == "navigate") {
        if (arg_start + 1 >= argc) {
            fprintf(stderr, "Error: navigate requires a panel name\n");
            return 1;
        }
        request = build_request("navigate", {{"panel", argv[arg_start + 1]}});
    } else if (command == "go_back") {
        request = build_request("go_back");
    } else if (command == "list_panels") {
        request = build_request("list_panels");
    } else if (command == "current") {
        request = build_request("get_current");
    } else if (command == "screenshot") {
        request = build_request("screenshot");
    } else if (command == "status") {
        request = build_request("status");
    } else if (command == "get") {
        if (arg_start + 1 >= argc) {
            fprintf(stderr, "Error: get requires a subject name\n");
            return 1;
        }
        request = build_request("get", {{"name", argv[arg_start + 1]}});
    } else if (command == "set") {
        if (arg_start + 2 >= argc) {
            fprintf(stderr, "Error: set requires a subject name and value\n");
            return 1;
        }
        // Try to parse value as integer, fall back to string
        std::string val_str = argv[arg_start + 2];
        nlohmann::json val;
        try {
            val = std::stoi(val_str);
        } catch (...) {
            val = val_str;
        }
        request = build_request("set", {{"name", argv[arg_start + 1]}, {"value", val}});
    } else if (command == "list_subjects") {
        request = build_request("list_subjects");
    } else if (command == "wait_for") {
        if (arg_start + 2 >= argc) {
            fprintf(stderr, "Error: wait_for requires a subject name and value\n");
            return 1;
        }
        nlohmann::json wait_params = {{"name", argv[arg_start + 1]},
                                      {"value", argv[arg_start + 2]}};

        // Check for --timeout
        for (int i = arg_start + 3; i < argc; i++) {
            if ((strcmp(argv[i], "--timeout") == 0 || strcmp(argv[i], "-t") == 0) && i + 1 < argc) {
                wait_params["timeout"] = std::atoi(argv[i + 1]);
                i++;
            }
        }
        request = build_request("wait_for", wait_params);
    } else if (command == "click") {
        if (arg_start + 1 >= argc) {
            fprintf(stderr, "Error: click requires a widget name\n");
            return 1;
        }
        request = build_request("click", {{"name", argv[arg_start + 1]}});
    } else if (command == "set_value") {
        if (arg_start + 2 >= argc) {
            fprintf(stderr, "Error: set_value requires a widget name and value\n");
            return 1;
        }
        // Try to parse as number, fall back to string
        std::string val_str = argv[arg_start + 2];
        nlohmann::json val;
        try {
            val = std::stoi(val_str);
        } catch (...) {
            val = val_str;
        }
        request =
            build_request("set_widget_value", {{"name", argv[arg_start + 1]}, {"value", val}});
    } else if (command == "scenario") {
        if (arg_start + 1 >= argc) {
            fprintf(stderr, "Error: scenario requires a name\n");
            return 1;
        }
        request = build_request("scenario", {{"name", argv[arg_start + 1]}});
    } else if (command == "list_scenarios") {
        request = build_request("list_scenarios");
    } else {
        fprintf(stderr, "Unknown command: %s\n", command.c_str());
        print_usage();
        return 1;
    }

    // Connect, send, receive
    std::string resolved_path = resolve_socket_path(socket_path);
    int fd = connect_to_server(resolved_path);
    if (fd < 0) {
        return 1;
    }

    if (!send_request(fd, request)) {
        close(fd);
        return 1;
    }

    std::string response = read_response(fd);
    close(fd);

    return handle_response(response);
}
