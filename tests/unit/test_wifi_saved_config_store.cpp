// Copyright (C) 2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "data_root_resolver.h"
#include "wifi_saved_config.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>

#include "../catch_amalgamated.hpp"

/**
 * A HelixScreen-owned credential store, independent of wpa_supplicant's own
 * config file.
 *
 * A real Adventurer 5M logged a SAVE_CONFIG that replied "OK" and still never
 * reached wpa_supplicant's config file — with no symlink in sight for
 * mirror_to_persistent() to copy onto. We cannot tell, without the device in
 * hand, whether the wrong daemon's config was verified or the write landed
 * somewhere non-persistent. This store sidesteps the question: every
 * successful connect is recorded here too, so a startup reconciliation pass
 * can restore it into wpa_supplicant regardless of what happened to the
 * vendor's own file.
 *
 * These tests pin: round-trip, dedup-by-SSID on save, removal, exact file
 * mode, graceful handling of a missing/corrupt file, and that SSIDs
 * containing quote/backslash characters (rejected by wpa_supplicant's own
 * command protocol, but legal as SSIDs) survive the JSON round trip intact.
 */

using helix::wifi::store::SavedNetwork;

namespace {

/// Point HELIX_CONFIG_DIR at an isolated temp directory for the duration of a
/// test, restoring whatever was there before on scope exit. Mirrors the
/// pattern used throughout tests/unit/ (e.g. test_theme_loader.cpp,
/// test_config_preset.cpp) for isolating get_user_config_dir().
class ConfigDirGuard {
  public:
    explicit ConfigDirGuard(const std::string& dir) {
        if (const char* prev = std::getenv("HELIX_CONFIG_DIR")) {
            had_prev_ = true;
            prev_ = prev;
        }
        setenv("HELIX_CONFIG_DIR", dir.c_str(), 1);
    }

    ~ConfigDirGuard() {
        if (had_prev_)
            setenv("HELIX_CONFIG_DIR", prev_.c_str(), 1);
        else
            unsetenv("HELIX_CONFIG_DIR");
    }

    ConfigDirGuard(const ConfigDirGuard&) = delete;
    ConfigDirGuard& operator=(const ConfigDirGuard&) = delete;

  private:
    bool had_prev_ = false;
    std::string prev_;
};

/// Fresh, empty temp dir per test so runs never see a previous test's store.
std::string make_temp_dir(const std::string& name) {
    const std::string dir = "/tmp/" + name;
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return dir;
}

} // namespace

TEST_CASE("A saved network round-trips through the store", "[network][wifi][savedconfig][store]") {
    ConfigDirGuard guard(make_temp_dir("helix_store_roundtrip"));

    CHECK(helix::wifi::store::load().empty());

    REQUIRE(helix::wifi::store::save({"MyHomeNet", "supersecretpsk"}));

    auto nets = helix::wifi::store::load();
    REQUIRE(nets.size() == 1);
    CHECK(nets[0].ssid == "MyHomeNet");
    CHECK(nets[0].psk == "supersecretpsk");
}

TEST_CASE("Saving an existing SSID replaces rather than duplicates",
          "[network][wifi][savedconfig][store]") {
    ConfigDirGuard guard(make_temp_dir("helix_store_dedup"));

    REQUIRE(helix::wifi::store::save({"MyHomeNet", "oldpassword"}));
    REQUIRE(helix::wifi::store::save({"MyHomeNet", "newpassword"}));
    REQUIRE(helix::wifi::store::save({"OtherNet", "otherpass"}));

    auto nets = helix::wifi::store::load();
    REQUIRE(nets.size() == 2);

    bool found_home = false;
    for (const auto& n : nets) {
        if (n.ssid == "MyHomeNet") {
            found_home = true;
            CHECK(n.psk == "newpassword"); // replaced, not appended
        }
    }
    CHECK(found_home);
}

TEST_CASE("Removing a network deletes it from the store", "[network][wifi][savedconfig][store]") {
    ConfigDirGuard guard(make_temp_dir("helix_store_remove"));

    REQUIRE(helix::wifi::store::save({"NetA", "pskA"}));
    REQUIRE(helix::wifi::store::save({"NetB", "pskB"}));

    REQUIRE(helix::wifi::store::remove("NetA"));

    auto nets = helix::wifi::store::load();
    REQUIRE(nets.size() == 1);
    CHECK(nets[0].ssid == "NetB");

    // Removing an SSID that was never present is not an error — the store
    // already reflects the desired end state.
    CHECK(helix::wifi::store::remove("NeverSaved"));
    CHECK(helix::wifi::store::load().size() == 1);
}

