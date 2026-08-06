// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace helix::widget_size {

/// Physical size bands for home widget layout decisions.
///
/// A widget picks its layout from the pixels it occupies rather than from a
/// grid span, so one authored span reads correctly on every panel and
/// orientation. Each value sits just below the smallest extent at which the
/// span predicate it replaces fires on any shipping panel, measured live
/// across all eight tiers — so no panel loses a layout it renders today.
///
/// Span and physical width are not monotonic across tiers (a portrait panel's
/// single column is wider than a small landscape panel's two), so these
/// thresholds cannot reproduce span behavior exactly. Where the two disagree,
/// physical size decides.
inline constexpr int W_NORMAL = 134; ///< was colspan >= 2 (smallest measured: 135)
inline constexpr int W_WIDE = 204;   ///< was colspan >= 3 (smallest measured: 205)
inline constexpr int H_TALL = 130;   ///< was rowspan >= 2 (smallest measured: 131)
inline constexpr int H_TALLER = 197; ///< was rowspan >= 3 (smallest measured: 197.5)

} // namespace helix::widget_size
