// SPDX-License-Identifier: GPL-3.0-or-later

#include "z_offset_utils.h"

#include "ui_emergency_stop.h"
#include "ui_error_reporting.h"
#include "ui_toast_manager.h"

#include "config.h"
#include "i_moonraker_api.h"
#include "lvgl/src/others/translation/lv_translation.h"
#include "toolhead_homing.h"

#include <spdlog/fmt/fmt.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <lvgl.h>

namespace helix::zoffset {

bool is_auto_saved(ZOffsetCalibrationStrategy strategy) {
    if (strategy == ZOffsetCalibrationStrategy::FIRMWARE_MANAGED) {
        // FIRMWARE_MANAGED means firmware or macros handle Z-offset persistence
        // (e.g., FlashForge firmware, Artillery M1 save-zoffset macro).
        // HelixScreen should not offer its own save path.
        spdlog::debug("[ZOffsetUtils] Z-offset auto-saved by firmware (firmware_managed strategy)");
        ToastManager::instance().show(ToastSeverity::INFO,
                                      lv_tr("Z-offset is auto-saved by firmware"), 3000);
        return true;
    }
    return false;
}

void format_delta(int microns, char* buf, size_t buf_size) {
    if (microns == 0) {
        buf[0] = '\0';
        return;
    }
    double mm = static_cast<double>(microns) / 1000.0;
    std::snprintf(buf, buf_size, "%+.3fmm", mm);
}

void format_offset(int microns, char* buf, size_t buf_size) {
    double mm = static_cast<double>(microns) / 1000.0;
    std::snprintf(buf, buf_size, "%+.3fmm", mm);
}

void format_offset_compact(int microns, char* buf, size_t buf_size) {
    // Drop leading zero for |value| < 1mm: "+.050mm" instead of "+0.050mm"
    int abs_microns = microns < 0 ? -microns : microns;
    if (abs_microns < 1000) {
        char sign = microns < 0 ? '-' : '+';
        std::snprintf(buf, buf_size, "%c.%03dmm", sign, abs_microns);
    } else {
        double mm = static_cast<double>(microns) / 1000.0;
        std::snprintf(buf, buf_size, "%+.3fmm", mm);
    }
}

int displayed_z_offset_microns(int live_microns, std::optional<int> persisted_microns,
                               bool print_active) {
    if (print_active || !persisted_microns.has_value()) {
        return live_microns;
    }
    return *persisted_microns;
}

int displayed_z_offset_microns(helix::PrinterState& state) {
    return displayed_z_offset_microns(lv_subject_get_int(state.get_gcode_z_offset_subject()),
                                      state.get_persisted_z_offset_microns(),
                                      lv_subject_get_int(state.get_print_active_subject()) != 0);
}

std::string build_z_adjust_gcode(int base_microns, int live_microns, int delta_microns,
                                 bool all_homed) {
    // MOVE=1 makes the toolhead take up the new offset immediately, which is what
    // makes baby stepping usable. Klipper errors on it when an axis is unhomed.
    const char* move = all_homed ? " MOVE=1" : "";

    if (base_microns == live_microns) {
        return fmt::format("SET_GCODE_OFFSET Z_ADJUST={:.3f}{}",
                           static_cast<double>(delta_microns) / 1000.0, move);
    }
    return fmt::format("SET_GCODE_OFFSET Z={:.3f}{}",
                       static_cast<double>(base_microns + delta_microns) / 1000.0, move);
}

void apply_and_save(IMoonrakerAPI* api, ZOffsetCalibrationStrategy strategy,
                    std::function<void()> on_success,
                    std::function<void(const std::string& error)> on_error, PrinterState* ps) {
    // Both success paths below (the FIRMWARE_MANAGED early return and the
    // APPLY -> SAVE_CONFIG chain) funnel through this wrapper, so the pending
    // Z-offset delta is cleared exactly once, wherever the save actually
    // completed — including firmware-managed printers, where the offset is
    // genuinely persisted even though HelixScreen sent nothing.
    auto on_saved = [ps, on_success = std::move(on_success)]() {
        if (ps) {
            ps->clear_pending_z_offset_delta();
        }
        if (on_success) {
            on_success();
        }
    };

    if (!api) {
        spdlog::error("[ZOffsetUtils] apply_and_save called with null API");
        if (on_error)
            on_error("No printer connection");
        return;
    }

    if (strategy == ZOffsetCalibrationStrategy::FIRMWARE_MANAGED) {
        // Firmware/macros handle persistence — nothing for us to do
        spdlog::debug("[ZOffsetUtils] apply_and_save: firmware_managed strategy — auto-saved");
        on_saved();
        return;
    }

    const char* apply_cmd = (strategy == ZOffsetCalibrationStrategy::PROBE_CALIBRATE)
                                ? "Z_OFFSET_APPLY_PROBE"
                                : "Z_OFFSET_APPLY_ENDSTOP";

    const char* strategy_name =
        (strategy == ZOffsetCalibrationStrategy::PROBE_CALIBRATE) ? "probe_calibrate" : "endstop";

    spdlog::info("[ZOffsetUtils] Applying Z-offset with {} strategy (cmd: {})", strategy_name,
                 apply_cmd);

    api->execute_gcode(
        apply_cmd,
        [api, apply_cmd, on_saved, on_error]() {
            spdlog::info("[ZOffsetUtils] {} success, executing SAVE_CONFIG", apply_cmd);

            // Suppress disconnect modal — SAVE_CONFIG triggers a Klipper restart
            EmergencyStopOverlay::instance().suppress_recovery_dialog(RecoverySuppression::LONG);

            api->execute_gcode(
                "SAVE_CONFIG",
                [on_saved]() {
                    spdlog::info("[ZOffsetUtils] SAVE_CONFIG success — Klipper restarting");
                    on_saved();
                },
                [on_error](const MoonrakerError& err) {
                    // Log in English (developer-facing), hand the user a
                    // translated copy. The message used to be one bare
                    // fmt::format serving both, so the whole sentence was
                    // untranslatable.
                    spdlog::error("[ZOffsetUtils] SAVE_CONFIG failed: {}", err.user_message());
                    if (on_error)
                        on_error(fmt::format(
                            lv_tr(
                                "SAVE_CONFIG failed: {}. Z-offset was applied but not saved. "
                                "Run SAVE_CONFIG manually or the offset will be lost on restart."),
                            err.user_message()));
                });
        },
        [apply_cmd, on_error](const MoonrakerError& err) {
            spdlog::error("[ZOffsetUtils] {} failed: {}", apply_cmd, err.user_message());
            if (on_error)
                on_error(fmt::format(lv_tr("{} failed: {}"), apply_cmd, err.user_message()));
        });
}

int persisted_step_index() {
    Config* config = Config::get_instance();
    if (!config) {
        return kZStepDefaultIndex;
    }
    int idx = config->get<int>(config->df() + "z_offset/step_index", kZStepDefaultIndex);
    if (idx < 0 || idx >= static_cast<int>(std::size(kZStepAmountsMm))) {
        return kZStepDefaultIndex;
    }
    return idx;
}

void set_persisted_step_index(int idx) {
    if (idx < 0 || idx >= static_cast<int>(std::size(kZStepAmountsMm))) {
        // Reject rather than clamp-and-write: the read path (persisted_step_index())
        // already fully defends against a corrupt on-disk value, so clamping here
        // buys no safety — it would just give a future caller bug a way to silently
        // overwrite the user's real setting with the default.
        spdlog::warn("[zoffset] rejecting out-of-range step index {} — leaving persisted "
                     "value unchanged",
                     idx);
        return;
    }
    Config* config = Config::get_instance();
    if (!config) {
        return;
    }
    config->set<int>(config->df() + "z_offset/step_index", idx);
    config->save();
}

bool should_extend_save_timeout(bool restart_latched, unsigned extensions_used,
                                unsigned max_extensions) {
    // Only stall the clock if this save actually triggered a restart. A save
    // that never restarted Klipper and still has not completed is a real hang
    // and must be reported.
    return restart_latched && extensions_used < max_extensions;
}

AdjustResult adjust(IMoonrakerAPI* api, PrinterState* ps, double session_base_mm,
                    double current_offset_mm, double delta_mm) {
    // Bound how far one session may travel from the offset it opened on, so a
    // stuck button cannot walk the nozzle into the bed. Clamping the absolute
    // offset instead snapped a legitimately large one down to the limit on the
    // first tap - a nose dive rather than a guard rail. The window is widened to
    // always contain the current offset, so should the base ever go stale the
    // worst outcome is a refused step rather than a jump.
    const double min_offset =
        std::min(session_base_mm - kZOffsetMaxSessionTravelMm, current_offset_mm);
    const double max_offset =
        std::max(session_base_mm + kZOffsetMaxSessionTravelMm, current_offset_mm);

    double new_offset = current_offset_mm + delta_mm;
    if (new_offset < min_offset || new_offset > max_offset) {
        spdlog::warn("[zoffset] {:.3f}mm clamped to [{:.3f}, {:.3f}]", new_offset, min_offset,
                     max_offset);
        new_offset = std::clamp(new_offset, min_offset, max_offset);
        delta_mm = new_offset - current_offset_mm;
        if (std::abs(delta_mm) < 0.0005) {
            return AdjustResult{0.0, current_offset_mm, false, true};
        }
    }

    // Round to the micron so repeated additions cannot drift.
    new_offset = std::round(new_offset * 1000.0) / 1000.0;

    const int delta_microns = static_cast<int>(std::lround(delta_mm * 1000.0));
    const int new_microns = static_cast<int>(std::lround(new_offset * 1000.0));
    const int base_microns = new_microns - delta_microns;
    // Read the live offset before the optimistic write below overwrites it.
    const int live_microns = ps ? lv_subject_get_int(ps->get_gcode_z_offset_subject()) : 0;
    const bool adjusting_from_persisted = ps && base_microns != live_microns;

    if (ps) {
        ps->add_pending_z_offset_delta(delta_microns);
        // Publish immediately rather than waiting for Moonraker to broadcast.
        if (auto* subj = ps->get_gcode_z_offset_subject()) {
            lv_subject_set_int(subj, new_microns);
        }
        // When the base came from the firmware-persisted value we are about to
        // send an absolute Z=, which ZMOD's override stores verbatim. Move the
        // persisted subject with it so the Controls row does not show the stale
        // number until save_variables is broadcast back.
        if (adjusting_from_persisted) {
            if (auto* subj = ps->get_persisted_z_offset_subject()) {
                lv_subject_set_int(subj, new_microns);
            }
        }
    }

    if (!api) {
        return AdjustResult{delta_mm, new_offset, false, false};
    }

    const bool all_homed = ps && helix::toolhead_is_homed(*ps);

    // Relative Z_ADJUST resolves against homing_origin, so it is only right when
    // the base we adjusted from IS the live offset. See build_z_adjust_gcode().
    const std::string gcode =
        build_z_adjust_gcode(base_microns, live_microns, delta_microns, all_homed);
    const double sent_delta = delta_mm;
    api->execute_gcode(
        gcode, [sent_delta]() { spdlog::debug("[zoffset] adjusted {:+.3f}mm", sent_delta); },
        [](const MoonrakerError& err) {
            spdlog::error("[zoffset] adjust failed: {}", err.message);
            NOTIFY_ERROR(lv_tr("Z-offset failed: {}"), err.user_message());
        });

    return AdjustResult{delta_mm, new_offset, true, false};
}

} // namespace helix::zoffset
