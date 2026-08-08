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

/// A single-row cell's height alone already exceeds H_TALL on Large and
/// XLarge (their one-row extent is 141px and 169px — taller than Micro's
/// genuinely-2-row 131px), so a predicate that reads "tall" from height_px
/// alone treats a plain 1x1 widget on those two tiers as if it had gained a
/// second row it never has. That is harmless for a layout that only scales
/// type or icon size in place, but wrong for a layout that would also lay
/// out new content across the width on the strength of height_px alone — a
/// legend beside a chart, a second column, a longer resolved label next to
/// an icon. A widget with that shape needs its own width-bearing predicate
/// (`width_px >= W_NORMAL`, plain and untransposable), not one that infers
/// width readiness from height.

} // namespace helix::widget_size
