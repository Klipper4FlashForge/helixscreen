// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "lvgl/lvgl.h"

/**
 * @file ui_tool_chip.h
 * @brief One tool's card in the filament panel's tool row.
 *
 * A chip answers, for a single tool, the three things the filament panel is
 * about: which tool this is, what material is mounted on it, and how much of
 * that spool is left. Tapping one selects it — heat, preheat and every filament
 * verb on the panel act on the selected tool.
 *
 * ```
 *  +===========================+   border = heat: amber heating, green at
 *  | * T0              PETG    |   temperature, red still-hot; thicker when
 *  | [========-----]     480g  |   this is the selected tool
 *  +===========================+   dot = material colour, filled when loaded
 * ```                              bar = spool remaining
 *
 * A toolchanger can hold several heads hot at once, which the single nozzle
 * reading on the left cannot express — so heat is per-chip, and it rides the
 * border rather than another dot, because the chip already spends its dot on
 * "which filament is loaded".
 *
 * A row of these replaces both the AMS pill row and the tool dropdown the panel
 * used to carry, which were two controls for one choice. Past four tools the
 * chips compress rather than scroll — the gram figure is what gives way, so an
 * eight-tool machine still shows every spool at a glance.
 *
 * The chip is drawn in C++ rather than composed in XML because its material dot
 * and spool bar take an arbitrary per-tool colour, which no `bind_style` pair
 * can express, and because the compact/wide choice is made from the chip's own
 * measured width.
 *
 * XML usage — `index` is the tool index the chip renders:
 * @code{.xml}
 * <lv_obj name="tool_row" flex_flow="row">
 *   <repeat count="tool_count">
 *     <tool_chip index="$i" flex_grow="1"/>
 *   </repeat>
 * </lv_obj>
 * @endcode
 *
 * Selection is published on the `filament_selected_tool` int subject, which the
 * panel owns; the chip both writes it on click and highlights from it.
 */

/// Name of the int subject carrying the selected tool index (-1 = none).
#define UI_TOOL_CHIP_SELECTED_SUBJECT "filament_selected_tool"

/// Tool count past which chips drop the gram figure to stay legible.
#define UI_TOOL_CHIP_COMPACT_ABOVE 4

/**
 * @brief Register `<tool_chip>` with the XML engine.
 *
 * Call once during application startup, before any XML referencing it is parsed.
 */
void ui_tool_chip_register_widget();

/**
 * @brief True when @p obj is a tool_chip.
 */
bool ui_tool_chip_is_valid(lv_obj_t* obj);

/**
 * @brief Tool index this chip renders, or -1 if @p obj is not a chip.
 */
int ui_tool_chip_get_index(lv_obj_t* obj);
