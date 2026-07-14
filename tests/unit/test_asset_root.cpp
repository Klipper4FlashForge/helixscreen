// SPDX-License-Identifier: GPL-3.0-or-later
#include "data_root_resolver.h"

#include "../catch_amalgamated.hpp"

TEST_CASE("asset_path is identity under the default root", "[paths][asset_root]") {
    helix::set_asset_root("");  // reset to default
    REQUIRE(helix::asset_root() == ".");
    REQUIRE(helix::asset_path("ui_xml") == "ui_xml");
    REQUIRE(helix::asset_path("assets/filaments.json") == "assets/filaments.json");
}

TEST_CASE("asset_path joins under an explicit root", "[paths][asset_root]") {
    helix::set_asset_root("/littlefs/");  // trailing slash must be stripped
    REQUIRE(helix::asset_root() == "/littlefs");
    REQUIRE(helix::asset_path("ui_xml") == "/littlefs/ui_xml");
    helix::set_asset_root("");  // restore for other tests
    REQUIRE(helix::asset_path("ui_xml") == "ui_xml");
}
