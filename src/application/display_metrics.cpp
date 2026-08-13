// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "display_metrics.h"

#include <algorithm>
#include <cmath>

namespace helix {

namespace {

/// Panels that are permanently attached to a platform, so the platform key
/// alone identifies them. Diagonals are measured, not taken from spec sheets —
/// the fleet survey found reported physical sizes wrong or absent on 6 of 8
/// devices, including a Raspberry Pi whose driver hands back the official 7"
/// touchscreen's exact active area for a ~115mm panel.
///
/// Deliberately absent: pi, pi32, x86, esp32. Those drive a user-chosen
/// display, so a panel size here would be a guess imposed on every user of
/// that build. Also absent: ad5x (no hardware to measure) and snapmaker-u1
/// (its reported size is bogus and the true diagonal is not yet measured).
struct PanelEntry {
    const char* platform_key;
    PanelSpec spec;
};

constexpr PanelEntry kKnownPanels[] = {
    // FlashForge Adventurer 5M — 221.5 DPI. Reports 108x64mm (189 DPI), ~17% low.
    {"ad5m", {800, 480, 107.0}},
    // Elegoo Centauri Carbon — 131.0 DPI. simpledrm synthesizes 96 DPI here.
    {"cc1", {480, 272, 107.0}},
    // Creality K1/K1C — 218.2 DPI. Reports 0x0 over the ioctl, so this table is
    // the only source it has. Panel is physically identical to the K2 Plus.
    {"k1", {480, 800, 108.6}},
    // Creality K2 Plus — 218.2 DPI. One of only two devices that measured honestly.
    {"k2", {480, 800, 108.6}},
};

} // namespace

double DisplayMetrics::ui_scale_for_dpi(double dpi) {
    if (!std::isfinite(dpi) || dpi <= kScaleDeadbandDpi) {
        return 1.0;
    }
    const double scale = 1.0 + (dpi - kScaleDeadbandDpi) * kScaleSlopePerDpi;
    return std::min(scale, kMaxScale);
}

bool DisplayMetrics::dpi_is_plausible(double dpi) {
    return std::isfinite(dpi) && dpi >= kMinPlausibleDpi && dpi <= kMaxPlausibleDpi;
}

bool DisplayMetrics::dpi_is_synthesized(double dpi) {
    return std::isfinite(dpi) && std::fabs(dpi - kSynthesizedDpi) <= kSynthesizedDpiEpsilon;
}

bool DisplayMetrics::dpi_is_trustworthy(double dpi) {
    return dpi_is_plausible(dpi) && !dpi_is_synthesized(dpi);
}

std::optional<PanelSpec> DisplayMetrics::known_panel(const std::string& platform_key) {
    for (const auto& entry : kKnownPanels) {
        if (platform_key == entry.platform_key) {
            return entry.spec;
        }
    }
    return std::nullopt;
}

double DisplayMetrics::dpi_of(const PanelSpec& panel) {
    if (panel.diagonal_mm <= 0.0) {
        return 0.0;
    }
    const double diagonal_px =
        std::hypot(static_cast<double>(panel.width_px), static_cast<double>(panel.height_px));
    return diagonal_px / (panel.diagonal_mm / 25.4);
}

ResolvedDpi DisplayMetrics::resolve_dpi(int user_override_dpi,
                                        std::optional<double> platform_measured,
                                        const std::string& platform_key) {
    // An explicit override wins outright, but a nonsense value is ignored
    // rather than obeyed — it would otherwise be indistinguishable from a
    // deliberate choice and strand the user with an unusable UI.
    if (user_override_dpi > 0) {
        const double requested = static_cast<double>(user_override_dpi);
        if (dpi_is_plausible(requested)) {
            return {requested, DpiSource::UserOverride};
        }
    }

    // Only ever populated by a platform we trust to measure honestly, and even
    // then the value has to survive the plausibility and synthesis checks.
    if (platform_measured && dpi_is_trustworthy(*platform_measured)) {
        return {*platform_measured, DpiSource::PlatformMeasured};
    }

    if (const auto panel = known_panel(platform_key)) {
        return {dpi_of(*panel), DpiSource::KnownPanel};
    }

    return {kReferenceDpi, DpiSource::Fallback};
}

int32_t DisplayMetrics::scaled_px(int32_t authored_px, double scale) {
    if (!std::isfinite(scale) || scale <= 0.0) {
        return authored_px;
    }
    const auto scaled = static_cast<int32_t>(std::lround(authored_px * scale));
    // A positive authored size must never round away to nothing — a 1px
    // hairline or divider stays visible.
    if (authored_px > 0 && scaled < 1) {
        return 1;
    }
    return scaled;
}

} // namespace helix
