// Copyright (C) 2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "wifi_interface.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

namespace fs = std::filesystem;

namespace helix::wifi {

namespace detail {

std::string parse_wpa_state(const std::string& status_reply) {
    static const std::string prefix = "wpa_state=";
    std::istringstream iss(status_reply);
    std::string line;
    while (std::getline(iss, line)) {
        if (line.compare(0, prefix.size(), prefix) != 0)
            continue;
        std::string value = line.substr(prefix.size());
        while (!value.empty() && (value.back() == '\r' || value.back() == '\n'))
            value.pop_back();
        return value;
    }
    return {};
}

pid_t find_daemon_for_interface(const std::string& proc_root, const std::string& netdev,
                                std::string& conf_path_out) {
    conf_path_out.clear();

    std::error_code ec;
    if (proc_root.empty() || !fs::is_directory(proc_root, ec) || ec)
        return -1;

    for (const auto& entry : fs::directory_iterator(proc_root, ec)) {
        if (ec)
            break;

        // PID directories only.
        const std::string name = entry.path().filename().string();
        if (name.empty() ||
            !std::all_of(name.begin(), name.end(), [](unsigned char c) { return std::isdigit(c); }))
            continue;

        // /proc/<pid>/cmdline is NUL-separated argv.
        std::ifstream cmd(entry.path() / "cmdline", std::ios::binary);
        if (!cmd.is_open())
            continue;
        std::vector<std::string> argv;
        std::string arg;
        while (std::getline(cmd, arg, '\0'))
            argv.push_back(arg);
        if (argv.empty())
            continue;

        // argv[0] basename must be wpa_supplicant.
        if (fs::path(argv[0]).filename().string() != "wpa_supplicant")
            continue;

        // Collect -i/-iVALUE (interface) and -c/-cVALUE (config path). Both
        // accept a separate "-X value" or a joined "-Xvalue" form.
        std::string i_val, c_val;
        bool has_i = false;
        for (size_t i = 1; i < argv.size(); ++i) {
            const std::string& a = argv[i];
            if (a == "-i" && i + 1 < argv.size()) {
                i_val = argv[++i];
                has_i = true;
            } else if (a.rfind("-i", 0) == 0 && a.size() > 2) {
                i_val = a.substr(2);
                has_i = true;
            } else if (a == "-c" && i + 1 < argv.size()) {
                c_val = argv[++i];
            } else if (a.rfind("-c", 0) == 0 && a.size() > 2) {
                c_val = a.substr(2);
            }
        }

        // Not the daemon that owns the interface we care about — keep
        // scanning. This is the fix: the code this replaces returned on the
        // first wpa_supplicant found, regardless of which interface it owned.
        if (!has_i || i_val != netdev)
            continue;

        conf_path_out = c_val;
        return static_cast<pid_t>(std::strtol(name.c_str(), nullptr, 10));
    }
    return -1;
}

} // namespace detail

std::optional<WifiInterface> resolve_interface(const Roots& roots, const StatusProbe& probe) {
    std::error_code ec;
    if (roots.ctrl.empty() || !fs::is_directory(roots.ctrl, ec) || ec)
        return std::nullopt;

    std::vector<std::string> names;
    for (const auto& entry : fs::directory_iterator(roots.ctrl, ec)) {
        if (ec)
            break;
        names.push_back(entry.path().filename().string());
    }
    // Sorted purely for determinism among equally-ranked candidates.
    std::sort(names.begin(), names.end());

    std::string completed_name;
    std::string fallback_name;
    bool have_completed = false;
    bool have_fallback = false;

    for (const auto& name : names) {
        const std::string socket_path = roots.ctrl + "/" + name;
        const std::string reply = probe(socket_path);
        if (reply.empty())
            continue;

        // Filter out control-only sockets (p2p-dev-*, etc.) that have no
        // backing wireless netdev.
        std::error_code wec;
        const std::string wireless_dir = roots.sys + "/class/net/" + name + "/wireless";
        if (!fs::is_directory(wireless_dir, wec) || wec)
            continue;

        if (!have_fallback) {
            fallback_name = name;
            have_fallback = true;
        }
        if (!have_completed && detail::parse_wpa_state(reply) == "COMPLETED") {
            completed_name = name;
            have_completed = true;
        }
    }

    if (!have_completed && !have_fallback)
        return std::nullopt;

    WifiInterface result;
    result.netdev = have_completed ? completed_name : fallback_name;
    result.ctrl_socket = roots.ctrl + "/" + result.netdev;
    result.associated = have_completed;
    result.daemon_pid =
        detail::find_daemon_for_interface(roots.proc, result.netdev, result.conf_path);
    // rfkill_node intentionally left empty here — filled in by a later task.

    return result;
}

} // namespace helix::wifi
