/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Font-face aliases. AssetManager::register_fonts() (src/application/
 * asset_manager.cpp) references every breakpoint tier's faces by symbol, but
 * helixcore compiles only the 11 medium-tier faces the 800x480 K-Touch renders
 * (see components/helixcore/CMakeLists.txt HELIX_FONT_SRCS). The faces below are
 * NOT in that set; each is aliased to a real medium-tier face so the app core
 * links without shipping every tier's glyph data (a full tier set would blow the
 * 5.8MB image budget). The medium globals.xml only references the 11 real faces,
 * so at runtime these aliases are dead weight referenced solely by
 * register_fonts()'s exhaustive list — never rendered. Real per-tier subsetting
 * for the K-Touch is a later task.
 *
 * Same technique as the Plan 2 native-audit (audit_stubs.cpp font block).
 * Populated during the Task 5 link pass from the actual undefined-symbol set.
 */

#include "lvgl.h"

/* A real, always-linked medium-tier face every alias points at. */
extern const lv_font_t noto_sans_18;

/* (populated during the Task 5 link pass — see report) */
