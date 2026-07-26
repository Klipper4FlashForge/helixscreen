// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_error_reporting.h"
#include "ui_notification.h"
#include "ui_temperature_utils.h"

#include "app_globals.h"
#include "fan_gcode.h"
#include "gcode_classify.h"
#include "gcode_homing.h"
#include "http_executor.h"
#include "hv/requests.h"
#include "macro_param_cache.h"
#include "moonraker_api.h"
#include "moonraker_api_internal.h"
#include "moonraker_gcode_annotate.h"
#include "printer_state.h"
#include "sensor_state.h"
#include "spdlog/spdlog.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>

using namespace moonraker_internal;

// ============================================================================
// Temperature Control Operations
// ============================================================================

void MoonrakerAPI::set_temperature(const std::string& heater, double temperature,
                                   SuccessCallback on_success, ErrorCallback on_error) {
    // Reject NaN/Inf before any G-code generation
    if (reject_non_finite({temperature}, "set_temperature", on_error)) {
        return;
    }

    // Validate heater name
    if (!is_safe_identifier(heater)) {
        NOTIFY_ERROR("Invalid heater name '{}'. Contains unsafe characters.", heater);
        if (on_error) {
            MoonrakerError err = MoonrakerError::validation_error(
                "set_temperature", "Invalid heater name contains illegal characters");
            on_error(err);
        }
        return;
    }

    // Validate temperature range
    if (!is_safe_temperature(temperature, safety_limits_)) {
        NOTIFY_ERROR("Temperature {:.0f}°C is out of range. Valid: {:.0f}°C to {:.0f}°C.",
                     temperature, safety_limits_.min_temperature_celsius,
                     safety_limits_.max_temperature_celsius);
        if (on_error) {
            MoonrakerError err = MoonrakerError::validation_error(
                "set_temperature",
                "Temperature " + std::to_string(static_cast<int>(temperature)) +
                    "°C exceeds safety limits (" +
                    std::to_string(static_cast<int>(safety_limits_.min_temperature_celsius)) + "-" +
                    std::to_string(static_cast<int>(safety_limits_.max_temperature_celsius)) +
                    "°C)");
            on_error(err);
        }
        return;
    }

    // Route a chamber set through M141 S{temp} when the printer defines that macro.
    // The safety-limit validation above still bounds the target — M141 is just an
    // alternate transport for the same already-validated temperature.
    const bool use_m141 = helix::ui::temperature::chamber_uses_m141(
        heater, state_.temperature_state().chamber_heater_name(),
        helix::MacroParamCache::instance().has_macro("m141"));

    char gcode_buf[128];
    const char* gcode = helix::ui::temperature::build_heater_gcode(
        heater, helix::units::to_decidegrees(temperature), gcode_buf, sizeof(gcode_buf), use_m141);
    if (!gcode) {
        spdlog::error("[Moonraker API] Cannot build gcode for empty heater name");
        if (on_error) {
            MoonrakerError err =
                MoonrakerError::validation_error("set_temperature", "Empty heater name");
            on_error(err);
        }
        return;
    }

    spdlog::info("[Moonraker API] Setting {} temperature to {}°C{}", heater, temperature,
                 use_m141 ? " (via M141)" : "");

    execute_gcode(gcode, on_success, on_error);
}

void MoonrakerAPI::set_fan_speed(const std::string& fan, double speed, SuccessCallback on_success,
                                 ErrorCallback on_error) {
    // Reject NaN/Inf before any G-code generation
    if (reject_non_finite({speed}, "set_fan_speed", on_error)) {
        return;
    }

    // Validate fan name
    if (!is_safe_identifier(fan)) {
        NOTIFY_ERROR("Invalid fan name '{}'. Contains unsafe characters.", fan);
        if (on_error) {
            MoonrakerError err = MoonrakerError::validation_error(
                "set_fan_speed", "Invalid fan name contains illegal characters");
            on_error(err);
        }
        return;
    }

    // Validate speed percentage
    if (!is_safe_fan_speed(speed, safety_limits_)) {
        NOTIFY_ERROR("Fan speed {:.0f}% is out of range. Valid: {:.0f}% to {:.0f}%.", speed,
                     safety_limits_.min_fan_speed_percent, safety_limits_.max_fan_speed_percent);
        if (on_error) {
            MoonrakerError err = MoonrakerError::validation_error(
                "set_fan_speed",
                "Fan speed " + std::to_string(static_cast<int>(speed)) +
                    "% exceeds safety limits (" +
                    std::to_string(static_cast<int>(safety_limits_.min_fan_speed_percent)) + "-" +
                    std::to_string(static_cast<int>(safety_limits_.max_fan_speed_percent)) + "%)");
            on_error(err);
        }
        return;
    }

    std::string gcode = helix::fan_gcode(fan, speed);
    spdlog::debug("[MoonrakerAPI] set_fan_speed('{}', {:.0f}%) -> {}", fan, speed, gcode);

    execute_gcode(gcode, on_success, on_error);
}

