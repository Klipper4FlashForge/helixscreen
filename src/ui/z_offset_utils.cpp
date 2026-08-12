// SPDX-License-Identifier: GPL-3.0-or-later

#include "z_offset_utils.h"

#include "ui_emergency_stop.h"
#include "ui_error_reporting.h"
#include "ui_toast_manager.h"

#include "i_moonraker_api.h"

#include <spdlog/fmt/fmt.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
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
                    std::string msg = fmt::format(
                        "SAVE_CONFIG failed: {}. Z-offset was applied but not saved. "
                        "Run SAVE_CONFIG manually or the offset will be lost on restart.",
                        err.user_message());
                    spdlog::error("[ZOffsetUtils] {}", msg);
                    if (on_error)
                        on_error(msg);
                });
        },
        [apply_cmd, on_error](const MoonrakerError& err) {
            std::string msg = fmt::format("{} failed: {}", apply_cmd, err.user_message());
            spdlog::error("[ZOffsetUtils] {}", msg);
            if (on_error)
                on_error(msg);
        });
}

bool should_extend_save_timeout(bool restart_latched, unsigned extensions_used,
                                unsigned max_extensions) {
    // Only stall the clock if this save actually triggered a restart. A save
    // that never restarted Klipper and still has not completed is a real hang
    // and must be reported.
    return restart_latched && extensions_used < max_extensions;
}

AdjustResult adjust(IMoonrakerAPI* api, PrinterState* ps, double current_offset_mm,
                    double delta_mm) {
    double new_offset = current_offset_mm + delta_mm;
    if (new_offset < kZOffsetMinMm || new_offset > kZOffsetMaxMm) {
        spdlog::warn("[zoffset] {:.3f}mm clamped to [{}, {}]", new_offset, kZOffsetMinMm,
                     kZOffsetMaxMm);
        new_offset = std::clamp(new_offset, kZOffsetMinMm, kZOffsetMaxMm);
        delta_mm = new_offset - current_offset_mm;
        if (std::abs(delta_mm) < 0.0005) {
            return AdjustResult{0.0, current_offset_mm, false};
        }
    }

    // Round to the micron so repeated additions cannot drift.
    new_offset = std::round(new_offset * 1000.0) / 1000.0;

    if (ps) {
        ps->add_pending_z_offset_delta(static_cast<int>(std::lround(delta_mm * 1000.0)));
        // Publish immediately rather than waiting for Moonraker to broadcast.
        if (auto* subj = ps->get_gcode_z_offset_subject()) {
            lv_subject_set_int(subj, static_cast<int>(std::lround(new_offset * 1000.0)));
        }
    }

    if (!api) {
        return AdjustResult{delta_mm, new_offset, false};
    }

    bool all_homed = false;
    if (ps) {
        const char* axes = lv_subject_get_string(ps->get_homed_axes_subject());
        all_homed = axes && strchr(axes, 'x') && strchr(axes, 'y') && strchr(axes, 'z');
    }

    char gcode[96];
    std::snprintf(gcode, sizeof(gcode), "SET_GCODE_OFFSET Z_ADJUST=%.3f%s", delta_mm,
                  all_homed ? " MOVE=1" : "");
    const double sent_delta = delta_mm;
    api->execute_gcode(
        gcode, [sent_delta]() { spdlog::debug("[zoffset] adjusted {:+.3f}mm", sent_delta); },
        [](const MoonrakerError& err) {
            spdlog::error("[zoffset] adjust failed: {}", err.message);
            NOTIFY_ERROR(lv_tr("Z-offset failed: {}"), err.user_message());
        });

    return AdjustResult{delta_mm, new_offset, true};
}

} // namespace helix::zoffset
