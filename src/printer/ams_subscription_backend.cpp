// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ams_subscription_backend.h"

#include "moonraker_error.h"
#include "printer_state.h"

#include "hv/json.hpp"

AmsSubscriptionBackend::AmsSubscriptionBackend(MoonrakerAPI* api, helix::MoonrakerClient* client)
    : api_(api), client_(client) {
    // Common defaults -- derived constructors set type-specific fields
    system_info_.version = "unknown";
    system_info_.current_tool = -1;
    system_info_.current_slot = -1;
    system_info_.filament_loaded = false;
    system_info_.action = AmsAction::IDLE;
    system_info_.total_slots = 0;
}

AmsSubscriptionBackend::~AmsSubscriptionBackend() {
    // Release without unsubscribe -- MoonrakerClient may already be destroyed
    subscription_.release();
}

AmsError AmsSubscriptionBackend::start() {
    bool should_emit = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (running_) {
            return AmsErrorHelper::success();
        }

        if (!client_) {
            spdlog::error("{} Cannot start: MoonrakerClient is null", backend_log_tag());
            return AmsErrorHelper::not_connected("MoonrakerClient not provided");
        }

        if (!api_) {
            spdlog::error("{} Cannot start: MoonrakerAPI is null", backend_log_tag());
            return AmsErrorHelper::not_connected("MoonrakerAPI not provided");
        }

        // Derived class extra checks (e.g., ToolChanger requires tools discovered)
        auto extra_check = additional_start_checks();
        if (!extra_check.success()) {
            return extra_check;
        }

        helix::SubscriptionId id = client_->register_notify_update(
            [this, token = lifetime_.token()](const nlohmann::json& notification) {
                // L081 Mechanism C: handle_status_update mutates members + emits events.
                // High-volume WS notify path: every status frame goes through queue_update,
                // matching the rest of printer state which is already main-thread-marshaled.
                // First-fire baseline state (initial subscription frame from Klipper) used
                // to need defer_critical to survive the splash→home scoped_freeze(); under
                // the new buffer-not-drop semantics, plain defer is sufficient — buffered
                // callbacks splice back into pending_ when the freeze releases.
                token.defer("AmsSubscriptionBackend::notify_update",
                            [this, notification]() { handle_status_update(notification); });
            });

        if (id == helix::INVALID_SUBSCRIPTION_ID) {
            spdlog::error("{} Failed to register for status updates", backend_log_tag());
            return AmsErrorHelper::not_connected("Failed to subscribe to Moonraker updates");
        }

        subscription_ = SubscriptionGuard(client_, id);
        running_ = true;
        spdlog::info("{} Backend started, subscription ID: {}", backend_log_tag(), id);
        should_emit = true;
    }

    // Emit initial state event OUTSIDE the lock to avoid deadlock
    if (should_emit) {
        emit_event(EVENT_STATE_CHANGED);
    }

    // Derived class post-start work (version detection, config loading, etc.)
    on_started();

    return AmsErrorHelper::success();
}

void AmsSubscriptionBackend::stop() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!running_) {
        return;
    }
    on_stopping();
    subscription_.reset();
    running_ = false;
    spdlog::info("{} Backend stopped", backend_log_tag());
}

void AmsSubscriptionBackend::release_subscriptions() {
    subscription_.release();
}

bool AmsSubscriptionBackend::is_running() const {
    return running_;
}

void AmsSubscriptionBackend::set_event_callback(EventCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    event_callback_ = std::move(callback);
}

void AmsSubscriptionBackend::emit_event(const std::string& event, const std::string& data) {
    EventCallback cb;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        cb = event_callback_;
    }
    if (cb) {
        cb(event, data);
    }
}

AmsAction AmsSubscriptionBackend::get_current_action() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return system_info_.action;
}

int AmsSubscriptionBackend::get_current_tool() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return system_info_.current_tool;
}

int AmsSubscriptionBackend::get_current_slot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return system_info_.current_slot;
}

bool AmsSubscriptionBackend::is_filament_loaded() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return system_info_.filament_loaded;
}

AmsError AmsSubscriptionBackend::check_preconditions(bool requires_toolhead_motion) const {
    if (!running_) {
        return AmsErrorHelper::not_connected(std::string(backend_log_tag()) +
                                             " backend not started");
    }
    if (system_info_.is_busy()) {
        return AmsErrorHelper::busy(ams_action_to_string(system_info_.action));
    }
    // Toolhead-motion ops (load/unload/tool-change) additionally refuse while a
    // print is active — no-motion ops (eject_lane, select, unlock) pass false.
    if (requires_toolhead_motion) {
        if (auto e = refuse_if_printing(); !e.success()) {
            return e;
        }
    }
    return AmsErrorHelper::success();
}

