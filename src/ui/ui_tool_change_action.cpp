// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_tool_change_action.h"

#include "ui_error_reporting.h"
#include "ui_event_safety.h"
#include "ui_modal.h"

#include "ams_state.h"
#include "app_globals.h"
#include "filament_op_slot_resolver.h"
#include "printer_state.h"
#include "tool_state.h"

#include <spdlog/spdlog.h>

namespace helix::ui {

AmsError tool_change_refusal(const PrinterState& printer_state) {
    const auto lifecycle = printer_state.get_print_lifecycle();
    const bool paused = lifecycle == PrintState::Paused;
    AmsBackend* backend = AmsState::instance().get_backend();
    const bool self_homes = backend && backend->filament_ops_self_home();

    if (!print_blocks_filament_op(lifecycle, self_homes)) {
        return AmsErrorHelper::success();
    }
    return AmsErrorHelper::print_active(paused, /*pause_allows_ops=*/!self_homes);
}

bool can_dock_tool() {
    AmsBackend* backend = AmsState::instance().get_backend();
    return backend && is_tool_changer(backend->get_type());
}

void dispatch_tool_change(int tool_index) {
    if (tool_index == DOCK_TOOL_INDEX) {
        spdlog::info("[Tool Change] Requesting tool dock");
        AmsBackend* backend = AmsState::instance().get_backend();
        if (!backend) {
            NOTIFY_ERROR(lv_tr("Tool change failed: {}"), "No tool changer backend");
            return;
        }
        const AmsError result = backend->unload_filament(DOCK_TOOL_INDEX);
        if (!result) {
            NOTIFY_ERROR(lv_tr("Tool change failed: {}"), result.display_text());
        }
        return;
    }

    spdlog::info("[Tool Change] Requesting tool change to T{}", tool_index);
    ToolState::instance().request_tool_change(
        tool_index, get_moonraker_api(), /*on_success=*/nullptr,
        [](const std::string& error) { NOTIFY_ERROR(lv_tr("Tool change failed: {}"), error); });
}

void request_tool_change(PrinterState& printer_state, int tool_index) {
    auto& tool_state = ToolState::instance();
    if (tool_index == tool_state.active_tool_index()) {
        spdlog::debug("[Tool Change] Tool T{} already active, ignoring", tool_index);
        return;
    }
    if (tool_index == DOCK_TOOL_INDEX && !can_dock_tool()) {
        spdlog::warn("[Tool Change] Dock requested without a tool changer backend, ignoring");
        return;
    }

    const AmsError refusal = tool_change_refusal(printer_state);
    if (!refusal.success()) {
        spdlog::info("[Tool Change] Change to T{} refused: {}", tool_index, refusal.technical_msg);
        notify_ams_warning(refusal);
        return;
    }

    if (printer_state.get_print_lifecycle() == PrintState::Paused) {
        modal_show_confirmation(
            lv_tr("Change Tool While Paused"),
            lv_tr("The print is paused. Changing tools now moves the toolhead and swaps the "
                  "filament at the nozzle. Resume the print once the change finishes."),
            ::ModalSeverity::Warning, lv_tr("Change Tool"),
            [](lv_event_t* e) {
                LVGL_SAFE_EVENT_CB_BEGIN("[Tool Change] confirm_tool_change");
                const int idx =
                    static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
                dispatch_tool_change(idx);
                LVGL_SAFE_EVENT_CB_END();
            },
            nullptr, reinterpret_cast<void*>(static_cast<intptr_t>(tool_index)));
        return;
    }

    dispatch_tool_change(tool_index);
}

} // namespace helix::ui