TEST_CASE("The store file mode is exactly 0600", "[network][wifi][savedconfig][store]") {
    ConfigDirGuard guard(make_temp_dir("helix_store_mode"));

    REQUIRE(helix::wifi::store::save({"NetA", "pskA"}));

    const std::string path = helix::wifi::store::store_path();
    struct stat st {};
    REQUIRE(::stat(path.c_str(), &st) == 0);

    // Assert the ACTUAL mode bits, not just that save() ran. 0600 = rw for
    // the owner, nothing for group or other — this file holds every PSK on
    // the device.
    CHECK((st.st_mode & 07777) == 0600);
}

TEST_CASE("load() on a missing file returns empty, not an exception",
          "[network][wifi][savedconfig][store]") {
    ConfigDirGuard guard(make_temp_dir("helix_store_missing"));

    // Nothing has been saved yet — the file does not exist at all.
    CHECK(helix::wifi::store::load().empty());
}

TEST_CASE("load() on a corrupt file returns empty, not an exception",
          "[network][wifi][savedconfig][store]") {
    const std::string dir = make_temp_dir("helix_store_corrupt");
    ConfigDirGuard guard(dir);

    const std::string path = helix::wifi::store::store_path();
    {
        std::ofstream out(path);
        out << "{ this is not valid json [[[";
    }

    CHECK(helix::wifi::store::load().empty());

    // A corrupt file also must not crash a subsequent save().
    CHECK(helix::wifi::store::save({"NetA", "pskA"}));
    auto nets = helix::wifi::store::load();
    REQUIRE(nets.size() == 1);
    CHECK(nets[0].ssid == "NetA");
}

TEST_CASE("load() on a JSON file that is not an array returns empty",
          "[network][wifi][savedconfig][store]") {
    const std::string dir = make_temp_dir("helix_store_wrong_shape");
    ConfigDirGuard guard(dir);

    const std::string path = helix::wifi::store::store_path();
    {
        std::ofstream out(path);
        out << R"({"ssid": "NotAnArray"})";
    }

    CHECK(helix::wifi::store::load().empty());
}

TEST_CASE("An SSID containing a quote or backslash round-trips intact",
          "[network][wifi][savedconfig][store]") {
    // wpa_supplicant's own SET_NETWORK protocol rejects these characters (see
    // validate_wpa_string() in wifi_backend_wpa_supplicant.cpp) because they
    // would break out of the quoted command string. That restriction belongs
    // to the wire protocol, not to the store: a network legitimately named
    // `Joe's "Cafe"` or `back\slash` must still survive being written to and
    // read back from our own JSON file untouched.
    ConfigDirGuard guard(make_temp_dir("helix_store_escaping"));

    const std::string quoted_ssid = R"(Joe's "Cafe" WiFi)";
    const std::string backslash_ssid = R"(back\slash\net)";
    const std::string tricky_psk = R"(p"a\s\sw"ord)";

    REQUIRE(helix::wifi::store::save({quoted_ssid, tricky_psk}));
    REQUIRE(helix::wifi::store::save({backslash_ssid, "plainpass"}));

    auto nets = helix::wifi::store::load();
    REQUIRE(nets.size() == 2);

    bool found_quoted = false, found_backslash = false;
    for (const auto& n : nets) {
        if (n.ssid == quoted_ssid) {
            found_quoted = true;
            CHECK(n.psk == tricky_psk);
        }
        if (n.ssid == backslash_ssid) {
            found_backslash = true;
            CHECK(n.psk == "plainpass");
        }
    }
    CHECK(found_quoted);
    CHECK(found_backslash);
}

TEST_CASE("The store path lives under the user config dir", "[network][wifi][savedconfig][store]") {
    const std::string dir = make_temp_dir("helix_store_path");
    ConfigDirGuard guard(dir);

    CHECK(helix::wifi::store::store_path() == dir + "/wifi_networks.json");
}