AmsError AmsSubscriptionBackend::refuse_if_printing() const {
    // Refuse toolhead-motion filament ops while a print OWNS the toolhead.
    //
    // PRINTING is always refused: the nozzle is laying plastic and any filament
    // move collides with the job.
    //
    // PAUSED splits on filament_ops_self_home(). Pause-then-swap is the runout /
    // colour-change recovery workflow — pause_resume has saved the gcode state,
    // the job resumes where it left off, and this is exactly what a user does
    // from Mainsail. Refusing it universally made HelixScreen the only surface
    // that could not perform the recovery Klipper had just asked for. What still
    // has to be refused is a backend whose firmware macro homes ITSELF: on the
    // loadcell-Z AD5X, `_IFS_REMOVE_CURRENT_PRUTOK` runs a buried `_G28` that
    // probes the nozzle down into the part, tripping ZMOD's ZCONTROL_AUTO into a
    // Klipper shutdown (bundle XWPBR2DX, commit 329e731e9). Layer 1's gcode-send
    // guard never sees that `_G28`, so it must be stopped here, before the op.
    //
    // Relaxing PAUSED does NOT widen what homing can reach the printer:
    // helix::api::reject_homing_during_active_print() still refuses every
    // app-emitted G28 while PRINTING or PAUSED, and ensure_homed_then() only
    // emits one when toolhead.homed_axes lacks "xyz" — which a paused print,
    // homed by construction, never does.
    //
    // api_ can be null in unit tests / cold-boot; when it is, print state is
    // unknown and we do not block (mirrors ensure_homed_then's null-client path).
    if (!api_) {
        return AmsErrorHelper::success();
    }
    const helix::PrintJobState pstate = api_->printer_state().get_print_job_state();
    if (!helix::print_occupies_toolhead(pstate)) {
        return AmsErrorHelper::success();
    }
    const bool is_paused = (pstate == helix::PrintJobState::PAUSED);
    const bool self_homes = filament_ops_self_home();
    if (is_paused && !self_homes) {
        spdlog::info("{} Allowing filament operation on a PAUSED print "
                     "(backend does not self-home; Layer 1 still blocks any G28)",
                     backend_log_tag());
        return AmsErrorHelper::success();
    }
    spdlog::warn("{} Refusing filament operation while a print is active (state={}, self_homes={})",
                 backend_log_tag(), static_cast<int>(pstate), self_homes);
    // A non-self-homing backend that reaches here is PRINTING, and pausing is a
    // recovery it can actually offer — say so instead of "finish or cancel".
    return AmsErrorHelper::print_active(is_paused, /*pause_allows_ops=*/!self_homes);
}

AmsError AmsSubscriptionBackend::ensure_homed_then(std::string gcode,
                                                   std::function<void()> on_complete) {
    // When no completion callback is wanted, route the final gcode through the
    // 1-arg execute_gcode so subclasses that override only that form (test
    // fixtures, AFC/ACE/CFS) keep capturing it — exact legacy behavior.
    if (!client_) {
        spdlog::debug("{} No client for homing query, executing directly", backend_log_tag());
        return on_complete ? execute_gcode(gcode, std::move(on_complete)) : execute_gcode(gcode);
    }

    auto token = lifetime_.token();
    auto gcode_copy = std::move(gcode);
    client_->send_jsonrpc(
        "printer.objects.query", json{{"objects", json{{"toolhead", json::array({"homed_axes"})}}}},
        [this, token, gcode_copy, on_complete](const json& response) {
            // L081 Mechanism C: this branches into api_->execute_gcode() (member access)
            // and execute_gcode() (member call); marshal to main.
            token.defer("AmsSubscriptionBackend::ensure_homed_then_query_success", [this, token,
                                                                                    gcode_copy,
                                                                                    response,
                                                                                    on_complete]() {
                bool needs_home = true;
                if (response.contains("result") && response["result"].contains("status")) {
                    const auto& status = response["result"]["status"];
                    if (status.contains("toolhead") && status["toolhead"].contains("homed_axes") &&
                        status["toolhead"]["homed_axes"].is_string()) {
                        std::string axes = status["toolhead"]["homed_axes"].get<std::string>();
                        needs_home = (axes.find("xyz") == std::string::npos);
                    }
                }

                if (needs_home) {
                    spdlog::info("{} Not homed, sending G28 before operation", backend_log_tag());
                    api_->execute_gcode(
                        "G28",
                        [this, token, gcode_copy, on_complete]() {
                            // L081 Mechanism C: execute_gcode touches api_/members.
                            token.defer("AmsSubscriptionBackend::ensure_homed_then_g28_success",
                                        [this, gcode_copy, on_complete]() {
                                            spdlog::info("{} Homing complete, proceeding with: {}",
                                                         backend_log_tag(), gcode_copy);
                                            if (on_complete) {
                                                execute_gcode(gcode_copy, on_complete);
                                            } else {
                                                execute_gcode(gcode_copy);
                                            }
                                        });
                        },
                        [this, token](const MoonrakerError& err) {
                            // L081 Mechanism C: system_info_ write under lock.
                            token.defer("AmsSubscriptionBackend::ensure_homed_then_g28_error",
                                        [this, message = err.message]() {
                                            spdlog::error("{} Homing failed: {}", backend_log_tag(),
                                                          message);
                                            std::lock_guard<std::mutex> lock(mutex_);
                                            system_info_.action = AmsAction::IDLE;
                                        });
                        },
                        MoonrakerAPI::HOMING_TIMEOUT_MS);
                } else if (on_complete) {
                    execute_gcode(gcode_copy, on_complete);
                } else {
                    execute_gcode(gcode_copy);
                }
            });
        },
        [this, token](const MoonrakerError& err) {
            // L081 Mechanism C: system_info_ write under lock.
            token.defer("AmsSubscriptionBackend::ensure_homed_then_query_error",
                        [this, message = err.message]() {
                            spdlog::error("{} Homed axes query failed: {}", backend_log_tag(),
                                          message);
                            std::lock_guard<std::mutex> lock(mutex_);
                            system_info_.action = AmsAction::IDLE;
                        });
        });

    return AmsErrorHelper::success();
}

