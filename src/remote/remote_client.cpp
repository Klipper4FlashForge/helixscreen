// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file remote_client.cpp
 * @brief The helixctl client, folded into helix-screen as a subcommand.
 *
 * Sends JSON-RPC 2.0 commands over a Unix domain socket to HelixScreen's
 * RemoteControlServer. Reached via `helix-screen ctl <cmd>` / `helix-screen
 * repl` (main.cpp dispatches to remote_client_main before the app starts).
 * No separate binary. Dev/test-only — compiled out of release builds.
 *
 * Usage:
 *   helix-screen ctl ping
 *   helix-screen ctl navigate controls
 *   helix-screen ctl screenshot
 *   helix-screen ctl status
 *   helix-screen repl                  # Interactive REPL with line editing
 */

#include "remote_client.h"

#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "hv/json.hpp"

// POSIX headers
#include "unix_socket_transport.h"

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

// Line editing for REPL
#include "linenoise.h"

static const char* PROGRAM_NAME = "helix-screen ctl";

// ---------------------------------------------------------------------------
// Help text
// ---------------------------------------------------------------------------

static void print_usage() {
    printf("Usage: %s [options] <command> [args...]\n", PROGRAM_NAME);
    printf("\nNavigation (fs metaphor):\n");
    printf("  help, ?                 Show this help\n");
    printf("  ping                    Health check\n");
    printf("  navigate, cd <target>   Go to a panel, or click a widget to descend\n");
    printf("  go_back, back, cd ..     Pop the current overlay/level\n");
    printf("  current, pwd            Show current panel and overlay stack\n");
    printf("  list_panels             List available panels\n");
    printf("  list_components         List every registered XML component (live registry)\n");
    printf("  list_callbacks          List every registered event-callback name\n");
    printf("  screenshot [path]       Capture the screen (a .png path encodes PNG;\n");
    printf("                          default: timestamped .bmp in the runtime dir)\n");
    printf("  status                  Show panel, connection state, printer status\n");
    printf("  wake                    Reset idle timer / dismiss the screensaver\n");
    printf("  demo <name>             Show a sample-data overlay unreachable in mock mode\n");
    printf("                          (preflight-check, runout-modal, lock-screen,\n");
    printf("                           print-status, print-tune, ams, camera)\n");
    printf("\nSubjects:\n");
    printf("  get <subject>           Read current value of named subject\n");
    printf("  set <subject> <value>   Set subject value\n");
    printf("  list_subjects           List all registered subjects\n");
    printf("  wait_for <subject> <value> [--timeout N]\n");
    printf("                          Block until subject matches value (default 30s)\n");
    printf("\nWidgets (targets: a name, a 'glob*' pattern, or @path from `ls`):\n");
    printf("  ls, describe_screen [target]\n");
    printf("                          List on-screen widgets: name, path, type, actions.\n");
    printf("                          With a target, list only that widget's subtree;\n");
    printf("                          with a pattern ('row_*'), every match. Quote it.\n");
    printf("  click <target>          Click (toggles switches/checkboxes). On a composite\n");
    printf("                          row, descends to the control inside it.\n");
    printf("  set_value <target> <v>  Set value (slider, switch, dropdown, textarea)\n");
    printf("  scroll <target> [dx dy] Scroll into view, or by a delta\n");
    printf("  focus <target>          Focus a widget through its input group. Raises the\n");
    printf("                          on-screen keyboard for a textarea (click does not).\n");
    printf("\nSynthetic pointer (drives LVGL's real input pipeline — gestures, long-press,\n");
    printf("scroll-vs-tap — unlike `click`, which sends a bare widget event):\n");
    printf("  press <x> <y>           Put the pointer down at x,y\n");
    printf("  move <x> <y>            Move it (a drag while pressed, a hover while not)\n");
    printf("  release [x y]           Lift it, at x,y if given, else where it is\n");
    printf("                          e.g. long-press: press 100 300; sleep 0.6; release\n");
    printf("\nDiagnostics & lifecycle:\n");
    printf("  wait_idle [--timeout N] Block until UpdateQueue and HttpExecutor are both\n");
    printf("                          quiet (default 10s). Best-effort — see\n");
    printf("                          docs/devel/HELIXCTL.md for what it cannot see\n");
    printf("  freeze                  Stop animations + pause periodic timers for a\n");
    printf("                          reproducible capture (skips the update-queue and\n");
    printf("                          display-refresh timers so the channel stays alive)\n");
    printf("  unfreeze                Reverse freeze: resume paused timers, re-enable\n");
    printf("                          animations\n");
    printf("  log [-n N]              Tail the app's in-memory log ring (default 50 lines)\n");
    printf("  shutdown, quit          Ask the running app to exit\n");
    printf("\nScenarios:\n");
    printf("  scenario <name>         Apply named mock scenario\n");
    printf("  list_scenarios          List available mock scenarios\n");
    printf("\nInteractive:\n");
    printf("  repl                    Interactive REPL with line editing and history\n");
    printf("\nOptions:\n");
    printf("  -s, --socket <path>     Socket path (default: auto-detect)\n");
    printf("  --json                  Emit the raw JSON-RPC result/error (one-shot only)\n");
    printf("  -h, --help              Show this help\n");
    printf("\nSocket path resolution:\n");
    printf("  1. --socket <path>  (explicit)\n");
    printf("  2. $XDG_RUNTIME_DIR/helixscreen-control.sock\n");
    printf("  3. /tmp/helixscreen-control.sock\n");
}

