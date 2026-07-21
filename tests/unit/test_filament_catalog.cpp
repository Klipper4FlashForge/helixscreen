// SPDX-License-Identifier: GPL-3.0-or-later
#include "filament_catalog.h"
#include "helix_test_fixture.h"
#include "../catch_amalgamated.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>

#include "hv/json.hpp"

using helix::printer::FilamentCatalog;

namespace {
constexpr const char* FIX = "tests/fixtures/filaments_test.json";
constexpr const char* USER_FIX = "tests/fixtures/user_filaments_test.json";
}

TEST_CASE_METHOD(HelixTestFixture, "resolve_code cfs hit and miss", "[filament_catalog]") {
    auto cat = FilamentCatalog::load_from_file(FIX, /*codes_only=*/true, "cfs");
    const auto* p = cat.resolve_code("cfs", "01001");
    REQUIRE(p != nullptr);
    CHECK(p->brand == "Creality");
    CHECK(p->type == "PLA");
    CHECK(cat.resolve_code("cfs", "99999") == nullptr);
}

TEST_CASE_METHOD(HelixTestFixture, "effective inherits type range when thin", "[filament_catalog]") {
    auto cat = FilamentCatalog::load_from_file(FIX, false, "");
    const auto* pla = cat.resolve_id("polymaker-pla-pro");   // no explicit range
    REQUIRE(pla != nullptr);
    CHECK(pla->nozzle_min == 190);   // inherited from PLA type
    CHECK(pla->nozzle_max == 220);
    CHECK(pla->bed_temp == 60);      // inherited from PLA type
    CHECK(pla->compat_group == std::string("PLA"));
}

TEST_CASE_METHOD(HelixTestFixture, "explicit override wins over type", "[filament_catalog]") {
    auto cat = FilamentCatalog::load_from_file(FIX, false, "");
    const auto* abs = cat.resolve_id("polymaker-abs-pro");
    REQUIRE(abs != nullptr);
    CHECK(abs->nozzle_min == 270);   // explicit, outside generic ABS range
    CHECK(abs->nozzle_max == 290);
    CHECK(abs->bed_temp == 105);     // explicit bed
}

TEST_CASE_METHOD(HelixTestFixture, "load_codes materializes only coded slice", "[filament_catalog]") {
    auto cat = FilamentCatalog::load_from_file(FIX, true, "cfs");
    CHECK(cat.all_products().size() == 1);          // only the cfs-coded entry
    CHECK(cat.resolve_id("polymaker-abs-pro") == nullptr);
}

TEST_CASE_METHOD(HelixTestFixture, "queries by brand and type", "[filament_catalog]") {
    auto cat = FilamentCatalog::load_from_file(FIX, false, "");
    CHECK(cat.products_for_brand("Polymaker").size() == 2);
    CHECK(cat.products_for_type("PLA").size() == 2);
}

TEST_CASE_METHOD(HelixTestFixture, "user overlay overrides and adds", "[filament_catalog]") {
    auto cat = FilamentCatalog::load_with_overlay(
        "tests/fixtures/filaments_test.json", "tests/fixtures/user_filaments_test.json");
    const auto* abs = cat.resolve_id("polymaker-abs-pro");
    REQUIRE(abs != nullptr);
    CHECK(abs->nozzle_min == 265);   // overridden by user
    CHECK(abs->nozzle_max == 285);
    const auto* added = cat.resolve_id("acme-custom-petg");
    REQUIRE(added != nullptr);       // new user product
    CHECK(added->brand == "Acme");
    CHECK(added->bed_temp == 80);    // inherited from PETG type
}

TEST_CASE_METHOD(HelixTestFixture, "user overlay accepts legacy bare-array product form",
                 "[filament_catalog]") {
    // Backward-compat: a pre-#1120 overlay is a bare JSON array of products with
    // no orca_type_map. The reader must still merge those products — this is the
    // legacy read path #1120 explicitly preserves. Regression guard: the shared
    // fixture moved to object form, which left this branch otherwise uncovered.
    constexpr const char* TMP = "/tmp/helix_user_bare_array_products_test.json";
    {
        std::ofstream out(TMP);
        out << R"([{"id":"legacy-user-petg","brand":"Legacy","name":"Old PETG",)"
            << R"("type":"PETG","nozzle":230}])";
    }
    auto cat = FilamentCatalog::load_with_overlay("tests/fixtures/filaments_test.json", TMP);
    const auto* added = cat.resolve_id("legacy-user-petg");
    REQUIRE(added != nullptr);
    CHECK(added->brand == "Legacy");
    CHECK(added->nozzle_recommended == 230);
    std::remove(TMP);
}