// ============================================================================
// Power Device Control Operations
// ============================================================================

void MoonrakerAPI::get_power_devices(PowerDevicesCallback on_success, ErrorCallback on_error) {
    if (http_base_url_.empty()) {
        spdlog::error("[Moonraker API] HTTP base URL not configured for power devices");
        if (on_error) {
            MoonrakerError err =
                MoonrakerError::connection_lost("get_power_devices", "Not connected to Moonraker");
            on_error(err);
        }
        return;
    }

    std::string url = http_base_url_ + "/machine/device_power/devices";
    spdlog::debug("[Moonraker API] Fetching power devices from: {}", url);

    helix::http::HttpExecutor::fast().submit([url, on_success, on_error]() {
        auto resp = requests::get(url.c_str());

        if (!resp) {
            spdlog::error("[Moonraker API] HTTP request failed for power devices");
            if (on_error) {
                MoonrakerError err =
                    MoonrakerError::connection_lost("get_power_devices", "HTTP request failed");
                on_error(err);
            }
            return;
        }

        if (resp->status_code != 200) {
            spdlog::error("[Moonraker API] Power devices request failed: HTTP {}",
                          static_cast<int>(resp->status_code));
            if (on_error) {
                MoonrakerError err = MoonrakerError::http_status_error(
                    "get_power_devices", static_cast<int>(resp->status_code));
                on_error(err);
            }
            return;
        }

        // Parse JSON response
        try {
            json j = json::parse(resp->body);
            std::vector<PowerDevice> devices;

            if (j.contains("result") && j["result"].contains("devices")) {
                for (const auto& info : j["result"]["devices"]) {
                    PowerDevice dev;
                    dev.device = info.value("device", "");
                    dev.type = info.value("type", "unknown");
                    dev.status = info.value("status", "off");
                    dev.locked_while_printing = info.value("locked_while_printing", false);
                    if (!dev.device.empty()) {
                        devices.push_back(dev);
                    }
                }
            }

            spdlog::info("[Moonraker API] Found {} power devices", devices.size());
            if (on_success) {
                on_success(devices);
            }
        } catch (const std::exception& e) {
            spdlog::error("[Moonraker API] Failed to parse power devices: {}", e.what());
            if (on_error) {
                MoonrakerError err = MoonrakerError::unknown(e.what(), "get_power_devices");
                on_error(err);
            }
        }
    });
}