// ---------------------------------------------------------------------------
// Socket helpers
// ---------------------------------------------------------------------------

static std::string resolve_socket_path(const std::string& override_path) {
    if (!override_path.empty()) {
        return override_path;
    }

    const char* xdg_runtime = getenv("XDG_RUNTIME_DIR");
    const std::string dir =
        (xdg_runtime && xdg_runtime[0] != '\0') ? std::string(xdg_runtime) : std::string("/tmp");
    const std::string well_known = dir + "/helixscreen-control.sock";

    // Liveness, not mere existence: a crashed instance leaves the file behind, and
    // connecting to it fails with a confusing error instead of finding the app that
    // is actually running on a pid-suffixed path.
    if (helix::UnixSocketTransport::path_is_live(well_known)) {
        return well_known;
    }

    std::vector<std::string> instances = helix::UnixSocketTransport::discover_instances(dir);
    if (instances.size() == 1) {
        return instances[0];
    }
    if (instances.size() > 1) {
        // Guessing here would silently drive the wrong app — exactly the failure
        // this whole change exists to prevent. Make the user choose.
        fprintf(stderr, "Error: several HelixScreen instances are running. Pick one with -s:\n");
        for (const std::string& path : instances) {
            fprintf(stderr, "  --socket %s\n", path.c_str());
        }
        exit(1);
    }

    return well_known; // Nothing running; report against the expected path.
}

static int connect_to_server(const std::string& socket_path) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        fprintf(stderr, "Error: Failed to create socket: %s\n", strerror(errno));
        return -1;
    }

    struct sockaddr_un addr {};
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

