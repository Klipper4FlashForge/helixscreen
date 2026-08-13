// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>

/**
 * @brief Mark a string literal for translation extraction without translating
 *        it here.
 *
 * `lv_tr()` both marks a string for the extractor and looks it up, which only
 * works when those happen in the same place. Static tables break that: the copy
 * is authored in a table initializer, but the lookup has to happen later, at
 * render time, once a language is actually loaded. Wrapping the literal in
 * `lv_tr()` at the table would resolve it once, at static-init time, against
 * whatever pack happened to be loaded — usually none.
 *
 * So the two halves are split, the same way gettext splits `N_()` from `_()`:
 * this macro expands to the literal unchanged and exists purely so
 * `scripts/translations/extractor.py` can see it, and the call site keeps
 * calling `lv_tr()` on the stored pointer.
 *
 * Without it a table's strings are extracted only by coincidence — when the
 * same English text happens to appear in some XML file — which is how 18 of the
 * 37 panel-widget names ended up untranslated in every locale.
 */
#define TR_NOOP(s) (s)

namespace helix::ui {

/**
 * @brief Ensure the given locale's translation pack is loaded into LVGL.
 *
 * HelixScreen ships one XML per locale (ui_xml/translations/<lang>.xml) so the
 * app can load only the current language at startup (~60-80 KB heap) instead
 * of the combined 9-language translations.xml (~500-700 KB heap).
 *
 * LVGL's translation system has no remove API — once a pack is registered it
 * stays until lv_translation_deinit(). This function de-duplicates via an
 * internal set so repeated calls for the same lang are no-ops. As the user
 * cycles between languages during a session, multiple packs accumulate, but
 * typical usage touches one or two.
 *
 * English IS loaded too (even though tags ARE English) — otherwise every
 * lv_tr() call that doesn't find a matching pack logs a warning. The ~140 KB
 * heap cost of en.xml is tolerable vs. warning-per-call log spam.
 *
 * @param lang Locale code, e.g. "en", "de", "zh"
 */
void ensure_translation_loaded(const std::string& lang);

} // namespace helix::ui