void MoonrakerAPI::set_device_power(const std::string& device, const std::string& action,
                                    SuccessCallback on_success, ErrorCallback on_error) {
    // Validate device name
    if (!is_safe_identifier(device)) {
        spdlog::error("[Moonraker API] Invalid device name: {}", device);
        if (on_error) {
            MoonrakerError err =
                MoonrakerError::validation_error("set_device_power", "Invalid device name");
            on_error(err);
        }
        return;
    }

    // Validate action
    if (action != "on" && action != "off" && action != "toggle") {
        spdlog::error("[Moonraker API] Invalid power action: {}", action);
        if (on_error) {
            MoonrakerError err = MoonrakerError::validation_error(
                "set_device_power", "Invalid action (must be on, off, or toggle)");
            on_error(err);
        }
        return;
    }

    if (http_base_url_.empty()) {
        spdlog::error("[Moonraker API] HTTP base URL not configured for power device control");
        if (on_error) {
            MoonrakerError err =
                MoonrakerError::connection_lost("set_device_power", "Not connected to Moonraker");
            on_error(err);
        }
        return;
    }

    // URL-encode device name (spaces, special chars) for safe query param
    std::string encoded_device;
    for (char c : device) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == '.') {
            encoded_device += c;
        } else {
            char buf[4];
            std::snprintf(buf, sizeof(buf), "%%%02X", static_cast<unsigned char>(c));
            encoded_device += buf;
        }
    }

    // Build URL with query params
    std::string url = http_base_url_ + "/machine/device_power/device?device=" + encoded_device +
                      "&action=" + action;

    spdlog::info("[Moonraker API] Setting power device '{}' to '{}'", device, action);

    helix::http::HttpExecutor::fast().submit([url, device, action, on_success, on_error]() {
        auto resp = requests::post(url.c_str(), "");

        if (!resp) {
            spdlog::error("[Moonraker API] HTTP request failed for power device");
            if (on_error) {
                MoonrakerError err =
                    MoonrakerError::connection_lost("set_device_power", "HTTP request failed");
                on_error(err);
            }
            return;
        }

        if (resp->status_code != 200) {
            spdlog::error("[Moonraker API] Power device command failed: HTTP {}",
                          static_cast<int>(resp->status_code));
            if (on_error) {
                MoonrakerError err = MoonrakerError::http_status_error(
                    "set_device_power", static_cast<int>(resp->status_code));
                on_error(err);
            }
            return;
        }

        spdlog::info("[Moonraker API] Power device '{}' set to '{}' successfully", device, action);
        if (on_success) {
            on_success();
        }
    });
}

// ============================================================================
// Sensor Operations
// ============================================================================

void MoonrakerAPI::get_sensors(SensorsCallback on_success, ErrorCallback on_error) {
    client_.send_jsonrpc(
        "server.sensors.list", json::object(),
        [on_success](json response) {
            std::vector<helix::SensorInfo> sensors;
            nlohmann::json initial_values;

            if (response.contains("result") && response["result"].contains("sensors") &&
                response["result"]["sensors"].is_object()) {
                for (auto it = response["result"]["sensors"].begin();
                     it != response["result"]["sensors"].end(); ++it) {
                    const std::string& sensor_id = it.key();
                    const auto& sensor_data = it.value();
                    if (!sensor_data.is_object())
                        continue;

                    helix::SensorInfo info;
                    info.id = sensor_id;
                    info.friendly_name = sensor_data.value("friendly_name", sensor_id);
                    info.type = sensor_data.value("type", "unknown");

                    // Extract value keys and initial values from the "values" object
                    if (sensor_data.contains("values") && sensor_data["values"].is_object()) {
                        nlohmann::json sensor_values;
                        for (auto vit = sensor_data["values"].begin();
                             vit != sensor_data["values"].end(); ++vit) {
                            info.value_keys.push_back(vit.key());
                            if (vit.value().is_object() && vit.value().contains("value") &&
                                vit.value()["value"].is_number()) {
                                sensor_values[vit.key()] = vit.value()["value"].get<double>();
                            }
                        }
                        if (!sensor_values.empty()) {
                            initial_values[sensor_id] = std::move(sensor_values);
                        }
                    }

                    if (!info.id.empty()) {
                        sensors.push_back(std::move(info));
                    }
                }
            }

            spdlog::info("[Moonraker API] Found {} sensors", sensors.size());
            if (on_success) {
                on_success(sensors, initial_values);
            }
        },
        [on_error](const MoonrakerError& err) {
            spdlog::debug("[Moonraker API] Sensor list failed: {}", err.message);
            if (on_error) {
                on_error(err);
            }
        },
        0,     // default timeout
        true); // silent — sensors are optional
}

// ============================================================================
// System Control Operations
// ============================================================================

