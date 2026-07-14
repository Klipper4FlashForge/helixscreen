// SPDX-License-Identifier: GPL-3.0-or-later
#include "config.h"
#include "config_storage.h"
#include "../test_helpers/mock_config_storage.h"

#include "../catch_amalgamated.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

TEST_CASE("file storage round-trips a document atomically", "[config][storage]") {
    fs::path dir = fs::temp_directory_path() / "helix-storage-test";
    fs::create_directories(dir);
    std::string path = (dir / "settings.json").string();
    auto storage = helix::make_file_config_storage(path);

    REQUIRE_FALSE(storage->load().has_value());  // missing = nullopt
    REQUIRE(storage->store("{\"config_version\": 19}\n"));
    auto doc = storage->load();
    REQUIRE(doc.has_value());
    REQUIRE(doc->find("config_version") != std::string::npos);
    REQUIRE_FALSE(fs::exists(path + ".tmp"));  // no temp litter after store

    storage->preserve_corrupt();
    REQUIRE_FALSE(storage->load().has_value());
    REQUIRE(fs::exists(path + ".corrupt"));

    fs::remove_all(dir);
}

TEST_CASE("Config routes load and save through an injected backend",
          "[config][storage]") {
    auto mock = std::make_unique<helix::test::MockConfigStorage>(
        std::string(R"({"config_version": 19, "wizard_completed": true})"));
    auto* mock_raw = mock.get();

    helix::Config cfg;
    cfg.set_storage(std::move(mock));
    cfg.init("config/settings-test.json");

    REQUIRE(cfg.get<bool>("/wizard_completed", false) == true);

    cfg.set<int>("/test_marker", 42);
    REQUIRE(cfg.save());
    REQUIRE(mock_raw->store_calls >= 1);
    REQUIRE(mock_raw->doc.has_value());
    REQUIRE(mock_raw->doc->find("test_marker") != std::string::npos);
}
