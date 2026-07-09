// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_helix_paths.cpp
 * @brief Unit tests for helix::paths filesystem-path primitives.
 *
 * These primitives centralize behavior currently reimplemented across
 * update_checker.cpp, log_path_probe.cpp, input_shaper_cache.cpp,
 * thumbnail_cache.cpp, app_globals.cpp, app_constants.h, logging_init.cpp,
 * and data_root_resolver.cpp. Written TDD-style: the cases below encode the
 * exact edge-case semantics converged from those sources.
 */

#include "system/helix_paths.h"

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

#include "../catch_amalgamated.hpp"

namespace fs = std::filesystem;
using namespace helix::paths;

namespace {

// RAII save/restore of an environment variable so tests don't leak state.
class EnvVarGuard {
  public:
    explicit EnvVarGuard(const char* name) : name_(name) {
        const char* v = std::getenv(name);
        had_ = v != nullptr;
        if (had_) {
            saved_ = v;
        }
    }
    ~EnvVarGuard() {
        if (had_) {
            ::setenv(name_.c_str(), saved_.c_str(), 1);
        } else {
            ::unsetenv(name_.c_str());
        }
    }
    void set(const std::string& value) { ::setenv(name_.c_str(), value.c_str(), 1); }
    void unset() { ::unsetenv(name_.c_str()); }

  private:
    std::string name_;
    bool had_ = false;
    std::string saved_;
};

// Create a unique, writable temp directory for the duration of a test.
fs::path make_tmp_dir(const std::string& tag) {
    fs::path base =
        fs::temp_directory_path() / ("helix_paths_" + tag + "_" + std::to_string(::getpid()));
    fs::remove_all(base);
    fs::create_directories(base);
    return base;
}

} // namespace

TEST_CASE("dirname edge cases", "[helix_paths]") {
    CHECK(dirname("/a/b/c.tar.gz") == "/a/b");
    CHECK(dirname("/a/b/") == "/a");
    CHECK(dirname("file") == ".");
    CHECK(dirname("/file") == "/");
    CHECK(dirname("/") == "/");
    CHECK(dirname("") == ".");
}

TEST_CASE("strip_trailing_slash edge cases", "[helix_paths]") {
    CHECK(strip_trailing_slash("/a/b/") == "/a/b");
    CHECK(strip_trailing_slash("/") == "/");
    CHECK(strip_trailing_slash("/a///") == "/a");
    CHECK(strip_trailing_slash("") == "");
    CHECK(strip_trailing_slash("/a/b") == "/a/b"); // no trailing slash: unchanged
}

TEST_CASE("home honors absolute HOME only", "[helix_paths]") {
    EnvVarGuard home_guard("HOME");

    home_guard.set("/home/tester");
    CHECK(home() == "/home/tester");

    home_guard.set("");
    CHECK(home() == "");

    home_guard.set("relative/path");
    CHECK(home() == "");

    home_guard.unset();
    CHECK(home() == "");
}

TEST_CASE("xdg_cache_bases ordered candidates", "[helix_paths]") {
    EnvVarGuard xdg_guard("XDG_CACHE_HOME");
    EnvVarGuard home_guard("HOME");

    // XDG set + HOME set -> both candidates, XDG first (the fallback chain).
    xdg_guard.set("/xdg/cache");
    home_guard.set("/home/tester");
    CHECK(xdg_cache_bases() == std::vector<std::string>{"/xdg/cache", "/home/tester/.cache"});

    // XDG unset -> only HOME/.cache.
    xdg_guard.unset();
    CHECK(xdg_cache_bases() == std::vector<std::string>{"/home/tester/.cache"});

    // Neither -> empty.
    home_guard.unset();
    CHECK(xdg_cache_bases().empty());
}

TEST_CASE("xdg_data_home chain", "[helix_paths]") {
    EnvVarGuard xdg_guard("XDG_DATA_HOME");
    EnvVarGuard home_guard("HOME");

    // XDG set wins.
    xdg_guard.set("/xdg/data");
    home_guard.set("/home/tester");
    CHECK(xdg_data_home() == "/xdg/data");

    // XDG unset -> HOME/.local/share.
    xdg_guard.unset();
    CHECK(xdg_data_home() == "/home/tester/.local/share");

    // Neither -> "" (no /tmp hard-fallback here).
    home_guard.unset();
    CHECK(xdg_data_home() == "");
}

TEST_CASE("available_space on real vs bogus dir", "[helix_paths]") {
    fs::path dir = make_tmp_dir("avail");
    CHECK(available_space(dir.string()) > 0);
    CHECK(available_space("/no/such/path/really/unlikely/xyz") == 0);
    fs::remove_all(dir);
}

TEST_CASE("is_writable_dir true/false", "[helix_paths]") {
    fs::path dir = make_tmp_dir("writable");
    CHECK(is_writable_dir(dir.string()));
    CHECK_FALSE(is_writable_dir("/no/such/path/really/unlikely/xyz"));
    CHECK_FALSE(is_writable_dir(""));

    // chmod-000 dir is unwritable, but root ignores permission bits.
    if (::geteuid() != 0) {
        fs::path locked = dir / "locked";
        fs::create_directories(locked);
        fs::permissions(locked, fs::perms::none);
        CHECK_FALSE(is_writable_dir(locked.string()));
        fs::permissions(locked, fs::perms::owner_all); // restore so remove_all works
    }
    fs::remove_all(dir);
}

TEST_CASE("ensure_dir creates nested, idempotent", "[helix_paths]") {
    fs::path base = make_tmp_dir("ensure");
    fs::path nested = base / "a" / "b" / "c";

    CHECK(ensure_dir(nested.string()));
    CHECK(fs::exists(nested));
    CHECK(fs::is_directory(nested));

    // Second call is idempotent.
    CHECK(ensure_dir(nested.string()));

    fs::remove_all(base);
}

TEST_CASE("probe_writable success leaves no residue", "[helix_paths]") {
    fs::path dir = make_tmp_dir("probe");

    CHECK(probe_writable(dir.string(), 0));

    // No leftover test file.
    bool leftover = false;
    for (const auto& e : fs::directory_iterator(dir)) {
        (void)e;
        leftover = true;
    }
    CHECK_FALSE(leftover);

    fs::remove_all(dir);
}

TEST_CASE("probe_writable fails on nonexistent dir", "[helix_paths]") {
    CHECK_FALSE(probe_writable("/no/such/path/really/unlikely/xyz", 0));
}

TEST_CASE("probe_writable fails when space short", "[helix_paths]") {
    fs::path dir = make_tmp_dir("probe_space");
    CHECK_FALSE(probe_writable(dir.string(), UINT64_MAX));
    fs::remove_all(dir);
}
