// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui_subscription_guard.h"

#include "ams_backend.h"
#include "async_lifetime_guard.h"
#include "moonraker_api.h"
#include "moonraker_client.h"

#include <spdlog/spdlog.h>

#include <atomic>
#include <mutex>

/// Base class for AMS backends that use Moonraker subscription-based status updates.
/// Extracts common lifecycle, event, and state query logic from AFC/HappyHare/ToolChanger.
///
/// Derived classes MUST implement:
///   - get_type() - return backend-specific AmsType
///   - handle_status_update() - parse backend-specific JSON notifications
///   - backend_log_tag() - return log prefix like "[AMS AFC]"
///
/// Derived classes MAY override:
///   - on_started() - post-start initialization (version detection, config loading, etc.)
///   - on_stopping() - pre-stop cleanup
///   - additional_start_checks() - extra preconditions before subscribing
///   - get_system_info() - if they need to build info from SlotRegistry
///   - validate_slot_index() - if they need custom validation
class AmsSubscriptionBackend : public AmsBackend {
  public:
    AmsSubscriptionBackend(MoonrakerAPI* api, helix::MoonrakerClient* client);
    ~AmsSubscriptionBackend() override;

    // --- Lifecycle (final -- derived classes use hooks instead) ---
    AmsError start() final;
    void stop() final;
    void release_subscriptions() final;
    [[nodiscard]] bool is_running() const final;

    // --- Event system (final) ---
    void set_event_callback(EventCallback callback) final;

    // --- State queries (final) ---
    [[nodiscard]] AmsAction get_current_action() const final;
    [[nodiscard]] int get_current_tool() const final;
    [[nodiscard]] int get_current_slot() const final;
    [[nodiscard]] bool is_filament_loaded() const final;

    // --- Shared utilities (public for AmsState and tests) ---
    void emit_event(const std::string& event, const std::string& data = "");
    /// Common gating before an AMS action runs.
    /// @param requires_toolhead_motion When true, additionally run
    ///        refuse_if_printing(): always refuse while PRINTING, and refuse
    ///        while PAUSED only when filament_ops_self_home() (AD5X
    ///        `_IFS_REMOVE_CURRENT_PRUTOK` runs a buried G28 that Layer 1 cannot
    ///        see). Pausing to swap filament is the runout recovery workflow, so
    ///        every other backend is allowed through while paused. Pass false for
    ///        no-motion ops (eject_lane, select, unlock/recovery).
    AmsError check_preconditions(bool requires_toolhead_motion = false) const;
    virtual AmsError execute_gcode(const std::string& gcode);
    /// Same as execute_gcode(gcode), but invokes @p on_complete when the gcode
    /// command finishes (Klipper acks the script — i.e. a long macro has fully
    /// run, not merely been queued). The callback fires on a background thread;
    /// the caller is responsible for hopping to the main thread. Use this when a
    /// macro's completion is the reliable terminal signal for an operation.
    virtual AmsError execute_gcode(const std::string& gcode, std::function<void()> on_complete);

    /// Whether the toolhead is homed. Virtual purely as a test seam: fixtures
    /// override it to exercise the homed and unhomed branches of
    /// ensure_homed_then() without a live Moonraker connection or a PrinterState.
    /// Production implementation reads the live homed_axes subject via api_.
    [[nodiscard]] virtual bool toolhead_homed() const;

    /// Query homing status and auto-home (G28) if needed before executing gcode.
    /// Returns immediately — homing and gcode execution happen asynchronously.
    /// @p on_complete (optional) fires when the final gcode command finishes
    /// (Klipper acks the script), on a background thread; the caller hops to the
    /// main thread. Use when the gcode's macro completion is the terminal signal.
    /// @param on_error   Fires on G28 or payload failure. When null the failure
    ///                   is logged and the action is reset to IDLE, matching
    ///                   the historical behaviour.
    /// @param timeout_ms Payload timeout. G28 always uses HOMING_TIMEOUT_MS.
    /// @param skip_homing Dispatch the payload without homing, whatever the
    ///                   toolhead reports. For firmware macros that home
    ///                   themselves (CFS Fork variant).
    /// @param silent     Suppress REQUEST_TIMEOUT toasts on the payload. True
    ///                   matches every backend except CFS, which passes false.
    ///
    /// @warning Call from the main thread only — checks toolhead_homed(),
    /// which (in the production override) reads an LVGL subject, and may
    /// synchronously create a confirmation modal.
    AmsError ensure_homed_then(std::string gcode, std::function<void()> on_complete = nullptr,
                               std::function<void(const MoonrakerError&)> on_error = nullptr,
                               uint32_t timeout_ms = MoonrakerAPI::AMS_OPERATION_TIMEOUT_MS,
                               bool skip_homing = false, bool silent = true);

    /// See AmsBackend::arm_home_preconfirmed(). Consumed single-shot by the
    /// NEXT ensure_homed_then() call that finds the toolhead genuinely
    /// unhomed -- does not skip that call's G28, only its confirmation prompt.
    void arm_home_preconfirmed() final {
        home_preconfirmed_ = true;
    }

