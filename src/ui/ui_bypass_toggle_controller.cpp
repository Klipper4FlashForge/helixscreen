// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_bypass_toggle_controller.h"

#include "ui_error_reporting.h"

#include "ams_state.h"
#include "app_globals.h"
#include "lvgl/src/others/translation/lv_translation.h"
#include "printer_state.h"

#include <spdlog/spdlog.h>

namespace helix::ui {

BypassToggleController::~BypassToggleController() {
    cancel_pending();
}

void BypassToggleController::toggle() {
    spdlog::info("[BypassToggle] Toggle requested");

    // Print guard — fully disabled while a job owns the toolhead (PRINTING
    // or PAUSED; a paused print still has filament staged mid-path).
    const int state = lv_subject_get_int(get_printer_state().get_print_state_enum_subject());
    if (print_occupies_toolhead(static_cast<PrintJobState>(state))) {
        NOTIFY_WARNING(lv_tr("Bypass cannot be changed while printing"));
        spdlog::info("[BypassToggle] Refused — print active ({})", state);
        return;
    }

    AmsBackend* backend = AmsState::instance().get_backend();
    if (!backend) {
        NOTIFY_WARNING(lv_tr("Multi-Filament System not available"));
        return;
    }

    AmsSystemInfo info = backend->get_system_info();
    if (info.has_hardware_bypass_sensor) {
        NOTIFY_WARNING(lv_tr("Bypass controlled by sensor"));
        spdlog::warn("[BypassToggle] Blocked — hardware sensor controls bypass");
        return;
    }

    if (backend->is_bypass_active()) {
        AmsError error = backend->disable_bypass();
        if (error.result == AmsResult::SUCCESS) {
            NOTIFY_INFO(lv_tr("Bypass disabled"));
        }
        if (error.result != AmsResult::SUCCESS) {
            helix::ui::notify_ams_error(error, lv_tr("Bypass toggle failed"));
        }
        return;
    }

    // Enable path: #1229 chaining discipline — unload first when the backend
    // allows implicit chaining, enable on UNLOADING->IDLE, disarm on ERROR.
    if (should_unload_before_bypass(info, backend->allows_implicit_chaining())) {
        spdlog::info("[BypassToggle] Unloading slot {} before enabling bypass", info.current_slot);
        pending_bypass_enable_ = true;
        AmsError error = backend->unload_active_filament();
        if (error.result == AmsResult::SUCCESS) {
            NOTIFY_INFO(lv_tr("Unloading before bypass..."));
        } else {
            pending_bypass_enable_ = false;
            helix::ui::notify_ams_error(error);
        }
        return;
    }
    enable_now(backend);
}

void BypassToggleController::enable_now(AmsBackend* backend) {
    AmsError error = backend->enable_bypass();
    if (error.result == AmsResult::SUCCESS) {
        NOTIFY_INFO(lv_tr("Bypass enabled"));
    } else {
        helix::ui::notify_ams_error(error, lv_tr("Bypass failed"));
    }
}

bool BypassToggleController::on_ams_action_changed(AmsAction prev, AmsAction next) {
    // The pending flag is armed by the unload we started, so it must be
    // disarmed by whichever way that unload ends. Clearing only on IDLE left
    // a failed unload's flag set, and the next unrelated unload completion
    // then enabled bypass out of nowhere. Only IDLE actually chains.
    if (!pending_bypass_enable_ || prev != AmsAction::UNLOADING ||
        (next != AmsAction::IDLE && next != AmsAction::ERROR)) {
        return false;
    }
    pending_bypass_enable_ = false;
    if (next == AmsAction::ERROR) {
        spdlog::warn("[BypassToggle] Unload failed — cancelling pending bypass enable");
        return true;
    }
    spdlog::info("[BypassToggle] Unload complete — enabling bypass");
    if (AmsBackend* backend = AmsState::instance().get_backend()) {
        enable_now(backend);
    }
    return true;
}

void BypassToggleController::cancel_pending() {
    pending_bypass_enable_ = false;
}

} // namespace helix::ui