AmsError AmsSubscriptionBackend::execute_gcode(const std::string& gcode) {
    if (!api_) {
        return AmsErrorHelper::not_connected("MoonrakerAPI not available");
    }
    const char* tag = backend_log_tag();
    spdlog::info("{} Executing G-code: {}", tag, gcode);
    api_->execute_gcode(
        gcode, [tag]() { spdlog::debug("{} G-code executed successfully", tag); },
        [tag, gcode](const MoonrakerError& err) {
            if (err.type == MoonrakerErrorType::TIMEOUT) {
                spdlog::warn("{} G-code response timed out (may still be running): {}", tag, gcode);
            } else if (err.type == MoonrakerErrorType::NOT_READY) {
                // MoonrakerAPI already logs a [warning] when refusing g-code on a halted
                // Klipper. AD5X-IFS retries _IFS_VARS on every Adventurer5M.json poll, so
                // duplicating at [error] floods the log post-halt.
                spdlog::debug("{} G-code skipped (Klipper halted): {}", tag, gcode);
            } else {
                spdlog::error("{} G-code failed: {} - {}", tag, gcode, err.message);
            }
        },
        MoonrakerAPI::AMS_OPERATION_TIMEOUT_MS,
        // silent=true: the phase tracker + IFS_STATUS own operation completion,
        // so the RPC timeout is advisory for these long macros. A cold-start
        // load can legitimately run past the 300s ceiling (bundle 77TDH9N6:
        // heat-from-cold + load + double purge + clean), completing fine ~47s
        // later — silent suppresses ONLY the false REQUEST_TIMEOUT toast. The
        // error_cb above still logs "may still be running", and genuine Klipper
        // RPC-error toasts stay suppressed anyway via the error_cb
        // caller_handles_ui path in the request tracker.
        /*silent=*/true);
    return AmsErrorHelper::success();
}

AmsError AmsSubscriptionBackend::execute_gcode(const std::string& gcode,
                                               std::function<void()> on_complete) {
    if (!api_) {
        return AmsErrorHelper::not_connected("MoonrakerAPI not available");
    }
    const char* tag = backend_log_tag();
    spdlog::info("{} Executing G-code: {}", tag, gcode);
    api_->execute_gcode(
        gcode,
        [tag, on_complete = std::move(on_complete)]() {
            spdlog::debug("{} G-code executed successfully", tag);
            // Fires when the command finishes (Klipper acks the script), so a
            // long macro has fully run — the reliable completion signal.
            if (on_complete) {
                on_complete();
            }
        },
        [tag, gcode](const MoonrakerError& err) {
            if (err.type == MoonrakerErrorType::TIMEOUT) {
                spdlog::warn("{} G-code response timed out (may still be running): {}", tag, gcode);
            } else if (err.type == MoonrakerErrorType::NOT_READY) {
                spdlog::debug("{} G-code skipped (Klipper halted): {}", tag, gcode);
            } else {
                spdlog::error("{} G-code failed: {} - {}", tag, gcode, err.message);
            }
        },
        MoonrakerAPI::AMS_OPERATION_TIMEOUT_MS,
        // silent=true: completion is owned by the phase tracker + IFS_STATUS, not
        // the RPC return, so the advisory 300s timeout must not surface a false
        // "timed out" toast for a legitimately-long macro (bundle 77TDH9N6).
        // Genuine RPC-error toasts remain suppressed via the error_cb path.
        /*silent=*/true);
    return AmsErrorHelper::success();
}