TEST_CASE_METHOD(HelixTestFixture, "load_user_orca_type_map_from reads object form", "[filament_catalog]") {
    auto m = FilamentCatalog::load_user_orca_type_map_from(USER_FIX);
    REQUIRE(m.size() == 3);
    CHECK(m.at("PLA-BioTough") == "PLA");
    CHECK(m.at("CustomASA") == "ASA");
    // Empty value is the documented "suppress" case — must round-trip verbatim,
    // not be dropped or normalized to a default. orca_match_type() step 1 treats
    // "" as "emit nothing for this type".
    CHECK(m.at("WeirdResin") == "");
}

TEST_CASE_METHOD(HelixTestFixture, "load_user_orca_type_map_from missing file returns empty",
                 "[filament_catalog]") {
    CHECK(FilamentCatalog::load_user_orca_type_map_from("tests/fixtures/does_not_exist.json").empty());
}

TEST_CASE_METHOD(HelixTestFixture, "load_user_orca_type_map_from empty path returns empty",
                 "[filament_catalog]") {
    // SubjectInitializer passes the result of first_existing(), which is "" when
    // no user overlay is present on the device. Must not throw, must not log a
    // parse warning — just return empty silently.
    CHECK(FilamentCatalog::load_user_orca_type_map_from("").empty());
}

TEST_CASE_METHOD(HelixTestFixture, "load_user_orca_type_map_from bare array returns empty",
                 "[filament_catalog]") {
    // A bare-array overlay is product-only (the historical minimum) and must
    // not be treated as an error — it simply carries no Orca hints.
    constexpr const char* TMP = "/tmp/helix_user_bare_array_test.json";
    {
        std::ofstream out(TMP);
        out << R"([{"id":"x","type":"PLA"}])";
    }
    auto m = FilamentCatalog::load_user_orca_type_map_from(TMP);
    CHECK(m.empty());
    std::remove(TMP);
}

TEST_CASE_METHOD(HelixTestFixture, "load_user_orca_type_map_from object without key returns empty",
                 "[filament_catalog]") {
    // Object form but only filaments[] (the shape shipped Phase 1 expected) —
    // valid overlay, just no Orca contribution. Not an error.
    constexpr const char* TMP = "/tmp/helix_user_no_key_test.json";
    {
        std::ofstream out(TMP);
        out << R"({"filaments":[{"id":"x","type":"PLA"}]})";
    }
    auto m = FilamentCatalog::load_user_orca_type_map_from(TMP);
    CHECK(m.empty());
    std::remove(TMP);
}

// ---- save_user_products_to ----

