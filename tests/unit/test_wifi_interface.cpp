// Copyright (C) 2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "wifi_interface.h"

#include <cstdlib>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>

#include "../catch_amalgamated.hpp"

namespace {

/// Build a throwaway directory tree standing in for /sys, /proc and the
/// wpa_supplicant control directory. Real resolution logic, fake filesystem —
/// this is what makes the two-daemon case testable without the device.
struct FakeRoot {
    std::string base;

    FakeRoot() {
        char tmpl[] = "/tmp/helix-wifi-iface-XXXXXX";
        base = ::mkdtemp(tmpl);
    }
    ~FakeRoot() {
        // Recursive delete via nftw would drag in more headers than this needs;
        // the tree is shallow and fully known to us.
        std::string cmd = "rm -rf '" + base + "'";
        (void)::system(cmd.c_str());
    }

    std::string mkdirs(const std::string& rel) const {
        std::string path = base;
        size_t start = 0;
        std::string sub = rel;
        while (start <= sub.size()) {
            size_t slash = sub.find('/', start);
            std::string piece = sub.substr(0, slash == std::string::npos ? sub.size() : slash);
            ::mkdir((base + "/" + piece).c_str(), 0755);
            if (slash == std::string::npos)
                break;
            start = slash + 1;
        }
        return base + "/" + rel;
    }

    void write(const std::string& rel, const std::string& contents) const {
        const size_t slash = rel.find_last_of('/');
        if (slash != std::string::npos)
            mkdirs(rel.substr(0, slash));
        std::ofstream f(base + "/" + rel, std::ios::binary);
        f.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    }

    /// /proc/<pid>/cmdline is NUL-separated argv.
    void write_cmdline(int pid, const std::vector<std::string>& argv) const {
        std::string blob;
        for (const auto& a : argv) {
            blob += a;
            blob.push_back('\0');
        }
        write("proc/" + std::to_string(pid) + "/cmdline", blob);
    }
};

helix::wifi::Roots roots_for(const FakeRoot& fr) {
    helix::wifi::Roots r;
    r.sys = fr.base + "/sys";
    r.proc = fr.base + "/proc";
    r.ctrl = fr.base + "/run/wpa_supplicant";
    return r;
}

} // namespace

TEST_CASE("resolve_interface picks the associated socket", "[wifi][interface]") {
    FakeRoot fr;
    fr.mkdirs("run/wpa_supplicant");
    fr.write("run/wpa_supplicant/wlan0", "");
    fr.write("run/wpa_supplicant/wlan1", "");
    fr.mkdirs("sys/class/net/wlan0/wireless");
    fr.mkdirs("sys/class/net/wlan1/wireless");
    fr.write_cmdline(100, {"/usr/sbin/wpa_supplicant", "-i", "wlan0", "-c",
                           "/usr/prog/wifi/wpa_supplicant.conf"});

    // wlan1 answers, but is not associated. wlan0 is.
    auto probe = [](const std::string& sock) -> std::string {
        if (sock.find("wlan0") != std::string::npos)
            return "bssid=aa:bb:cc:dd:ee:ff\nwpa_state=COMPLETED\n";
        return "wpa_state=INACTIVE\n";
    };

    const auto iface = helix::wifi::resolve_interface(roots_for(fr), probe);

    REQUIRE(iface.has_value());
    CHECK(iface->netdev == "wlan0");
    CHECK(iface->conf_path == "/usr/prog/wifi/wpa_supplicant.conf");
    CHECK(iface->daemon_pid == 100);
}

TEST_CASE("resolve_interface reads the conf of the daemon it talks to", "[wifi][interface]") {
    // The AD5X reproduction: two daemons, different -c files. The old
    // first-match /proc walk could return either. Resolution must return the
    // one owning the socket we selected.
    FakeRoot fr;
    fr.mkdirs("run/wpa_supplicant");
    fr.write("run/wpa_supplicant/wlan0", "");
    fr.mkdirs("sys/class/net/wlan0/wireless");
    fr.mkdirs("sys/class/net/wlan1/wireless");
    // Lower PID, iterated first by a naive walk, but NOT our daemon.
    fr.write_cmdline(50, {"/usr/sbin/wpa_supplicant", "-i", "wlan1", "-c", "/etc/ap-mode.conf"});
    fr.write_cmdline(90, {"/usr/sbin/wpa_supplicant", "-i", "wlan0", "-c", "/data/station.conf"});

    auto probe = [](const std::string&) { return std::string("wpa_state=COMPLETED\n"); };
    const auto iface = helix::wifi::resolve_interface(roots_for(fr), probe);

    REQUIRE(iface.has_value());
    CHECK(iface->netdev == "wlan0");
    CHECK(iface->conf_path == "/data/station.conf"); // NOT /etc/ap-mode.conf
    CHECK(iface->daemon_pid == 90);
}

TEST_CASE("resolve_interface falls back when nothing is associated", "[wifi][interface]") {
    FakeRoot fr;
    fr.mkdirs("run/wpa_supplicant");
    fr.write("run/wpa_supplicant/wlan0", "");
    fr.mkdirs("sys/class/net/wlan0/wireless");
    fr.write_cmdline(100, {"/usr/sbin/wpa_supplicant", "-i", "wlan0", "-c", "/etc/wpa.conf"});

    auto probe = [](const std::string&) { return std::string("wpa_state=SCANNING\n"); };
    const auto iface = helix::wifi::resolve_interface(roots_for(fr), probe);

    REQUIRE(iface.has_value());
    CHECK(iface->netdev == "wlan0");
}

TEST_CASE("resolve_interface rejects a socket with no wireless netdev", "[wifi][interface]") {
    FakeRoot fr;
    fr.mkdirs("run/wpa_supplicant");
    fr.write("run/wpa_supplicant/p2p-dev-wlan0", "");
    // No sys/class/net/p2p-dev-wlan0/wireless — not a real station interface.
    auto probe = [](const std::string&) { return std::string("wpa_state=COMPLETED\n"); };

    const auto iface = helix::wifi::resolve_interface(roots_for(fr), probe);
    CHECK_FALSE(iface.has_value());
}

TEST_CASE("resolve_interface returns nullopt when nothing answers", "[wifi][interface]") {
    FakeRoot fr;
    fr.mkdirs("run/wpa_supplicant");
    fr.write("run/wpa_supplicant/wlan0", "");
    fr.mkdirs("sys/class/net/wlan0/wireless");
    auto probe = [](const std::string&) { return std::string(); };

    CHECK_FALSE(helix::wifi::resolve_interface(roots_for(fr), probe).has_value());
}

TEST_CASE("resolve_interface tolerates a missing conf path", "[wifi][interface]") {
    // A daemon launched with -C and no -c. Resolution still succeeds; conf_path
    // is empty and persistence degrades to the HelixScreen-owned store.
    FakeRoot fr;
    fr.mkdirs("run/wpa_supplicant");
    fr.write("run/wpa_supplicant/wlan0", "");
    fr.mkdirs("sys/class/net/wlan0/wireless");
    fr.write_cmdline(100, {"/usr/sbin/wpa_supplicant", "-i", "wlan0", "-C", "/run/wpa_supplicant"});

    auto probe = [](const std::string&) { return std::string("wpa_state=COMPLETED\n"); };
    const auto iface = helix::wifi::resolve_interface(roots_for(fr), probe);

    REQUIRE(iface.has_value());
    CHECK(iface->netdev == "wlan0");
    CHECK(iface->conf_path.empty());
}
