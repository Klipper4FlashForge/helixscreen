// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "lvgl/lvgl.h"

/// Per-tool heater card. Uses heater_summary_card.xml for appearance and
/// instance-owned subjects for its live temperature/target. Clicking anywhere
/// opens that tool's preheat dialog; no selection or physical change occurs.
/// XML: <tool_chip index="$i"/> inside a tool_count repeat.
void ui_tool_chip_register_widget();
bool ui_tool_chip_is_valid(lv_obj_t* obj);
int ui_tool_chip_get_index(lv_obj_t* obj);
