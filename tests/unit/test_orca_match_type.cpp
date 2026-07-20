// SPDX-License-Identifier: GPL-3.0-or-later
//
// Resolution from HelixScreen's display type to a string OrcaSlicer can match.
//
// Why this matters: Orca resolves an unmatched filament_type to the first
// library preset whose name contains "PLA" (Preset.cpp:3300), and that bogus id
// then resolves successfully in sync_ams_list, short-circuiting the smarter
// similarity search. So an unmatchable string does not degrade gracefully —
// it becomes PLA temperatures on an ASA-GF spool.

#include "filament_variants.h"

#include <map>
#include <set>
#include <string>

#include "../catch_amalgamated.hpp"

namespace {

// Mirrors the shipped orca_library_types for deterministic tests.
const std::set<std::string> kLib = {
    "ABS", "ABS-GF", "ASA", "ASA-AERO", "ASA-CF", "BVOH", "CoPE", "EVA",
    "HIPS", "PA", "PA-CF", "PA-GF", "PA6-CF", "PC", "PCTG", "PE", "PET-CF",
    "PETG", "PETG-CF", "PHA", "PLA", "PLA-AERO", "PLA-CF", "PP", "PP-CF",
    "PP-GF", "PPA-CF", "PPA-GF", "PVA", "SBS", "TPU"};

const std::map<std::string, std::string> kOverrides = {
    {"rPLA", "PLA"},   {"rPETG", "PETG"}, {"TPE", "TPU"},  {"TPU-95A", "TPU"},
    {"TPU-85A", "TPU"}, {"SILK", "PLA"},  {"Color-Change", "PLA"},
    {"PLA+", "PLA"},   {"ASA+", "ASA"},   {"ABS+", "ABS"}};

struct TableFixture {
    TableFixture() { filament::set_orca_tables(kLib, kOverrides); }
    ~TableFixture() { filament::set_orca_tables({}, {}); }
};

} // namespace

TEST_CASE_METHOD(TableFixture, "orca_match_type passes library types through", "[orca_match]") {
    CHECK(filament::orca_match_type("PLA") == "PLA");
    CHECK(filament::orca_match_type("PLA-CF") == "PLA-CF");
    CHECK(filament::orca_match_type("ASA-CF") == "ASA-CF");
    CHECK(filament::orca_match_type("TPU") == "TPU");
    CHECK(filament::orca_match_type("ABS") == "ABS");
}

TEST_CASE_METHOD(TableFixture, "orca_match_type degrades unmatchable variants to base",
                 "[orca_match]") {
    // The reported bug: K2 CFS slot loaded with ASA-GF synced to Orca as PLA.
    CHECK(filament::orca_match_type("ASA-GF") == "ASA");
    CHECK(filament::orca_match_type("PLA-GF") == "PLA");
    CHECK(filament::orca_match_type("PETG-GF") == "PETG");
    CHECK(filament::orca_match_type("PC-GF") == "PC");
}

TEST_CASE_METHOD(TableFixture, "orca_match_type applies explicit overrides", "[orca_match]") {
    CHECK(filament::orca_match_type("rPLA") == "PLA");
    CHECK(filament::orca_match_type("TPE") == "TPU");
    CHECK(filament::orca_match_type("TPU-95A") == "TPU");
    CHECK(filament::orca_match_type("PLA+") == "PLA");
    CHECK(filament::orca_match_type("ASA+") == "ASA");
}

TEST_CASE_METHOD(TableFixture, "orca_match_type emits nothing rather than a wrong match",
                 "[orca_match][safety]") {
    // PET is NOT PETG — different polymer, different temperatures. A future
    // "helpful" PET->PETG mapping must fail this test loudly.
    CHECK(filament::orca_match_type("PET") == "");
    CHECK(filament::orca_match_type("PET-GF") == "");
    // High-temp engineering materials with no library equivalent. Blank makes
    // Orca show the lane as empty; a guess would give it PLA temps.
    CHECK(filament::orca_match_type("PPS") == "");
    CHECK(filament::orca_match_type("PPS-CF") == "");
    CHECK(filament::orca_match_type("PPA") == "");
    // Garbage in, nothing out — never a PLA fallback.
    CHECK(filament::orca_match_type("") == "");
    CHECK(filament::orca_match_type("NotAMaterial") == "");
}

TEST_CASE_METHOD(TableFixture, "orca_match_type handles non-catalog input", "[orca_match]") {
    // Material reaching the write path is not always catalog-sourced: firmware
    // reports strings, the whitelist dropdown has its own spellings, and users
    // type free text. [L093] — test the inputs the code actually receives.
    CHECK(filament::orca_match_type("  PLA  ") == "PLA");   // whitespace
    CHECK(filament::orca_match_type("pla") == "PLA");       // case
    CHECK(filament::orca_match_type("Silk PLA") == "PLA");  // decorated
}