void MoonrakerAPI::execute_gcode(const std::string& gcode, SuccessCallback on_success,
                                 ErrorCallback on_error, uint32_t timeout_ms, bool silent,
                                 GcodeSource source, SuccessCallback on_queued) {
    // Provenance-comment policy: tag HelixScreen-initiated commands so they're
    // traceable in Klipper logs, but never tag user-typed console input, and
    // never tag anything when the printer's firmware re-echoes received G-code
    // (AD5X: the echoed quoted RESPOND MSG="..." would be mis-parsed by Klipper).
    const bool add_comment =
        (source == GcodeSource::Internal) && !state_.firmware_echoes_gcode();
    // Refuse to ship gcode while Klipper is halted. The user-visible bug this
    // closes: dragging the fan slider on a K2 with `key298` produced a stream
    // of M106 commands that Klipper rejected on each tick (see /server/gcode_store
    // history during the 2026-05-05 K2 incident). Recovery paths use dedicated
    // RPCs (printer.firmware_restart, printer.emergency_stop, machine.services.restart)
    // plus local fork+exec of helix-recover.sh, all of which bypass this gate
    // naturally. STARTUP is allowed through — it's brief and Klipper queues
    // incoming commands.
    {
        const int klippy = lv_subject_get_int(state_.get_klippy_state_subject());
        if (klippy == static_cast<int>(helix::KlippyState::SHUTDOWN) ||
            klippy == static_cast<int>(helix::KlippyState::ERROR)) {
            if (!silent) {
                spdlog::warn("[Moonraker API] Refusing G-code while Klipper is halted "
                             "(state={}): '{}'",
                             klippy, gcode.substr(0, 60));
            }
            if (on_error) {
                on_error(MoonrakerError::not_ready(
                    "printer.gcode.script", "Klipper is halted — restart firmware to continue"));
            }
            return;
        }
    }

    // Refuse app-initiated homing while a print is active. On loadcell-Z printers
    // (AD5X: G28 probes the nozzle DOWN into the bed) a mid-print home drives the
    // nozzle into the part -> collision -> ZMOD ZCONTROL_AUTO trip -> Klipper down.
    // "Active" = PRINTING or PAUSED (the head is parked over the print in both).
    // Only literal homing is blocked; all other gcode (including recovery) passes.
    if (helix::api::reject_homing_during_active_print(gcode, state_, silent, on_error,
                                                      "[Moonraker API]")) {
        return;
    }

    // Gate discretionary gcode (fan, temp, non-homing moves, LED) while a blocking
    // non-print operation holds Klipper's single-threaded gcode lock (homing,
    // BED_MESH_CALIBRATE, QGL, PROBE_ACCURACY, manual probe). Split by danger below:
    // a physical MOVE is refused (a late jog is dangerous); benign fan/temp/LED are
    // queued fire-and-forget with a single per-episode toast rather than lost or
    // timed out (bundle 7CT79XXK, Sovol SV08 calibration; #1108). Recovery, homing,
    // probe-control (TESTZ/ACCEPT/ABORT) and macros are never discretionary, so they
    // pass. Real file prints are excluded by is_blocking_operation_active(). Self-busy
    // from the app's own recent jog passes too (idle_timeout reports "Printing"
    // during any move); only external blocking ops are gated.
    if (helix::is_discretionary_gcode(gcode) && state_.is_external_blocking_operation_active()) {
        // A physical MOVE must never queue behind the blocking op: a jog that fires
        // minutes late, after the user has walked away, can crash the toolhead.
        // Refuse it up front (recovery/homing are non-discretionary and never reach
        // here). #1108: the reporter agrees the hard block on motion is correct.
        if (helix::gcode_contains_move(gcode)) {
            if (!silent) {
                spdlog::warn("[Moonraker API] Refusing motion G-code while printer is "
                             "homing/leveling: '{}'",
                             gcode.substr(0, 60));
            }
            if (on_error) {
                on_error(MoonrakerError::not_ready("printer.gcode.script",
                                                   "Printer is busy — try again in a moment"));
            }
            return;
        }

        // Benign discretionary command (fan / temp / LED): let Klipper queue it
        // behind the blocking op — it runs harmlessly the moment the gcode lock
        // frees, which is what every other frontend does. Send fire-and-forget
        // (silent, no callbacks) so the queued command's inevitable ~60s response
        // wait never surfaces a scary timeout toast (bundle 7CT79XXK). Instead
        // announce it ONCE per blocking episode so a late-firing change isn't a
        // surprise. #1108.
        //
        // Settling the caller needs its own disposition — it is neither success
        // nor error.
        //
        // Callers may hold an in-flight counter keyed to the callback pair and
        // wedge if neither fires: LedController::note_command_dispatched() bumps a
        // counter at dispatch and only decrements it from on_success/on_error,
        // driving the `led_command_in_flight` subject the light buttons disable on.
        // Dropping both left that counter stuck at >=1 until the printer
        // disconnected, greying the buttons out for the whole session (#1129).
        //
        // But on_success cannot be the settle. For every other caller on_success
        // means "the printer did it", and acting on that here is a lie the user
        // sees: cooldown sends SET_HEATER_TEMPERATURE TARGET=0 with
        // on_success = NOTIFY_SUCCESS("Heaters off"), and temperature_service pairs
        // its "target set to N°C" toast with a go_back() that closes the overlay —
        // all while the heaters sit at target for the rest of the calibration.
        // claim_busy_queue_toast() is once per episode, so every command after the
        // first in a burst would show only the false success with no caveat.
        //
        // So the settle goes to on_queued, an explicit opt-in that means "accepted
        // for later execution", nothing more. Callers that do not opt in get
        // exactly the #1108 behaviour: fire-and-forget, no callback. What is
        // genuinely dropped either way is the RPC *response* — a late rejection
        // (e.g. a macro emitting an out-of-range target) surfaces no error, same as
        // the silent-queue frontends.
        if (state_.claim_busy_queue_toast()) {
            NOTIFY_INFO("Printer is busy — your {} will run when it's ready.",
                        helix::discretionary_gcode_noun(gcode));
        }
        std::string queued = helix::api::annotate_gcode(gcode, add_comment);
        json queued_params = {{"script", queued}};
        spdlog::debug("[Moonraker API] Queuing discretionary G-code behind blocking op: {}",
                      queued);
        client_.send_jsonrpc("printer.gcode.script", queued_params, nullptr, nullptr, timeout_ms,
                             /*silent=*/true);
        // Settle the CALLER directly, not via the RPC response — waiting on the
        // response is exactly the ~60s timeout toast #1108 removed. NOTE: this runs
        // synchronously on the calling thread, not the libhv response thread, so it
        // is typically inside an LVGL LV_EVENT_CLICKED frame. A handler here must
        // not delete widgets synchronously (see the on_queued docs in
        // moonraker_api.h and CLAUDE.md § Threading).
        if (on_queued) {
            on_queued();
        }
        return;
    }

    std::string annotated = helix::api::annotate_gcode(gcode, add_comment);
    json params = {{"script", annotated}};

    spdlog::trace("[Moonraker API] Executing G-code: {}", annotated);

    // Guard: only wrap on_success in lambda if non-null, otherwise pass nullptr.
    // A lambda wrapping a null std::function would bypass send_jsonrpc's null check
    // and throw bad_function_call when invoked.
    std::function<void(const json&)> success_wrapper;
    if (on_success) {
        success_wrapper = [on_success](json) { on_success(); };
    }
    client_.send_jsonrpc("printer.gcode.script", params, std::move(success_wrapper), on_error,
                         timeout_ms, silent);
}

