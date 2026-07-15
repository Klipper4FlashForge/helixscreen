// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Registers the medium-tier (800x480 K-Touch) compiled font faces with
// helix-xml's global font registry under their token names (e.g.
// "noto_sans_26"), matching desktop's AssetManager::register_fonts(). MUST
// be called before theme init — theme_manager_register_responsive_fonts()
// resolves globals.xml font tokens (font_heading_medium, etc.) to these
// names via lv_xml_get_font() and hard-aborts on a miss (audit rule).
void helix_fonts_register(void);
