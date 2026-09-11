// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ams_error.h"

namespace helix {
class PrinterState;

namespace ui {

inline constexpr int DOCK_TOOL_INDEX = -1;

[[nodiscard]] AmsError tool_change_refusal(const PrinterState& printer_state);
[[nodiscard]] bool can_dock_tool();
void dispatch_tool_change(int tool_index);
void request_tool_change(PrinterState& printer_state, int tool_index);

} // namespace ui
} // namespace helix