bool MoonrakerAPI::is_safe_gcode_param(const std::string& str) {
    return moonraker_internal::is_safe_identifier(str);
}

// ============================================================================
// Object Exclusion Operations
// ============================================================================

void MoonrakerAPI::exclude_object(const std::string& object_name, SuccessCallback on_success,
                                  ErrorCallback on_error) {
    // Validate object name to prevent G-code injection. Uses the object-name allowlist
    // (permits `. - : ( ) [ ]`, no whitespace) because slicer-generated names routinely
    // contain these characters and the stricter `is_safe_identifier()` rejected legitimate
    // names. Log the offending name so operators can diagnose character-class regressions.
    if (!is_safe_object_name(object_name)) {
        spdlog::warn("[Moonraker API] Rejected exclude_object name '{}' — contains characters "
                     "outside the object-name allowlist",
                     object_name);
        NOTIFY_ERROR("Invalid object name '{}'. Contains unsafe characters.", object_name);
        if (on_error) {
            MoonrakerError err = MoonrakerError::validation_error(
                "exclude_object", "Invalid object name contains illegal characters");
            on_error(err);
        }
        return;
    }

    std::ostringstream gcode;
    gcode << "EXCLUDE_OBJECT NAME=" << object_name;

    spdlog::info("[Moonraker API] Excluding object: {}", object_name);

    // Klipper's printer.gcode.script does not return until the script actually executes.
    // During pre-print (heating/homing/purge), EXCLUDE_OBJECT can sit queued for many
    // minutes. The default 60s RPC timeout would fire spuriously and surface a scary
    // "timed out" toast even though Klipper will eventually run the command and exclude
    // the object. Pass silent=true so any late timeout does not emit a user-facing event;
    // truth comes from the `exclude_object.excluded_objects` status subscription. A 15-minute
    // ceiling still catches genuinely stuck requests without aborting legitimate pre-prints.
    constexpr uint32_t EXCLUDE_OBJECT_TIMEOUT_MS = 15 * 60 * 1000;
    execute_gcode(gcode.str(), on_success, on_error, EXCLUDE_OBJECT_TIMEOUT_MS, /*silent=*/true);
}

