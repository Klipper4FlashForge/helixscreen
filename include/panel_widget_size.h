// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace helix::widget_size {

/// Physical size bands for home widget layout decisions.
///
/// A widget picks its layout from the pixels it occupies rather than from a
/// grid span, so one authored span reads correctly on every panel and
/// orientation. Every predicate that uses these constants compares with
/// `>=`, so each constant must equal the smallest extent at which the span
/// predicate it replaces fires on any shipping panel, not one pixel below
/// it — measured live across all eight tiers, so no panel loses a layout it
/// renders today. (`grid_track_extent()` truncates its float result via
/// `static_cast<int>`, so "smallest measured" here is already the truncated
/// integer pixel value a widget actually receives, not the raw float cell
/// arithmetic.)
///
/// Span and physical width are not monotonic across tiers (a portrait panel's
/// single column is wider than a small landscape panel's two), so these
/// thresholds cannot reproduce span behavior exactly. Where the two disagree,
/// physical size decides.
inline constexpr int W_NORMAL = 135; ///< was colspan >= 2 (smallest measured: 135, Small span2)
inline constexpr int W_WIDE = 205;   ///< was colspan >= 3 (smallest measured: 205, Small span3)
inline constexpr int H_TALL = 131;   ///< was rowspan >= 2 (smallest measured: 131, Micro row2)
inline constexpr int H_TALLER = 197; ///< was rowspan >= 3 (smallest measured: 197.5, truncates to
                                     ///< 197 at runtime, Micro row3 — already the `>=` value)

} // namespace helix::widget_size