namespace {
// Helper: read a small file into a string (empty string on failure).
std::string read_small_file(const std::string& path) {
    std::ifstream in(path);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

constexpr const char* SAVE_TMP = "/tmp/helix_user_save_test.json";

void remove_save_tmp() { std::remove(SAVE_TMP); }
}  // namespace

TEST_CASE_METHOD(HelixTestFixture, "save_user_products_to round-trips through load_with_overlay",
                 "[filament_catalog][user_save]") {
    remove_save_tmp();
    std::vector<nlohmann::json> products = {
        {{"id", "acme-test-pla"}, {"brand", "Acme"}, {"name", "Test PLA"},
         {"type", "PLA"}, {"nozzle", 220}, {"source", "user"}},
        {{"id", "brand-x-abs"}, {"brand", "Brand X"}, {"name", "Fast ABS"},
         {"type", "ABS"}, {"nozzle_min", 240}, {"nozzle_max", 260}, {"source", "user"}},
    };

    REQUIRE(FilamentCatalog::save_user_products_to(products, SAVE_TMP));

    // File exists, is parseable, carries the filaments section.
    const std::string raw = read_small_file(SAVE_TMP);
    INFO("overlay content: " << raw);
    auto doc = nlohmann::json::parse(raw);
    REQUIRE(doc.is_object());
    REQUIRE(doc.contains("filaments"));
    REQUIRE(doc["filaments"].is_array());
    REQUIRE(doc["filaments"].size() == 2);
    CHECK(doc["filaments"][0]["id"] == "acme-test-pla");
    CHECK(doc["filaments"][1]["nozzle_max"] == 260);

    // Functional round-trip: load_with_overlay against the built-in fixture
    // merges the new entries over the existing ones.
    auto cat = FilamentCatalog::load_with_overlay(FIX, SAVE_TMP);
    const auto* added = cat.resolve_id("acme-test-pla");
    REQUIRE(added != nullptr);
    CHECK(added->brand == "Acme");
    CHECK(added->nozzle_recommended == 220);  // "nozzle" key

    remove_save_tmp();
}

TEST_CASE_METHOD(HelixTestFixture, "save_user_products_to preserves existing orca_type_map",
                 "[filament_catalog][user_save]") {
    remove_save_tmp();
    // Seed the file with both sections.
    {
        std::ofstream out(SAVE_TMP);
        out << R"({"filaments":[{"id":"old-entry","type":"PLA"}],)"
            << R"("orca_type_map":{"MyResin":"PLA","WeirdResin":""}})";
    }

    std::vector<nlohmann::json> fresh_products = {
        {{"id", "new-entry"}, {"type", "PETG"}, {"source", "user"}},
    };
    REQUIRE(FilamentCatalog::save_user_products_to(fresh_products, SAVE_TMP));

    // Products were replaced (old-entry is gone, new-entry is present).
    auto doc = nlohmann::json::parse(read_small_file(SAVE_TMP));
    REQUIRE(doc["filaments"].size() == 1);
    CHECK(doc["filaments"][0]["id"] == "new-entry");

    // orca_type_map is preserved verbatim, including the "" suppress entry.
    auto m = FilamentCatalog::load_user_orca_type_map_from(SAVE_TMP);
    REQUIRE(m.size() == 2);
    CHECK(m.at("MyResin") == "PLA");
    CHECK(m.at("WeirdResin") == "");

    remove_save_tmp();
}

TEST_CASE_METHOD(HelixTestFixture, "save_user_products_to migrates legacy bare-array overlay",
                 "[filament_catalog][user_save]") {
    remove_save_tmp();
    // Seed with the legacy bare-array form (pre-#1120). Migration: keep the
    // products we're given (the caller is replacing them anyway), drop the
    // bare-array shape, write proper object form. The legacy form never
    // carried orca_type_map, so nothing is lost.
    {
        std::ofstream out(SAVE_TMP);
        out << R"([{"id":"legacy-entry","type":"PLA"}])";
    }

    std::vector<nlohmann::json> products = {
        {{"id", "post-migration"}, {"type", "PETG"}, {"source", "user"}},
    };
    REQUIRE(FilamentCatalog::save_user_products_to(products, SAVE_TMP));

    auto doc = nlohmann::json::parse(read_small_file(SAVE_TMP));
    REQUIRE(doc.is_object());  // migrated to object form
    REQUIRE(doc["filaments"].size() == 1);
    CHECK(doc["filaments"][0]["id"] == "post-migration");
    // Reader accepts the new shape.
    auto cat = FilamentCatalog::load_with_overlay(FIX, SAVE_TMP);
    CHECK(cat.resolve_id("post-migration") != nullptr);

    remove_save_tmp();
}

TEST_CASE_METHOD(HelixTestFixture, "save_user_products_to creates missing parent directories",
                 "[filament_catalog][user_save]") {
    const std::string nested = "/tmp/helix_save_nested_dir_test/overlay.json";
    std::remove(nested.c_str());
    std::filesystem::remove_all("/tmp/helix_save_nested_dir_test");

    std::vector<nlohmann::json> products = {
        {{"id", "nested-test"}, {"type", "PLA"}, {"source", "user"}},
    };
    REQUIRE(FilamentCatalog::save_user_products_to(products, nested));
    CHECK(std::filesystem::exists(nested));

    std::filesystem::remove_all("/tmp/helix_save_nested_dir_test");
}

TEST_CASE_METHOD(HelixTestFixture, "save_user_products_to handles corrupt existing file",
                 "[filament_catalog][user_save]") {
    remove_save_tmp();
    {
        std::ofstream out(SAVE_TMP);
        out << R"(this is not json)";
    }

    std::vector<nlohmann::json> products = {
        {{"id", "post-corrupt"}, {"type", "PLA"}, {"source", "user"}},
    };
    // Corrupt existing file must not block the save — start fresh, log a warn.
    REQUIRE(FilamentCatalog::save_user_products_to(products, SAVE_TMP));

    auto doc = nlohmann::json::parse(read_small_file(SAVE_TMP));
    REQUIRE(doc["filaments"].size() == 1);
    CHECK(doc["filaments"][0]["id"] == "post-corrupt");

    remove_save_tmp();
}

TEST_CASE_METHOD(HelixTestFixture, "save_user_products_to backs up a corrupt existing file",
                 "[filament_catalog][user_save]") {
    remove_save_tmp();
    const std::string bak = std::string(SAVE_TMP) + ".bak";
    std::remove(bak.c_str());
    // A hand-authored file that's been truncated mid-edit: the orca_type_map is
    // real data the user would want back, but the file no longer parses.
    {
        std::ofstream out(SAVE_TMP);
        out << R"({"orca_type_map":{"PreciousHint":"PLA"}, "filaments":[)";  // truncated
    }

    std::vector<nlohmann::json> products = {
        {{"id", "post-corrupt"}, {"type", "PLA"}, {"source", "user"}},
    };
    REQUIRE(FilamentCatalog::save_user_products_to(products, SAVE_TMP));

    // The unparseable original is preserved to <path>.bak for hand-recovery,
    // with its content intact.
    REQUIRE(std::filesystem::exists(bak));
    const std::string recovered = read_small_file(bak);
    CHECK(recovered.find("PreciousHint") != std::string::npos);

    std::remove(bak.c_str());
    remove_save_tmp();
}

TEST_CASE_METHOD(HelixTestFixture, "choose_overlay_write_path falls back to canonical path",
                 "[filament_catalog][user_save]") {
    // Fresh install: neither candidate exists on disk. The write target must
    // still resolve to a creatable path (the first candidate), not "" —
    // otherwise the first save from the edit modal has nowhere to write.
    const char* none[] = {"/tmp/helix_choose_path_a.json", "/tmp/helix_choose_path_b.json"};
    std::remove(none[0]);
    std::remove(none[1]);
    CHECK(FilamentCatalog::choose_overlay_write_path(none, 2) == std::string(none[0]));

    // When a later candidate already exists, it is preferred over the fallback.
    {
        std::ofstream out(none[1]);
        out << "[]";
    }
    CHECK(FilamentCatalog::choose_overlay_write_path(none, 2) == std::string(none[1]));
    std::remove(none[1]);
}

TEST_CASE_METHOD(HelixTestFixture, "save_user_products_to empty path returns false",
                 "[filament_catalog][user_save]") {
    // SubjectInitializer passes the result of first_existing(), which is "" when
    // no overlay directory exists yet. Must not throw, must not create a file
    // named "" — just return false cleanly.
    CHECK_FALSE(FilamentCatalog::save_user_products_to({}, ""));
}

TEST_CASE_METHOD(HelixTestFixture, "save_user_products_to empty product list writes valid overlay",
                 "[filament_catalog][user_save]") {
    remove_save_tmp();
    // Seed with orca_type_map so we can verify it survives an empty-products save.
    {
        std::ofstream out(SAVE_TMP);
        out << R"({"filaments":[{"id":"will-be-replaced"}],"orca_type_map":{"X":"PLA"}})";
    }

    REQUIRE(FilamentCatalog::save_user_products_to({}, SAVE_TMP));

    auto doc = nlohmann::json::parse(read_small_file(SAVE_TMP));
    REQUIRE(doc["filaments"].is_array());
    CHECK(doc["filaments"].empty());
    // orca_type_map preserved.
    CHECK(doc["orca_type_map"]["X"] == "PLA");

    remove_save_tmp();
}