void MoonrakerAPI::emergency_stop(SuccessCallback on_success, ErrorCallback on_error) {
    spdlog::warn("[Moonraker API] Emergency stop requested!");

    client_.send_jsonrpc(
        "printer.emergency_stop", json::object(),
        [on_success](json) {
            spdlog::info("[Moonraker API] Emergency stop executed");
            on_success();
        },
        on_error);
}

void MoonrakerAPI::restart_firmware(SuccessCallback on_success, ErrorCallback on_error) {
    spdlog::info("[Moonraker API] Restarting firmware");

    client_.send_jsonrpc(
        "printer.firmware_restart", json::object(),
        [on_success](json) {
            spdlog::info("[Moonraker API] Firmware restart initiated");
            on_success();
        },
        on_error);
}

void MoonrakerAPI::restart_klipper(SuccessCallback on_success, ErrorCallback on_error) {
    spdlog::info("[Moonraker API] Restarting Klipper");

    client_.send_jsonrpc(
        "printer.restart", json::object(),
        [on_success](json) {
            spdlog::info("[Moonraker API] Klipper restart initiated");
            on_success();
        },
        on_error);
}

void MoonrakerAPI::restart_service(const std::string& service_name, SuccessCallback on_success,
                                   ErrorCallback on_error) {
    spdlog::info("[Moonraker API] Restarting service '{}' via machine.services.restart",
                 service_name);

    client_.send_jsonrpc(
        "machine.services.restart", json{{"service", service_name}},
        [on_success, service_name](json) {
            spdlog::info("[Moonraker API] Service '{}' restart initiated", service_name);
            on_success();
        },
        on_error);
}

void MoonrakerAPI::restart_moonraker(SuccessCallback on_success, ErrorCallback on_error) {
    spdlog::info("[Moonraker API] Restarting Moonraker");

    client_.send_jsonrpc(
        "server.restart", json::object(),
        [on_success](json) {
            spdlog::info("[Moonraker API] Moonraker restart initiated");
            if (on_success)
                on_success();
        },
        on_error);
}

void MoonrakerAPI::machine_shutdown(SuccessCallback on_success, ErrorCallback on_error) {
    spdlog::info("[Moonraker API] Shutting down host machine");

    client_.send_jsonrpc(
        "machine.shutdown", json::object(),
        [on_success](json) {
            spdlog::info("[Moonraker API] Host shutdown initiated");
            if (on_success)
                on_success();
        },
        on_error);
}

void MoonrakerAPI::machine_reboot(SuccessCallback on_success, ErrorCallback on_error) {
    spdlog::info("[Moonraker API] Rebooting host machine");

    client_.send_jsonrpc(
        "machine.reboot", json::object(),
        [on_success](json) {
            spdlog::info("[Moonraker API] Host reboot initiated");
            if (on_success)
                on_success();
        },
        on_error);
}

// ============================================================================
// Safety Limits Configuration
// ============================================================================