static std::string read_response(int fd, int timeout_sec = 30) {
    std::string buffer;
    char chunk[4096];

    // Set a read timeout
    struct timeval tv;
    tv.tv_sec = timeout_sec;
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

// ---------------------------------------------------------------------------
// JSON-RPC helpers
// ---------------------------------------------------------------------------

static int g_request_id = 1;

/// When set, the one-shot client emits the raw JSON-RPC `result` instead of the
/// human-formatted rendering. Errors from the server go to stderr as the raw
/// `error` object. Client-side usage errors stay human-readable — a caller that
/// mistyped a command name needs prose, not a protocol object.
static bool g_json_output = false;

static nlohmann::json build_request(const std::string& method,
                                    const nlohmann::json& params = nlohmann::json::object()) {
    return {{"jsonrpc", "2.0"}, {"method", method}, {"params", params}, {"id", g_request_id++}};
}

/// Handle a JSON-RPC response. Returns 0 on success, 1 on error.
/// If out_result is provided, stores the result JSON there instead of printing.
static int handle_response(const std::string& raw_response, nlohmann::json* out_result = nullptr) {
    if (raw_response.empty()) {
        fprintf(stderr, "Error: Empty response from server\n");
        return 1;
    }

    try {
        auto response = nlohmann::json::parse(raw_response);

        if (response.contains("error")) {
            auto& error = response["error"];
            if (g_json_output) {
                fprintf(stderr, "%s\n", error.dump().c_str());
            } else {
                fprintf(stderr, "Error: %s (code %d)\n",
                        error.value("message", "Unknown error").c_str(),
                        error.value("code", -1));
            }
            return 1;
        }

        if (response.contains("result")) {
            if (out_result) {
                *out_result = response["result"];
            } else if (g_json_output) {
                // Raw, single-line. Callers pipe this to jq or json.loads.
                printf("%s\n", response["result"].dump().c_str());
            } else {
                auto& result = response["result"];
                if (result.is_string()) {
                    printf("%s\n", result.get<std::string>().c_str());
                } else if (result.is_object() && result.contains("lines") &&
                           result["lines"].is_array()) {
                    // Log tail: print the lines as lines. JSON-escaped log output
                    // is unreadable and defeats piping it to grep.
                    for (const auto& line : result["lines"]) {
                        if (line.is_string()) {
                            printf("%s\n", line.get<std::string>().c_str());
                        }
                    }
                } else {
                    printf("%s\n", result.dump(2).c_str());
                }
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

// ---------------------------------------------------------------------------
// Command building from tokens (shared by CLI and REPL)
// ---------------------------------------------------------------------------

/// Try to parse a string as int, return JSON int on success, JSON string otherwise.
/// Only treats the token as an int when the ENTIRE string is numeric — otherwise
/// std::stoi("3dbenchy") would silently send 3, and a digit-leading string value
/// (e.g. a filename) could never be set on a string subject/widget.
static nlohmann::json parse_value(const std::string& val_str) {
    try {
        size_t consumed = 0;
        int v = std::stoi(val_str, &consumed);
        if (consumed == val_str.size()) {
            return v;
        }
    } catch (...) {
        // fall through to string
    }
    return val_str;
}

/// Build a target param object for click/set/scroll. "@s/3/1" (or a bare
/// "s/3/1") addresses a widget by its describe_screen path, unique even for
/// duplicate names; anything else is treated as a widget name.
static nlohmann::json target_param(const std::string& t) {
    if (!t.empty() && t[0] == '@') {
        return {{"path", t.substr(1)}};
    }
    // Accept a bare path too, so a locator pasted straight out of `ls` works
    // without remembering the '@'. Widget names never contain '/', so the
    // "s/1/2" / "t/0" shape is unambiguous.
    if (helix::is_bare_path(t)) {
        return {{"path", t}};
    }
    return {{"name", t}};
}

/// Build a JSON-RPC request from a command + args vector.
/// Returns empty json on parse error (with error printed to stderr).
static nlohmann::json build_request_from_tokens(const std::vector<std::string>& tokens) {
    if (tokens.empty())
        return {};

    const auto& cmd = tokens[0];

    if (cmd == "ping") {
        return build_request("ping");
    } else if (cmd == "navigate" || cmd == "cd") {
        // fs metaphor: `cd ..` pops a level, `cd <name>` descends into a panel
        // or a clickable widget (overlay/modal trigger).
        if (cmd == "cd" && tokens.size() >= 2 && tokens[1] == "..") {
            return build_request("go_back");
        }
        if (tokens.size() < 2) {
            fprintf(stderr, "Error: %s requires a target (panel or widget name)\n", cmd.c_str());
            return {};
        }
        return build_request("navigate", {{"panel", tokens[1]}});
    } else if (cmd == "go_back" || cmd == "back") {
        return build_request("go_back");
    } else if (cmd == "list_panels") {
        return build_request("list_panels");
    } else if (cmd == "list_components") {
        return build_request("list_components");
    } else if (cmd == "list_callbacks") {
        return build_request("list_callbacks");
    } else if (cmd == "current" || cmd == "pwd") {
        return build_request("get_current");
    } else if (cmd == "screenshot") {
        // Optional destination; a .png suffix asks the app to encode PNG.
        if (tokens.size() >= 2) {
            return build_request("screenshot", {{"path", tokens[1]}});
        }
        return build_request("screenshot");
    } else if (cmd == "shutdown" || cmd == "quit" || cmd == "exit") {
        // In the REPL, quit/exit are intercepted before dispatch (they leave the
        // REPL); reaching here means the one-shot client, where stopping the app
        // is the only sensible reading.
        return build_request("shutdown");
    } else if (cmd == "log") {
        nlohmann::json params = nlohmann::json::object();
        for (size_t i = 1; i < tokens.size(); i++) {
            if ((tokens[i] == "-n" || tokens[i] == "--lines") && i + 1 < tokens.size()) {
                params["lines"] = std::atoi(tokens[i + 1].c_str());
                i++;
            } else {
                params["lines"] = std::atoi(tokens[i].c_str());
            }
        }
        return build_request("log", params);
    } else if (cmd == "status") {
        return build_request("status");
    } else if (cmd == "demo") {
        if (tokens.size() < 2) {
            fprintf(stderr, "Error: demo requires a name (preflight-check, runout-modal, ams, "
                            "lock-screen, print-status, print-tune, ams, camera)\n");
            return {};
        }
        return build_request("demo", {{"name", tokens[1]}});
    } else if (cmd == "get") {
        if (tokens.size() < 2) {
            fprintf(stderr, "Error: get requires a subject name\n");
            return {};
        }
        return build_request("get", {{"name", tokens[1]}});
    } else if (cmd == "set") {
        if (tokens.size() < 3) {
            fprintf(stderr, "Error: set requires a subject name and value\n");
            return {};
        }
        return build_request("set", {{"name", tokens[1]}, {"value", parse_value(tokens[2])}});
    } else if (cmd == "list_subjects") {
        return build_request("list_subjects");
    } else if (cmd == "wait_for") {
        if (tokens.size() < 3) {
            fprintf(stderr, "Error: wait_for requires a subject name and value\n");
            return {};
        }
        nlohmann::json params = {{"name", tokens[1]}, {"value", tokens[2]}};
        for (size_t i = 3; i < tokens.size(); i++) {
            if ((tokens[i] == "--timeout" || tokens[i] == "-t") && i + 1 < tokens.size()) {
                params["timeout"] = std::atoi(tokens[i + 1].c_str());
                i++;
            }
        }
        return build_request("wait_for", params);
    } else if (cmd == "wait_idle") {
        nlohmann::json params = nlohmann::json::object();
        for (size_t i = 1; i < tokens.size(); i++) {
            if ((tokens[i] == "--timeout" || tokens[i] == "-t") && i + 1 < tokens.size()) {
                params["timeout"] = std::atof(tokens[i + 1].c_str());
                i++;
            }
        }
        return build_request("wait_idle", params);
    } else if (cmd == "freeze") {
        return build_request("freeze");
    } else if (cmd == "unfreeze") {
        return build_request("unfreeze");
    } else if (cmd == "click") {
        if (tokens.size() < 2) {
            fprintf(stderr, "Error: click requires a widget name or @path\n");
            return {};
        }
        return build_request("click", target_param(tokens[1]));
    } else if (cmd == "focus") {
        if (tokens.size() < 2) {
            fprintf(stderr, "Error: focus requires a widget name or @path\n");
            return {};
        }
        return build_request("focus", target_param(tokens[1]));
    } else if (cmd == "press" || cmd == "move") {
        if (tokens.size() < 3) {
            fprintf(stderr, "Error: %s requires x and y coordinates\n", cmd.c_str());
            return {};
        }
        nlohmann::json params;
        params["x"] = std::atoi(tokens[1].c_str());
        params["y"] = std::atoi(tokens[2].c_str());
        return build_request(cmd == "press" ? "pointer_press" : "pointer_move", params);
    } else if (cmd == "release") {
        nlohmann::json params;
        // Optional coordinates; without them the pointer lifts where it already is.
        if (tokens.size() >= 3) {
            params["x"] = std::atoi(tokens[1].c_str());
            params["y"] = std::atoi(tokens[2].c_str());
        }
        return build_request("pointer_release", params);
    } else if (cmd == "set_value") {
        if (tokens.size() < 3) {
            fprintf(stderr, "Error: set_value requires a widget name/@path and value\n");
            return {};
        }
        nlohmann::json p = target_param(tokens[1]);
        p["value"] = parse_value(tokens[2]);
        return build_request("set_widget_value", p);
    } else if (cmd == "scenario") {
        if (tokens.size() < 2) {
            fprintf(stderr, "Error: scenario requires a name\n");
            return {};
        }
        return build_request("scenario", {{"name", tokens[1]}});
    } else if (cmd == "list_scenarios") {
        return build_request("list_scenarios");
    } else if (cmd == "wake" || cmd == "screensaver") {
        // `wake` or `screensaver off` — reset the idle timer / dismiss the saver.
        return build_request("wake");
    } else if (cmd == "describe_screen" || cmd == "ls") {
        // `ls` lists the whole screen; `ls <name|@path>` scopes to a subtree.
        if (tokens.size() >= 2) {
            return build_request("describe_screen", target_param(tokens[1]));
        }
        return build_request("describe_screen");
    } else if (cmd == "scroll") {
        if (tokens.size() < 2) {
            fprintf(stderr, "Error: scroll requires a widget name/@path [dx dy]\n");
            return {};
        }
        nlohmann::json params = target_param(tokens[1]);
        if (tokens.size() >= 4) {
            params["dx"] = std::atoi(tokens[2].c_str());
            params["dy"] = std::atoi(tokens[3].c_str());
        }
        return build_request("scroll", params);
    } else if (cmd == "geom") {
        if (tokens.size() < 2) {
            fprintf(stderr, "Error: geom requires a widget name/@path [depth]\n");
            return {};
        }
        nlohmann::json params = target_param(tokens[1]);
        if (tokens.size() >= 3) {
            params["depth"] = std::atoi(tokens[2].c_str());
        }
        return build_request("geom", params);
    } else if (cmd == "get_const") {
        if (tokens.size() < 2) {
            fprintf(stderr, "Error: get_const requires <name>, <scope> <name>, or <scope> \n");
            return {};
        }
        nlohmann::json params;
        if (tokens.size() >= 3) {
            params["scope"] = tokens[1];
            params["name"] = tokens[2];
        } else if (tokens[1].rfind("@", 0) == 0) {
            // @scope with no name dumps every const in that scope.
            params["scope"] = tokens[1].substr(1);
        } else {
            params["name"] = tokens[1];
        }
        return build_request("get_const", params);
    }

    fprintf(stderr, "Unknown command: %s\n", cmd.c_str());
    return {};
}

// ---------------------------------------------------------------------------
// REPL
// ---------------------------------------------------------------------------

// All known commands for tab completion
static const char* REPL_COMMANDS[] = {"ping",
                                      "navigate",
                                      "cd",
                                      "go_back",
                                      "back",
                                      "list_panels",
                                      "list_components",
                                      "list_callbacks",
                                      "current",
                                      "pwd",
                                      "screenshot",
                                      "status",
                                      "wake",
                                      "demo",
                                      "get",
                                      "set",
                                      "list_subjects",
                                      "wait_for",
                                      "wait_idle",
                                      "freeze",
                                      "unfreeze",
                                      "ls",
                                      "describe_screen",
                                      "click",
                                      "set_value",
                                      "focus",
                                      "press",
                                      "move",
                                      "release",
                                      "scroll",
                                      "scenario",
                                      "list_scenarios",
                                      "help",
                                      "refresh",
                                      "log",
                                      "shutdown",
                                      "geom",
                                      "get_const",
                                      "quit",
                                      "exit",
                                      nullptr};

// Cached subject names for tab completion (populated lazily)
static std::vector<std::string> g_cached_subjects;
static std::vector<std::string> g_cached_scenarios;
static std::vector<std::string> g_cached_panels;

static void repl_completion(const char* buf, linenoiseCompletions* lc) {
    std::string input(buf);

    // Find the first space to determine if we're completing a command or an argument
    size_t space_pos = input.find(' ');

    if (space_pos == std::string::npos) {
        // Completing a command name
        for (int i = 0; REPL_COMMANDS[i]; i++) {
            if (strncmp(buf, REPL_COMMANDS[i], input.length()) == 0) {
                linenoiseAddCompletion(lc, REPL_COMMANDS[i]);
            }
        }
    } else {
        // Completing an argument
        std::string cmd = input.substr(0, space_pos);
        std::string partial = input.substr(space_pos + 1);

        // Strip leading spaces from partial
        while (!partial.empty() && partial[0] == ' ')
            partial.erase(0, 1);

        const std::vector<std::string>* candidates = nullptr;

        if (cmd == "get" || cmd == "set" || cmd == "wait_for") {
            candidates = &g_cached_subjects;
        } else if (cmd == "scenario") {
            candidates = &g_cached_scenarios;
        } else if (cmd == "navigate") {
            candidates = &g_cached_panels;
        }

        if (candidates) {
            for (const auto& c : *candidates) {
                if (c.compare(0, partial.length(), partial) == 0) {
                    std::string completion = cmd + " " + c;
                    linenoiseAddCompletion(lc, completion.c_str());
                }
            }
        }
    }
}

static char* repl_hints(const char* buf, int* color, int* bold) {
    std::string input(buf);
    *color = 90; // dark gray
    *bold = 0;

    if (input == "navigate")
        return strdup(" <panel>");
    if (input == "get")
        return strdup(" <subject>");
    if (input == "set")
        return strdup(" <subject> <value>");
    if (input == "click")
        return strdup(" <widget>");
    if (input == "set_value")
        return strdup(" <widget> <value>");
    if (input == "scenario")
        return strdup(" <name>");
    if (input == "wait_for")
        return strdup(" <subject> <value> [--timeout N]");
    if (input == "wait_idle")
        return strdup(" [--timeout N]");

    return nullptr;
}

static void repl_free_hints(void* ptr) {
    free(ptr);
}

/// Split a string into tokens by whitespace
static std::vector<std::string> tokenize(const std::string& line) {
    std::vector<std::string> tokens;
    std::string token;
    bool in_quotes = false;
    char quote_char = 0;

    for (char c : line) {
        if (in_quotes) {
            if (c == quote_char) {
                in_quotes = false;
            } else {
                token += c;
            }
        } else if (c == '"' || c == '\'') {
            in_quotes = true;
            quote_char = c;
        } else if (c == ' ' || c == '\t') {
            if (!token.empty()) {
                tokens.push_back(token);
                token.clear();
            }
        } else {
            token += c;
        }
    }
    if (!token.empty()) {
        tokens.push_back(token);
    }
    return tokens;
}

/// Send a request and get the parsed result. Returns true on success.
static bool repl_rpc(int fd, const std::string& method, const nlohmann::json& params,
                     nlohmann::json& result) {
    auto request = build_request(method, params);
    if (!send_request(fd, request))
        return false;
    auto raw = read_response(fd, 30);
    return handle_response(raw, &result) == 0;
}

/// Populate tab-completion caches by querying the server
static void repl_populate_caches(int fd) {
    nlohmann::json result;

    // Cache subject names
    if (repl_rpc(fd, "list_subjects", {}, result) && result.is_array()) {
        g_cached_subjects.clear();
        for (const auto& entry : result) {
            if (entry.contains("name")) {
                g_cached_subjects.push_back(entry["name"].get<std::string>());
            }
        }
    }

    // Cache scenario names
    if (repl_rpc(fd, "list_scenarios", {}, result) && result.is_array()) {
        g_cached_scenarios.clear();
        for (const auto& entry : result) {
            if (entry.contains("name")) {
                g_cached_scenarios.push_back(entry["name"].get<std::string>());
            } else if (entry.is_string()) {
                g_cached_scenarios.push_back(entry.get<std::string>());
            }
        }
    }

    // Cache panel names
    if (repl_rpc(fd, "list_panels", {}, result) && result.is_array()) {
        g_cached_panels.clear();
        for (const auto& entry : result) {
            if (entry.is_string()) {
                g_cached_panels.push_back(entry.get<std::string>());
            }
        }
    }
}

static void repl_print_help() {
    printf("Commands:\n");
    printf("  ping                      Health check\n");
    printf("  navigate <panel>          Switch to panel/overlay\n");
    printf("  go_back                   Pop current overlay\n");
    printf("  list_panels               List available panels\n");
    printf("  list_components           List every registered XML component\n");
    printf("  list_callbacks            List every registered event-callback name\n");
    printf("  current                   Show current panel and overlay stack\n");
    printf("  screenshot [path]         Capture the screen (.png path encodes PNG)\n");
    printf("  status                    Full status summary\n");
    printf("  get <subject>             Read subject value\n");
    printf("  set <subject> <value>     Set subject value\n");
    printf("  list_subjects             List all subjects\n");
    printf("  wait_for <s> <v> [-t N]   Wait for subject to match value\n");
    printf("  wait_idle [-t N]          Block until the UI has settled (default 10s)\n");
    printf("  freeze                    Stop animations + pause timers for capture\n");
    printf("  unfreeze                  Reverse freeze\n");
    printf("  ls [target]               List widgets here, or one widget's subtree\n");
    printf("  click <widget>            Click a widget (descends into composite rows)\n");
    printf("  set_value <widget> <v>    Set widget value\n");
    printf("  scenario <name>           Apply mock scenario\n");
    printf("  list_scenarios            List available scenarios\n");
    printf("  log [-n N]                Tail the app's in-memory log ring\n");
    printf("\n");
    printf("  refresh                   Reload tab-completion caches\n");
    printf("  help                      Show this help\n");
    printf("  shutdown                  Stop the running app (not just this REPL)\n");
    printf("  quit, exit, Ctrl-D        Exit REPL\n");
    printf("\nTab completion works for commands, subjects, panels, and scenarios.\n");
    printf("Emacs keybindings: Ctrl-A/E, Ctrl-B/F, Ctrl-K/U, Ctrl-W, Ctrl-D, etc.\n");
}

/// Resolve history file path: $XDG_DATA_HOME/helixscreen/helixctl_history
/// or ~/.local/share/helixscreen/helixctl_history
static std::string resolve_history_path() {
    std::string dir;
    const char* xdg_data = getenv("XDG_DATA_HOME");
    if (xdg_data && xdg_data[0] != '\0') {
        dir = std::string(xdg_data) + "/helixscreen";
    } else {
        const char* home = getenv("HOME");
        if (home) {
            dir = std::string(home) + "/.local/share/helixscreen";
        } else {
            return ""; // Can't determine home
        }
    }
    return dir + "/helixctl_history";
}

// Fetch the JSON-RPC result for a request; returns false on any failure.
static bool query_result(const std::string& socket_path, const nlohmann::json& request,
                         nlohmann::json& out) {
    int fd = connect_to_server(socket_path);
    if (fd < 0) {
        return false;
    }
    bool ok = false;
    if (send_request(fd, request)) {
        auto raw = read_response(fd, 10);
        ok = (handle_response(raw, &out) == 0);
    }
    close(fd);
    return ok;
}

// Filesystem-style breadcrumb prompt from the server's current location,
// e.g. "controls / fan_control_overlay > ".
static std::string build_prompt(const std::string& socket_path) {
    nlohmann::json r;
    if (!query_result(socket_path, build_request("get_current"), r)) {
        return "helixctl(offline)> ";
    }
    std::string crumb = r.value("panel", std::string("?"));
    if (r.contains("overlays") && r["overlays"].is_array()) {
        for (auto& o : r["overlays"]) {
            if (o.is_string()) {
                crumb += " / " + o.get<std::string>();
            }
        }
    }
    return crumb + " > ";
}

// Pretty-print describe_screen output (`ls`) grouped by what you can do to each
// widget. Duplicate-named widgets get their @path appended so they stay
// addressable.
static void print_describe_grouped(const nlohmann::json& result) {
    if (!result.contains("widgets") || !result["widgets"].is_array()) {
        printf("%s\n", result.dump(2).c_str());
        return;
    }
    const auto& widgets = result["widgets"];
    std::map<std::string, int> name_count;
    for (const auto& w : widgets) {
        name_count[w.value("name", std::string())]++;
    }
    const char* verbs[] = {"click", "fill", "toggle", "set", "scroll"};
    for (const char* verb : verbs) {
        std::vector<std::string> items;
        for (const auto& w : widgets) {
            bool has = false;
            for (const auto& a : w["actions"]) {
                if (a == verb) {
                    has = true;
                    break;
                }
            }
            if (!has) {
                continue;
            }
            std::string name = w.value("name", std::string("?"));
            std::string label = name;
            if (name_count[name] > 1) {
                label += " @" + w.value("path", std::string());
            }
            if (w.contains("value")) {
                label += "=" + (w["value"].is_string() ? w["value"].get<std::string>()
                                                       : w["value"].dump());
            }
            items.push_back(label);
        }
        if (items.empty()) {
            continue;
        }
        printf("  %-7s", verb);
        for (const auto& it : items) {
            printf(" %s", it.c_str());
        }
        printf("\n");
    }
    printf("  (%zu widgets — `ls` shows all; @path targets any one)\n", widgets.size());
}

static int run_repl(const std::string& socket_path) {
    // We reconnect for each command for simplicity — avoids handling connection
    // loss mid-session and keeps the REPL stateless. The server does support
    // multiple requests per connection if we want to optimize later.

    printf("helix-screen control REPL — type 'help' for commands, Tab for completion, "
           "Ctrl-D to quit\n");

    // Set up linenoise
    linenoiseSetCompletionCallback(repl_completion);
    linenoiseSetHintsCallback(repl_hints);
    linenoiseSetFreeHintsCallback(repl_free_hints);
    linenoiseHistorySetMaxLen(500);

    // Load history
    std::string history_path = resolve_history_path();
    if (!history_path.empty()) {
        linenoiseHistoryLoad(history_path.c_str());
    }

    // Populate completion caches
    {
        int fd = connect_to_server(socket_path);
        if (fd < 0) {
            return 1;
        }
        repl_populate_caches(fd);
        close(fd);
    }

    int last_result = 0;
    char* line;

    while ((line = linenoise(build_prompt(socket_path).c_str())) != nullptr) {
        std::string input(line);
        linenoiseFree(line);

        // Skip empty lines
        auto tokens = tokenize(input);
        if (tokens.empty())
            continue;

        // Add to history
        linenoiseHistoryAdd(input.c_str());

        // Handle REPL-only commands
        if (tokens[0] == "quit" || tokens[0] == "exit") {
            break;
        }
        if (tokens[0] == "help") {
            repl_print_help();
            continue;
        }
        if (tokens[0] == "refresh") {
            int fd = connect_to_server(socket_path);
            if (fd >= 0) {
                repl_populate_caches(fd);
                close(fd);
                printf("Caches refreshed (%zu subjects, %zu scenarios, %zu panels)\n",
                       g_cached_subjects.size(), g_cached_scenarios.size(), g_cached_panels.size());
            }
            continue;
        }

        // `ls` gets a grouped, human-readable rendering instead of raw JSON.
        if (tokens[0] == "ls" || tokens[0] == "describe_screen") {
            nlohmann::json r;
            if (query_result(socket_path, build_request("describe_screen"), r)) {
                print_describe_grouped(r);
            }
            continue;
        }

        // Build and send request
        auto request = build_request_from_tokens(tokens);
        if (request.is_null())
            continue;

        int fd = connect_to_server(socket_path);
        if (fd < 0) {
            fprintf(stderr, "Connection lost. Is HelixScreen still running?\n");
            continue;
        }

        if (!send_request(fd, request)) {
            close(fd);
            continue;
        }

        // Use longer timeout for the blocking commands — the caller's own
        // --timeout can exceed the default socket read window otherwise.
        int timeout = (tokens[0] == "wait_for" || tokens[0] == "wait_idle") ? 120 : 30;
        auto raw = read_response(fd, timeout);
        close(fd);

        last_result = handle_response(raw);
    }

    // Save history
    if (!history_path.empty()) {
        // Ensure parent directories exist (simple recursive mkdir)
        std::string dir = history_path.substr(0, history_path.rfind('/'));
        for (size_t i = 1; i < dir.size(); i++) {
            if (dir[i] == '/') {
                dir[i] = '\0';
                mkdir(dir.c_str(), 0755);
                dir[i] = '/';
            }
        }
        mkdir(dir.c_str(), 0755);
        linenoiseHistorySave(history_path.c_str());
    }

    printf("\n");
    return last_result;
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int helix::remote_client_main(int argc, char** argv) {
    std::string socket_path;
    int arg_start = 1;

    // Parse options
    while (arg_start < argc) {
        if (strcmp(argv[arg_start], "-h") == 0 || strcmp(argv[arg_start], "--help") == 0) {
            print_usage();
            return 0;
        }
        if (strcmp(argv[arg_start], "--json") == 0) {
            g_json_output = true;
            arg_start++;
            continue;
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

    // No command → drop into the interactive fs-style REPL.
    if (arg_start >= argc) {
        g_json_output = false; // --json is a one-shot flag; the REPL always formats
        return run_repl(resolve_socket_path(socket_path));
    }

    std::string command = argv[arg_start];

    // `ctl help` is the first thing anyone types; make it work as a command and
    // not just as the -h/--help option.
    if (command == "help" || command == "?") {
        print_usage();
        return 0;
    }

    // Explicit REPL mode
    if (command == "repl") {
        g_json_output = false; // --json is a one-shot flag; the REPL always formats
        std::string resolved_path = resolve_socket_path(socket_path);
        return run_repl(resolved_path);
    }

    // Single-command mode — build tokens from argv
    std::vector<std::string> tokens;
    for (int i = arg_start; i < argc; i++) {
        tokens.push_back(argv[i]);
    }

    auto request = build_request_from_tokens(tokens);
    if (request.is_null()) {
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

    // Use longer timeout for the blocking commands — the caller's own
    // --timeout can exceed the default socket read window otherwise.
    int timeout = (command == "wait_for" || command == "wait_idle") ? 120 : 30;
    std::string response = read_response(fd, timeout);
    close(fd);

    return handle_response(response);
}
