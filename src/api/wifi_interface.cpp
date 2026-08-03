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

std::vector<DaemonInfo> list_wpa_daemons(const std::string& proc_root) {
    std::vector<DaemonInfo> daemons;

    std::error_code ec;
    if (proc_root.empty() || !fs::is_directory(proc_root, ec) || ec)
        return daemons;

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
        for (size_t i = 1; i < argv.size(); ++i) {
            const std::string& a = argv[i];
            if (a == "-i" && i + 1 < argv.size()) {
                i_val = argv[++i];
            } else if (a.rfind("-i", 0) == 0 && a.size() > 2) {
                i_val = a.substr(2);
            } else if (a == "-c" && i + 1 < argv.size()) {
                c_val = argv[++i];
            } else if (a.rfind("-c", 0) == 0 && a.size() > 2) {
                c_val = a.substr(2);
            }
        }

        DaemonInfo d;
        d.pid = static_cast<pid_t>(std::strtol(name.c_str(), nullptr, 10));
        d.iface = std::move(i_val);
        d.conf_path = std::move(c_val);
        daemons.push_back(std::move(d));
    }
    return daemons;
}

pid_t find_daemon_for_interface(const std::string& proc_root, const std::string& netdev,
                                std::string& conf_path_out) {
    conf_path_out.clear();

    // Not the daemon that owns the interface we care about — keep scanning.
    // This is the fix: the code this replaces returned on the first
    // wpa_supplicant found, regardless of which interface it owned.
    for (const auto& d : list_wpa_daemons(proc_root)) {
        if (d.iface.empty() || d.iface != netdev)
            continue;

        conf_path_out = d.conf_path;
        return d.pid;
    }
    return -1;
}

std::string find_rfkill_node(const std::string& sys_root, const std::string& netdev) {
    std::error_code ec;

    // Try to find an rfkill entry in the netdev's phy80211 directory.
    const std::string phy_dir = sys_root + "/class/net/" + netdev + "/phy80211";
    if (fs::is_directory(phy_dir, ec) && !ec) {
        for (const auto& entry : fs::directory_iterator(phy_dir, ec)) {
            if (ec)
                break;
            const std::string name = entry.path().filename().string();
            if (name.compare(0, 6, "rfkill") == 0) {
                return sys_root + "/class/rfkill/" + name;
            }
        }
    }

    // Fall back to the first "wlan" typed switch in /sys/class/rfkill/.
    const std::string rfkill_dir = sys_root + "/class/rfkill";
    if (fs::is_directory(rfkill_dir, ec) && !ec) {
        for (const auto& entry : fs::directory_iterator(rfkill_dir, ec)) {
            if (ec)
                break;

            const std::string rfkill_name = entry.path().filename().string();
            const std::string type_file = entry.path() / "type";

            std::ifstream f(type_file);
            if (!f.is_open())
                continue;

            std::string type_value;
            std::getline(f, type_value);
            // Trim trailing whitespace.
            while (!type_value.empty() && (type_value.back() == '\r' || type_value.back() == '\n' ||
                                           type_value.back() == ' ' || type_value.back() == '\t'))
                type_value.pop_back();

            if (type_value == "wlan") {
                return rfkill_dir + "/" + rfkill_name;
            }
        }
    }

    return "";
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
    result.rfkill_node = detail::find_rfkill_node(roots.sys, result.netdev);

    return result;
}

} // namespace helix::wifi
