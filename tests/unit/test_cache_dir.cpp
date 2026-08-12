// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_cache_dir.cpp
 * @brief Unit tests for get_helix_cache_dir() resolution chain
 *
 * Tests the 7-step cache directory resolution: HELIX_CACHE_DIR env,
 * config, platform, XDG, HOME, /var/tmp, /tmp fallbacks.
 */

#include "app_globals.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>

#include "../catch_amalgamated.hpp"

// Helper: create a unique temp directory for test isolation
static std::string make_test_tmpdir(const std::string& label) {
    std::string path = std::string("/tmp/helix_test_cache_") + label + "_" +
                       std::to_string(static_cast<unsigned long>(time(nullptr)));
    std::filesystem::create_directories(path);
    return path;
}

// Helper: clean up a directory tree
static void cleanup_dir(const std::string& path) {
    std::error_code ec;
    std::filesystem::remove_all(path, ec);
}

// RAII guard for env vars - restores original value on destruction
struct EnvGuard {
    std::string name;
    std::string original;
    bool was_set;

    explicit EnvGuard(const char* env_name) : name(env_name) {
        const char* val = std::getenv(env_name);
        was_set = (val != nullptr);
        if (was_set)
            original = val;
    }

    ~EnvGuard() {
        if (was_set) {
            setenv(name.c_str(), original.c_str(), 1);
        } else {
            unsetenv(name.c_str());
        }
    }
};

// ============================================================================
// get_helix_cache_dir() Tests
// ============================================================================

TEST_CASE("get_helix_cache_dir HELIX_CACHE_DIR override", "[cache]") {
    EnvGuard guard("HELIX_CACHE_DIR");
    std::string tmpdir = make_test_tmpdir("env_override");

    SECTION("Uses HELIX_CACHE_DIR when set") {
        setenv("HELIX_CACHE_DIR", tmpdir.c_str(), 1);

        std::string result = get_helix_cache_dir("test_sub");

        REQUIRE(result.find(tmpdir) == 0);
        REQUIRE(result.find("test_sub") != std::string::npos);
        REQUIRE(std::filesystem::exists(result));
    }

    SECTION("Creates subdirectory inside HELIX_CACHE_DIR") {
        setenv("HELIX_CACHE_DIR", tmpdir.c_str(), 1);

        std::string result = get_helix_cache_dir("my_subdir");

        std::string expected = tmpdir + "/my_subdir";
        REQUIRE(result == expected);
        REQUIRE(std::filesystem::is_directory(result));
    }

    cleanup_dir(tmpdir);
}

TEST_CASE("get_helix_cache_dir falls through on empty env", "[cache]") {
    EnvGuard helix_guard("HELIX_CACHE_DIR");
    unsetenv("HELIX_CACHE_DIR");

    std::string result = get_helix_cache_dir("fallthrough_test");

    // Should still resolve to something valid (XDG, HOME, /var/tmp, or /tmp)
    REQUIRE(!result.empty());
    REQUIRE(std::filesystem::exists(result));

    cleanup_dir(result);
}

TEST_CASE("get_helix_cache_dir falls through on invalid env path", "[cache]") {
    EnvGuard guard("HELIX_CACHE_DIR");
    // Set to a path that can't be created (nested under /nonexistent)
    setenv("HELIX_CACHE_DIR", "/nonexistent/readonly/cache", 1);

    std::string result = get_helix_cache_dir("invalid_test");

    // Should gracefully fall through to a working directory
    REQUIRE(!result.empty());
    REQUIRE(std::filesystem::exists(result));

    cleanup_dir(result);
}

TEST_CASE("get_helix_cache_dir result is writable", "[cache]") {
    std::string result = get_helix_cache_dir("writable_test");
    REQUIRE(!result.empty());

    // Verify we can actually write a file there
    std::string test_file = result + "/.write_test";
    {
        std::ofstream ofs(test_file);
        REQUIRE(ofs.good());
        ofs << "test";
    }

    REQUIRE(std::filesystem::exists(test_file));
    std::filesystem::remove(test_file);
    cleanup_dir(result);
}

TEST_CASE("get_helix_cache_dir different subdirs get different paths", "[cache]") {
    std::string dir_a = get_helix_cache_dir("subdir_alpha");
    std::string dir_b = get_helix_cache_dir("subdir_beta");

    REQUIRE(dir_a != dir_b);
    REQUIRE(dir_a.find("subdir_alpha") != std::string::npos);
    REQUIRE(dir_b.find("subdir_beta") != std::string::npos);

    cleanup_dir(dir_a);
    cleanup_dir(dir_b);
}
