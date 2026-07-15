// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_plr_prompt.h"

#include "ui_error_reporting.h"
#include "ui_event_safety.h"
#include "ui_filename_utils.h"
#include "ui_modal.h"
#include "ui_update_queue.h"

#include "app_globals.h"
#include "lvgl/src/others/translation/lv_translation.h"
#include "moonraker_api.h"
#include "moonraker_error.h"
#include "printer_state.h"
#include "snapmaker_resume.h"

#include <spdlog/fmt/fmt.h>
#include <spdlog/spdlog.h>

#include <utility>

namespace helix::ui {

namespace {

// Both button handlers receive the borrowed MoonrakerAPI* via user_data. The
// gcode error callback may arrive on the libhv WebSocket thread, so the coded
// message is extracted on that thread (pure string work) and the toast + lv_tr
// lookup are bounced onto the main thread via queue_update — never capture
// widgets or `this` (there is no owning object; the modal is fire-and-forget).
void run_recovery_gcode(MoonrakerAPI* api, const char* gcode, const char* fail_fmt,
                        const char* log_tag) {
    api->execute_gcode(
        gcode,
        [log_tag]() { spdlog::info("[PLR] {} accepted by firmware", log_tag); },
        [fail_fmt, log_tag](const MoonrakerError& err) {
            spdlog::error("[PLR] {} failed: {}", log_tag, err.message);
            std::string detail = helix::snapmaker_extract_coded_msg(err.message, err.user_message());
            helix::ui::queue_update("ui_plr_prompt::recovery_error",
                                    [fail_fmt, detail = std::move(detail)]() {
                                        NOTIFY_ERROR(fmt::runtime(lv_tr(fail_fmt)), detail);
                                    });
        });
}

void on_plr_resume(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[PLR] on_plr_resume");
    auto* api = static_cast<MoonrakerAPI*>(lv_event_get_user_data(e));
    Modal::hide(Modal::get_top());
    if (!api) {
        spdlog::error("[PLR] resume: api is null");
        return;
    }
    spdlog::info("[PLR] User chose Resume — running SDCARD_PRINT_PL_RESTORE");
    run_recovery_gcode(api, "SDCARD_PRINT_PL_RESTORE", "Recovery failed: {}",
                       "SDCARD_PRINT_PL_RESTORE");
    LVGL_SAFE_EVENT_CB_END();
}

void on_plr_discard(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[PLR] on_plr_discard");
    auto* api = static_cast<MoonrakerAPI*>(lv_event_get_user_data(e));
    Modal::hide(Modal::get_top());
    if (!api) {
        spdlog::error("[PLR] discard: api is null");
        return;
    }
    spdlog::info("[PLR] User chose Discard — running SDCARD_PRINT_PL_CLEAR_ENV");
    run_recovery_gcode(api, "SDCARD_PRINT_PL_CLEAR_ENV", "Failed to discard recovery data: {}",
                       "SDCARD_PRINT_PL_CLEAR_ENV");
    LVGL_SAFE_EVENT_CB_END();
}

} // namespace

std::string plr_prompt_body(const std::string& file_path, const char* with_file_fmt,
                            const char* generic_body) {
    const char* generic = generic_body ? generic_body : "";
    if (file_path.empty() || !with_file_fmt) {
        return generic;
    }
    std::string name = helix::gcode::get_display_filename(file_path);
    if (name.empty()) {
        return generic;
    }
    try {
        return fmt::format(fmt::runtime(with_file_fmt), name);
    } catch (const std::exception& ex) {
        // A mistranslated placeholder must never abort through the LVGL C
        // dispatch frame — fall back to the generic body.
        spdlog::warn("[PLR] body format failed: {}", ex.what());
        return generic;
    }
}

void show_plr_recovery_prompt(MoonrakerAPI* api) {
    if (!api) {
        spdlog::warn("[PLR] show_plr_recovery_prompt: api is null — skipping");
        return;
    }

    const std::string& file = get_printer_state().pl_recovery_file();
    std::string body = plr_prompt_body(
        file, lv_tr("The printer lost power while printing {}. Resume where it left off?"),
        lv_tr("The printer lost power during a print. It can resume where it left off."));

    // modal_configure stores the button-label POINTERS in subjects, so they
    // must outlive the modal — lv_tr(...) returns static-lifetime strings.
    modal_configure(ModalSeverity::Info, /*show_cancel=*/true, lv_tr("Resume"), lv_tr("Discard"));

    const char* attrs[] = {"title", lv_tr("Resume interrupted print?"), "message", body.c_str(),
                           nullptr};
    lv_obj_t* dialog = Modal::show("modal_dialog", attrs);
    if (!dialog) {
        spdlog::error("[PLR] Failed to create recovery prompt modal");
        return;
    }

    // Pass `api` as borrowed user_data (no heap ctx → backdrop dismiss cannot
    // leak). btn_secondary = Discard, btn_primary = Resume.
    if (lv_obj_t* discard_btn = lv_obj_find_by_name(dialog, "btn_secondary")) {
        lv_obj_add_event_cb(discard_btn, on_plr_discard, LV_EVENT_CLICKED, api);
    }
    if (lv_obj_t* resume_btn = lv_obj_find_by_name(dialog, "btn_primary")) {
        lv_obj_add_event_cb(resume_btn, on_plr_resume, LV_EVENT_CLICKED, api);
    }

    spdlog::info("[PLR] Recovery prompt shown (recovery_file='{}')", file);
}

} // namespace helix::ui
