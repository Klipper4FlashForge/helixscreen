// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "filament_macro_profiles.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <string>

namespace helix::filament_macros {
namespace {

/// One stock macro that drives the hotend itself.
struct SelfHeatingMacro {
    /// Uppercase macro name. Must be a spelling that carries no meaning in
    /// stock Klipper - a generic name proves nothing about the body.
    const char* name;
    /// Which firmware ships it, for the reader deciding whether a new row
    /// belongs beside this one.
    const char* firmware;
};

// QIDI's stock screen drives M604 to load and M603 to unload. Both take the
// temperature as `params.S` (default 250) and drive it themselves with
// `M104 S{hotendtemp}` followed by a blocking `M109 S{hotendtemp}`, so the
// hotend is already at temperature by the time either extrudes - a preheat in
// front of one waits for the same heat twice, at whatever temperature we picked
// rather than the one the macro then commands. AmsBackendQidi drives
// `M603 S<temp>` for the same reason.
//
// Neither spelling means anything in Marlin either (M603 there configures a
// filament change and there is no M604), so a Klipper config defining them is
// running QIDI's convention.
constexpr std::array<SelfHeatingMacro, 2> SELF_HEATING_MACROS{{
    {"M604", "QIDI stock"},
    {"M603", "QIDI stock"},
}};

std::string to_upper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return s;
}

} // namespace

bool macro_heats_hotend(const std::string& macro_name) {
    if (macro_name.empty()) {
        return false;
    }
    const std::string upper = to_upper(macro_name);
    return std::any_of(SELF_HEATING_MACROS.begin(), SELF_HEATING_MACROS.end(),
                       [&upper](const SelfHeatingMacro& m) { return upper == m.name; });
}

} // namespace helix::filament_macros