void MoonrakerAPI::update_safety_limits_from_printer(SuccessCallback on_success,
                                                     ErrorCallback on_error) {
    // Only update if limits haven't been explicitly set
    if (limits_explicitly_set_) {
        spdlog::debug("[Moonraker API] Safety limits explicitly configured, skipping Moonraker "
                      "auto-detection");
        if (on_success) {
            on_success();
        }
        return;
    }

    // Query printer configuration for safety limits
    json params = {{"objects", json::object({{"configfile", json::array({"settings"})}})}};

    client_.send_jsonrpc(
        "printer.objects.query", params,
        [this, on_success](json response) {
            try {
                if (!response.contains("result") || !response["result"].contains("status") ||
                    !response["result"]["status"].contains("configfile") ||
                    !response["result"]["status"]["configfile"].contains("settings")) {
                    spdlog::warn("[Moonraker API] Printer configuration not available, using "
                                 "default safety limits");
                    if (on_success) {
                        on_success();
                    }
                    return;
                }

                const json& settings = response["result"]["status"]["configfile"]["settings"];
                bool updated = false;

                // Extract max_velocity from printer settings.
                // Every read in this callback is is_number()-guarded on purpose: the
                // whole body is a single try, so one wrong-typed field would abort
                // every limit parsed after it (see the position_endstop note below).
                if (settings.contains("printer") && settings["printer"].is_object() &&
                    settings["printer"].contains("max_velocity") &&
                    settings["printer"]["max_velocity"].is_number()) {
                    double max_velocity_mm_s = settings["printer"]["max_velocity"].get<double>();
                    safety_limits_.max_feedrate_mm_min = max_velocity_mm_s * 60.0;
                    updated = true;
                    spdlog::debug(
                        "[Moonraker API] Updated max_feedrate from printer config: {} mm/min",
                        safety_limits_.max_feedrate_mm_min);
                }

                // Extract axis limits from stepper configurations
                // Also populate build_volume from stepper_x/y for accurate bed dimensions
                BuildVolume build_vol = hardware().build_volume();
                bool build_volume_updated = false;

                for (const auto& stepper : {"stepper_x", "stepper_y", "stepper_z"}) {
                    if (settings.contains(stepper) && settings[stepper].is_object()) {
                        if (settings[stepper].contains("position_max") &&
                            settings[stepper]["position_max"].is_number()) {
                            double pos_max = settings[stepper]["position_max"].get<double>();
                            // Use the largest axis max as absolute position limit
                            if (pos_max > safety_limits_.max_absolute_position_mm) {
                                safety_limits_.max_absolute_position_mm = pos_max;
                                updated = true;
                            }
                            // Update build_volume for X/Y axes
                            if (std::string(stepper) == "stepper_x") {
                                build_vol.x_max = static_cast<float>(pos_max);
                                build_volume_updated = true;
                            } else if (std::string(stepper) == "stepper_y") {
                                build_vol.y_max = static_cast<float>(pos_max);
                                build_volume_updated = true;
                            } else if (std::string(stepper) == "stepper_z") {
                                build_vol.z_max = static_cast<float>(pos_max);
                                build_volume_updated = true;
                            }
                        }
                        if (settings[stepper].contains("position_min") &&
                            settings[stepper]["position_min"].is_number()) {
                            double pos_min = settings[stepper]["position_min"].get<double>();
                            // Use the smallest (most negative) axis min as absolute position limit
                            if (pos_min < safety_limits_.min_absolute_position_mm) {
                                safety_limits_.min_absolute_position_mm = pos_min;
                                updated = true;
                            }
                            // Update build_volume for X/Y axes
                            if (std::string(stepper) == "stepper_x") {
                                build_vol.x_min = static_cast<float>(pos_min);
                                build_volume_updated = true;
                            } else if (std::string(stepper) == "stepper_y") {
                                build_vol.y_min = static_cast<float>(pos_min);
                                build_volume_updated = true;
                            }
                        }
                    }
                }

                // Update build_volume if we found stepper configs
                if (build_volume_updated) {
                    hardware().set_build_volume(build_vol);
                    notify_build_volume_changed();
                    spdlog::debug("[Moonraker API] Build volume from stepper config: "
                                  "X[{:.0f},{:.0f}] Y[{:.0f},{:.0f}] Z[0,{:.0f}]",
                                  build_vol.x_min, build_vol.x_max, build_vol.y_min,
                                  build_vol.y_max, build_vol.z_max);
                }

                // Extract stepper_z position_endstop for non-probe Z-offset reference.
                //
                // Klipper emits `position_endstop: null` for any printer using
                // `endstop_pin: probe:z_virtual_endstop` — i.e. most probe-equipped
                // machines. is_number() is a PRESENCE test, not a default: 0.0 is a
                // legal endstop, so there is no value we could substitute that the
                // reader could tell apart from a real reading. Leaving the setter
                // uncalled keeps stepper_z_endstop_microns_ at its "not set" 0.
                //
                // The guard is load-bearing: a bare .get<double>() throws
                // type_error.302 on that null, and since the whole callback is one
                // try block the throw would skip the temperature-limit loop below —
                // max/min temp and min_extrude_temp would silently keep their
                // compiled defaults while the catch still reported success.
                if (settings.contains("stepper_z") && settings["stepper_z"].is_object() &&
                    settings["stepper_z"].contains("position_endstop")) {
                    const json& endstop_val = settings["stepper_z"]["position_endstop"];
                    if (endstop_val.is_number()) {
                        double endstop = endstop_val.get<double>();
                        int microns = static_cast<int>(endstop * 1000.0);
                        state_.set_stepper_z_endstop_microns(microns);
                        spdlog::debug(
                            "[Moonraker API] stepper_z position_endstop: {:.3f}mm ({} microns)",
                            endstop, microns);
                    } else {
                        spdlog::debug("[Moonraker API] stepper_z position_endstop is not a "
                                      "number (virtual endstop / probe) — leaving unset");
                    }
                }

                // Extract temperature limits from heater configurations
                for (const auto& [key, value] : settings.items()) {
                    if ((key.find("extruder") != std::string::npos ||
                         key.find("heater_") != std::string::npos) &&
                        value.is_object()) {
                        if (value.contains("max_temp") && value["max_temp"].is_number()) {
                            double max_temp = value["max_temp"].get<double>();
                            // Use the highest heater max_temp as temperature limit
                            if (max_temp > safety_limits_.max_temperature_celsius) {
                                safety_limits_.max_temperature_celsius = max_temp;
                                updated = true;
                            }
                        }
                        if (value.contains("min_temp") && value["min_temp"].is_number()) {
                            double min_temp = value["min_temp"].get<double>();
                            // Use the lowest heater min_temp as temperature limit
                            if (min_temp < safety_limits_.min_temperature_celsius) {
                                safety_limits_.min_temperature_celsius = min_temp;
                                updated = true;
                            }
                        }
                        // Extract min_extrude_temp from extruder (not heater_bed)
                        if (key == "extruder" && value.contains("min_extrude_temp") &&
                            value["min_extrude_temp"].is_number()) {
                            double min_extrude = value["min_extrude_temp"].get<double>();
                            safety_limits_.min_extrude_temp_celsius = min_extrude;
                            updated = true;
                            spdlog::debug("[Moonraker API] min_extrude_temp from config: {}°C",
                                          min_extrude);
                        }
                    }
                }

                if (updated) {
                    spdlog::debug(
                        "[Moonraker API] Updated safety limits from printer configuration:");
                    spdlog::debug("[Moonraker API]   Temperature: {} to {}°C",
                                  safety_limits_.min_temperature_celsius,
                                  safety_limits_.max_temperature_celsius);
                    spdlog::debug("[Moonraker API]   Position: {} to {}mm",
                                  safety_limits_.min_absolute_position_mm,
                                  safety_limits_.max_absolute_position_mm);
                    spdlog::debug("[Moonraker API]   Feedrate: {} to {} mm/min",
                                  safety_limits_.min_feedrate_mm_min,
                                  safety_limits_.max_feedrate_mm_min);
                } else {
                    spdlog::debug("[Moonraker API] No safety limit overrides found in printer "
                                  "config, using defaults");
                }

                if (on_success) {
                    on_success();
                }
            } catch (const std::exception& e) {
                LOG_ERROR_INTERNAL("Failed to parse printer configuration for safety limits: {}",
                                   e.what());
                if (on_success) {
                    on_success(); // Continue with defaults on parse error
                }
            }
        },
        on_error);
}
