// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "screws_tilt_share_text.h"

#include <cstdio>

namespace helix {

namespace {

/// Trim leading/trailing ASCII whitespace — Klipper's adjustment strings
/// occasionally carry padding, and a QR payload should not.
std::string trimmed(const std::string& in) {
    const auto first = in.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const auto last = in.find_last_not_of(" \t\r\n");
    return in.substr(first, last - first + 1);
}

} // namespace

std::string format_screw_share_z(const ScrewTiltResult& screw) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.3f", static_cast<double>(screw.z_height));
    return buf;
}

std::string format_screw_share_adjustment(const ScrewTiltResult& screw, const char* base_label) {
    if (screw.is_reference) {
        return base_label ? base_label : SCREWS_TILT_SHARE_BASE_LABEL;
    }
    std::string adjustment = trimmed(screw.adjustment);
    if (adjustment.empty()) {
        return SCREWS_TILT_SHARE_NO_ADJUSTMENT;
    }
    return adjustment;
}

std::string build_screws_tilt_share_text(const std::vector<ScrewTiltResult>& results) {
    std::string out = SCREWS_TILT_SHARE_HEADER;

    if (results.empty()) {
        out += "\n(no results)";
        return out;
    }

    for (const auto& screw : results) {
        out += '\n';
        out += screw.display_name();
        out += ": z=";
        out += format_screw_share_z(screw);
        out += ' ';
        out += format_screw_share_adjustment(screw);
    }

    return out;
}

} // namespace helix
