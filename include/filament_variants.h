// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>
#include <string_view>

/**
 * @file filament_variants.h
 * @brief Base-material (family) derivation from a decorated material string.
 *
 * A "variant" is a modifier applied to a base polymer: fiber fills (CF/GF),
 * foaming grades (AERO), speed grades (HS/HF), cosmetic finishes (Silk/Matte/
 * Wood/...). Variants may appear as a suffix ("PLA-CF", "PLA Silk") or as a
 * prefix ("HT-PLA-GF", "Silk PLA").
 *
 * Two callers share this one implementation:
 *  - FilamentMapper::materials_match() — reduces a firmware-reported slot
 *    material to something the filament database can look up a compat group for.
 *  - FilamentCatalogSelector — groups catalog TYPE strings under one heading so
 *    the picker shows "ASA" once instead of ASA / ASA-CF / ASA-GF / ASA-AERO.
 *
 * DISPLAY-ONLY for the picker: nothing here changes the `type` string a
 * selection emits. Grouping affects which heading a product is filed under,
 * never its identity, its temperatures, or what gets persisted/sent.
 */

namespace filament {

/**
 * @brief Reduce a decorated material name to its base polymer.
 *
 * Strips known variant affixes (see VARIANT_AFFIXES in the .cpp) from either
 * end at '-', '_' or ' ' boundaries, applies the explicit family override table
 * for polymer grades that string-stripping cannot reach ("PA6" -> "PA"), then
 * falls back to a shortest-known-prefix walk for unrecognised compound names
 * ("PLA SnapSpeed" -> "PLA").
 *
 * Affixes are only stripped when delimited by a separator, so a name that
 * merely *contains* an affix substring is never mangled ("PPA" stays "PPA",
 * "CoPE" stays "CoPE", "PET-CF" reduces to "PET" and never to "PE").
 *
 * @param name Material name, possibly an alias, possibly decorated.
 * @return Base material name, or @p name unchanged if nothing is recognised.
 */
std::string extract_base_material(std::string_view name);

/**
 * @brief Display family heading for a catalog type string.
 *
 * Thin wrapper over extract_base_material() that guarantees a non-empty result:
 * a type we cannot reduce becomes its own family (a heading with one entry),
 * which keeps unknown//user-overlay types visible instead of silently dropped.
 */
std::string display_family(std::string_view type);

} // namespace filament
