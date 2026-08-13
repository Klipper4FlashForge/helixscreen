// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#include "filament_catalog.h"
#include "helix_test_fixture.h"

#include <algorithm>

#include "../catch_amalgamated.hpp"

using helix::printer::EffectiveFilament;
using helix::printer::FilamentCatalog;

namespace {
bool contains(const std::vector<std::string>& v, const std::string& s) {
    return std::find(v.begin(), v.end(), s) != v.end();
}
} // namespace

TEST_CASE_METHOD(HelixTestFixture, "all_brands is deduped and sorted", "[filament_catalog]") {
    auto cat = FilamentCatalog::load_full();
    auto brands = cat.all_brands();
    REQUIRE(!brands.empty());
    // deduped
    auto sorted = brands;
    std::sort(sorted.begin(), sorted.end());
    REQUIRE(std::unique(sorted.begin(), sorted.end()) == sorted.end());
    // "Generic" is present (seed products preserve it)
    REQUIRE(contains(brands, "Generic"));
}

TEST_CASE_METHOD(HelixTestFixture, "types_for_brand returns that brand's types only",
                 "[filament_catalog]") {
    auto cat = FilamentCatalog::load_full();
    auto types = cat.types_for_brand("Generic");
    REQUIRE(!types.empty());
    REQUIRE(contains(types, "PLA"));
    // every returned type actually has a Generic product
    for (const auto& t : types) {
        REQUIRE(!cat.products_for("Generic", t).empty());
    }
}

TEST_CASE_METHOD(HelixTestFixture, "products_for filters by brand AND type", "[filament_catalog]") {
    auto cat = FilamentCatalog::load_full();
    auto prods = cat.products_for("Generic", "PLA");
    REQUIRE(!prods.empty());
    for (const auto* p : prods) {
        REQUIRE(p->brand == "Generic");
        REQUIRE(p->type == "PLA");
    }
}

TEST_CASE_METHOD(HelixTestFixture, "brands_for_type only lists carriers", "[filament_catalog]") {
    auto cat = FilamentCatalog::load_full();
    auto brands = cat.brands_for_type("PLA");
    REQUIRE(contains(brands, "Generic"));
    for (const auto& b : brands) {
        REQUIRE(!cat.products_for(b, "PLA").empty());
    }
}

TEST_CASE_METHOD(HelixTestFixture, "SILK has a selectable Generic product (AD5X whitelist seed)",
                 "[filament_catalog]") {
    // AD5X stock firmware's material whitelist includes "SILK" as a type distinct from
    // "PLA" (see get_supported_materials() in ams_backend_ad5x_ifs.cpp), but Orca has no
    // profile for it — silk PLA is just a display name over Orca's plain "PLA" type. A
    // scripts/fixtures/cfs_seed.json entry seeds a standalone "Generic"/"SILK" product so
    // the type dropdown's whitelist-appended SILK row has something to select. This test
    // pins that product against the shipped assets/filaments.json (loaded via
    // FilamentCatalog::load_full(), same as the real UI) so a future `make regen-filaments`
    // that silently drops the seed entry fails here instead of shipping a dead-end dropdown
    // row.
    auto cat = FilamentCatalog::load_full();
    auto prods = cat.products_for("Generic", "SILK");
    REQUIRE(!prods.empty());
    for (const auto* p : prods) {
        REQUIRE(p->brand == "Generic");
        REQUIRE(p->type == "SILK");
        REQUIRE(p->nozzle_min > 0);
        REQUIRE(p->nozzle_max > 0);
        REQUIRE(p->nozzle_min <= p->nozzle_recommended);
        REQUIRE(p->nozzle_recommended <= p->nozzle_max);
        REQUIRE(p->bed_temp > 0);
    }
}
