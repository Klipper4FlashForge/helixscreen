// SPDX-License-Identifier: GPL-3.0-or-later

#include "filament_variants.h"

#include "filament_database.h"

#include <algorithm>
#include <cctype>

namespace filament {

namespace {

/// Known variant affixes, matched case-insensitively and ONLY when delimited by
/// a '-', '_' or ' ' separator. Derived from the type strings actually present
/// in assets/filaments.json (CF, GF, AERO), the variant rows in
/// filament_database.h MATERIALS[] (Silk/Matte/Wood/Marble/Metal/Glow), and the
/// prefixed product names the catalog carries (HT-PLA-GF, PLA-HS, PETG+HS,
/// PLA-LW, Bambu PETG HF).
///
/// Deliberately NOT listed: "ABS" (so "PC-ABS" keeps its own identity and its
/// ABS_ASA compat group), "Soft"/"95A"/"85A" (TPU shore grades), "Change"
/// (Color-Change). Adding a polymer name here would merge two real materials.
constexpr const char* VARIANT_AFFIXES[] = {
    // Fiber fills
    "CF",
    "GF",
    // Foaming / lightweight grades
    "AERO",
    "LW",
    // Speed / temperature grades
    "HS",
    "HF",
    "HT",
    // Cosmetic finishes
    "Silk",
    "Matte",
    "Wood",
    "Marble",
    "Metal",
    "Glow",
};

/// Family for PAHT-branded products. "PAHT" is not a standardized polymer
/// designation — it is a marketing category, and the underlying resin varies by
/// vendor. Every PAHT product in assets/filaments.json is PA12/PA612-class
/// (Bambu and Creality both name PA12 as the base resin) and prints in the
/// ordinary PA envelope, which is why it maps to PA. Other vendors ship
/// PPA-based PAHT-CF under the same type string, at PPA temperatures and
/// needing a hardened nozzle and sealed enclosure: verify the base resin before
/// adding a PAHT product from a new vendor rather than assuming this mapping.
constexpr const char* kPahtFamily = "PA";

/// Explicit family mapping for names that affix-stripping alone cannot reduce,
/// because the modifier is fused to the polymer name with no separator.
///
/// Numbered nylon grades collapse into PA: the catalog does not carry enough of
/// each to justify separate headings (PA-CF 14 products, PA 8, PA-GF 3,
/// PA6-CF 3, PA12 a single Generic product), and it files PA6-CF products under
/// BOTH type=PA-CF and type=PA6-CF — one "PA" heading papers over that split.
///
/// Self-mapping rows are documented STOPS, not no-ops: they assert that a name
/// which superficially looks reducible must be left alone.
struct FamilyOverride {
    const char* name;
    const char* family;
};
constexpr FamilyOverride FAMILY_OVERRIDES[] = {
    {"PA6", "PA"},
    {"PA12", "PA"},
    {"PA66", "PA"},
    {"PA612", "PA"},
    {"PAHT", kPahtFamily},
    // STOP: PPA (polyphthalamide) must NEVER be normalized or aliased to PA.
    // The names are one letter apart, but it is a semi-aromatic polyamide in a
    // different processing regime — 280-310C nozzle, 100-120C bed, sealed
    // enclosure and hardened nozzle, against PA's 260-290C/90-110C. It keeps
    // its own heading, with PPA-CF and PPA-GF filed under it.
    {"PPA", "PPA"},
    // STOP: copolyester elastomer ends in "PE" but is unrelated to polyethylene.
    {"CoPE", "CoPE"},
    // STOP: polyethylene is a polyolefin, NOT polyethylene terephthalate.
    // "PE-CF" must reduce to PE and "PET-CF" to PET — never across.
    {"PE", "PE"},
    {"PET", "PET"},
};

bool iequals(std::string_view a, std::string_view b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char ca, char cb) {
               return std::tolower(static_cast<unsigned char>(ca)) ==
                      std::tolower(static_cast<unsigned char>(cb));
           });
}

bool is_separator(char c) {
    return c == '-' || c == '_' || c == ' ';
}

bool is_variant_affix(std::string_view token) {
    for (const char* affix : VARIANT_AFFIXES) {
        if (iequals(token, affix)) {
            return true;
        }
    }
    return false;
}

std::string_view trim(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())))
        s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
        s.remove_suffix(1);
    return s;
}

/// One pass of affix removal. Returns true if @p s was shortened.
bool strip_one_affix(std::string_view& s) {
    // Leading affix: "HT-PLA-GF" -> "PLA-GF", "Silk PLA" -> "PLA"
    for (size_t i = 0; i < s.size(); ++i) {
        if (!is_separator(s[i]))
            continue;
        if (is_variant_affix(s.substr(0, i))) {
            std::string_view rest = s.substr(i + 1);
            if (!rest.empty()) {
                s = rest;
                return true;
            }
        }
        break; // only the first token can be a leading affix
    }
    // Trailing affix: "PLA-CF" -> "PLA", "PLA Silk" -> "PLA"
    for (size_t i = s.size(); i > 0; --i) {
        if (!is_separator(s[i - 1]))
            continue;
        if (is_variant_affix(s.substr(i))) {
            std::string_view rest = s.substr(0, i - 1);
            if (!rest.empty()) {
                s = rest;
                return true;
            }
        }
        break; // only the last token can be a trailing affix
    }
    return false;
}

/// Explicit family override lookup, or empty if the name has no entry.
std::string_view family_override(std::string_view name) {
    for (const auto& row : FAMILY_OVERRIDES) {
        if (iequals(name, row.name)) {
            return row.family;
        }
    }
    return {};
}

} // namespace

std::string extract_base_material(std::string_view name) {
    std::string_view work = trim(name);
    if (work.empty()) {
        return std::string(name);
    }

    // Resolve aliases first so decoration on the CANONICAL spelling is visible:
    // "SILK" -> "Silk PLA" -> (leading affix) -> "PLA".
    work = resolve_alias(work);

    // Strip known affixes from both ends until nothing more is recognised. The
    // override table is consulted each round so a fused grade name exposed by
    // stripping is caught ("PA6-CF" -> "PA6" -> "PA").
    for (int guard = 0; guard < 8; ++guard) {
        if (auto fam = family_override(work); !fam.empty()) {
            return std::string(fam);
        }
        if (!strip_one_affix(work)) {
            break;
        }
    }

    if (find_material(work).has_value()) {
        return std::string(work);
    }

    // Unrecognised compound name ("PLA SnapSpeed"): walk progressively shorter
    // prefixes at separator boundaries against the database.
    for (size_t i = work.size(); i > 0; --i) {
        if (!is_separator(work[i - 1]))
            continue;
        auto prefix = work.substr(0, i - 1);
        if (!prefix.empty() && find_material(prefix).has_value()) {
            return std::string(prefix);
        }
    }

    return std::string(work);
}

std::string display_family(std::string_view type) {
    std::string base = extract_base_material(type);
    // A type we cannot reduce is its own family — one heading, one entry — so
    // user-overlay and firmware-only types stay reachable instead of vanishing.
    return base.empty() ? std::string(type) : base;
}

} // namespace filament