    /// See AmsBackend::clear_home_preconfirmed(). Idempotent no-op if nothing
    /// is currently armed.
    void clear_home_preconfirmed() final {
        home_preconfirmed_ = false;
    }

  protected:
    // --- Hooks for derived classes ---

    /// Called after subscription is established and running_ is set.
    /// Lock is NOT held. Safe to call emit_event().
    virtual void on_started() {}

    /// Called before stop() releases the subscription.
    /// Lock IS held.
    virtual void on_stopping() {}

    /// Called when the user declines the pre-operation home prompt raised by
    /// ensure_homed_then(). Default resets system_info_.action to IDLE (under
    /// mutex_) and emits EVENT_STATE_CHANGED -- exactly what a plain Cancel
    /// looked like before the confirmation prompt existed.
    ///
    /// Override when the backend arms additional optimistic state BEFORE
    /// calling ensure_homed_then() (a phase tracker, a pending-dispatch
    /// generation, ...): the default only clears system_info_.action, so any
    /// such state is left active. A backend whose apply-loop lacks an
    /// explicit `!= IDLE` guard (AmsBackendAd5xIfs's phase tracker did) then
    /// gets re-armed busy by the very next status frame -- the user declines
    /// a home and the backend wedges for a full timeout window before
    /// latching a fabricated error. Unwind exactly what was armed before the
    /// prompt, then call the base implementation (or replicate its IDLE
    /// reset) to finish the cancel.
    virtual void on_home_confirmation_declined();

    /// Extra checks before subscribing (e.g., ToolChanger requires tools discovered).
    /// Return error to abort start. Lock IS held.
    virtual AmsError additional_start_checks() {
        return AmsErrorHelper::success();
    }

    /// Handle incoming Moonraker status notification. Called from background thread.
    virtual void handle_status_update(const nlohmann::json& notification) = 0;

    /// Return log tag like "[AMS AFC]" for log messages.
    virtual const char* backend_log_tag() const = 0;

    /// Refuse a toolhead-motion filament op while a print owns the toolhead.
    ///
    /// Returns AmsErrorHelper::print_active() (with a spdlog::warn) when the
    /// print-job state is PRINTING, or when it is PAUSED **and**
    /// filament_ops_self_home() is true. A PAUSED job on a backend that does not
    /// self-home is allowed through — pause-then-swap is the runout /
    /// colour-change recovery workflow, and Layer 1
    /// (helix::api::reject_homing_during_active_print) still refuses any
    /// app-emitted G28 in that state. Success when api_ is null (unit tests /
    /// cold-boot: print state is unknown, don't block).
    ///
    /// Exposed to derived backends that gate motion ops WITHOUT the running_/busy
    /// checks in check_preconditions() (e.g. QIDI Box).
    AmsError refuse_if_printing() const;

    // --- Protected state for derived classes ---
    MoonrakerAPI* api_;
    helix::MoonrakerClient* client_;
    mutable std::mutex mutex_;
    AmsSystemInfo system_info_;
    std::atomic<bool> running_{false};

    /// Lifetime guard for async callback safety. Tokens captured in the
    /// subscription lambda are expired when the backend is destroyed, preventing
    /// use-after-free when WebSocket dispatch races with clear_backends() (#621).
    helix::AsyncLifetimeGuard lifetime_;

  private:
    EventCallback event_callback_;
    SubscriptionGuard subscription_;

    /// Set by arm_home_preconfirmed(), consumed single-shot by ensure_homed_then().
    /// Main-thread only -- both the setter (a UI-surface click handler) and the
    /// consuming read happen on the main thread, same as toolhead_homed() itself.
    bool home_preconfirmed_ = false;

    /// Send the payload gcode, honouring the 1-arg/2-arg execute_gcode split.
    /// ~20 test fixtures override ONLY the 1-arg form; calling the 2-arg form
    /// with a null callback would fall through to the base and stop capturing.
    ///
    /// When @p on_error, @p timeout_ms, and @p silent are all left at their
    /// ensure_homed_then() defaults, dispatch stays on the same two virtuals
    /// referenced above — required for fixture compatibility. Any non-default
    /// combination needs a live @p api_: it talks to MoonrakerAPI directly (the
    /// hardcoded virtuals can't carry a caller's own error/timeout/toast
    /// policy), the same way AmsBackendCfs::dispatch_action_script used to
    /// before this method existed to replace it.
    AmsError dispatch_payload(std::string gcode, std::function<void()> on_complete,
                              std::function<void(const MoonrakerError&)> on_error = nullptr,
                              uint32_t timeout_ms = MoonrakerAPI::AMS_OPERATION_TIMEOUT_MS,
                              bool silent = true);

    /// Report a gcode failure through @p on_error when set, else fall back to
    /// the historical behaviour: log at error level and reset the action to
    /// IDLE. Callers on a background thread must already have marshalled to
    /// main (see dispatch_payload()/ensure_homed_then()'s token.defer() calls)
    /// before reaching this -- it touches system_info_ under mutex_ directly.
    void handle_dispatch_error(const MoonrakerError& err,
                               const std::function<void(const MoonrakerError&)>& on_error);
};
