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

    // --- Filament operations (final -- derived backends implement do_*) ---
    //
    // Non-virtual interface. The public entry point runs the print-active gate,
    // then dispatches to the backend's do_* hook. Invoking the gate used to be
    // opt-in, hand-written at ~24 call sites: 329e731e9 added it to seven
    // backends and missed the eighth, which shipped with no gate at all
    // (180a71c7d). A backend cannot forget it here, because it never writes the
    // entry point.
    AmsError load_filament(int slot_index) final;
    AmsError unload_filament(int slot_index) final;
    AmsError select_slot(int slot_index) final;
    AmsError change_tool(int tool_number) final;

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

    /// Query homing status and auto-home (G28) if needed before executing gcode.
    /// Returns immediately — homing and gcode execution happen asynchronously.
    /// @p on_complete (optional) fires when the final gcode command finishes
    /// (Klipper acks the script), on a background thread; the caller hops to the
    /// main thread. Use when the gcode's macro completion is the terminal signal.
    AmsError ensure_homed_then(std::string gcode, std::function<void()> on_complete = nullptr);

  protected:
    // --- Hooks for derived classes ---

    /// Called after subscription is established and running_ is set.
    /// Lock is NOT held. Safe to call emit_event().
    virtual void on_started() {}

    /// Called before stop() releases the subscription.
    /// Lock IS held.
    virtual void on_stopping() {}

    /// Extra checks before subscribing (e.g., ToolChanger requires tools discovered).
    /// Return error to abort start. Lock IS held.
    virtual AmsError additional_start_checks() {
        return AmsErrorHelper::success();
    }

    /// Handle incoming Moonraker status notification. Called from background thread.
    virtual void handle_status_update(const nlohmann::json& notification) = 0;

    /// Return log tag like "[AMS AFC]" for log messages.
    virtual const char* backend_log_tag() const = 0;

    // --- Filament operation hooks ---

    /// Backend implementation of the matching public operation. Reached only
    /// after the gate has passed, so these must NOT call check_preconditions()
    /// or refuse_if_printing() again — one authority, in the base.
    ///
    /// When one op is really another (ACE loads to select, a Snapmaker select
    /// IS a tool change), forward to the sibling do_* rather than the public
    /// entry point, so the gate runs once.
    virtual AmsError do_load_filament(int slot_index) = 0;
    virtual AmsError do_unload_filament(int slot_index) = 0;
    virtual AmsError do_select_slot(int slot_index) = 0;
    virtual AmsError do_change_tool(int tool_number) = 0;

    /// Does a slot SELECT move the toolhead on this backend?
    ///
    /// Load, unload and tool change are toolhead motion on every backend and are
    /// not negotiable — see op_moves_toolhead(). select_slot is the one
    /// genuinely per-backend answer, and re-deciding it once per backend is how
    /// it gets got wrong: Happy Hare's MMU_SELECT drives the gate selector and
    /// never touches the carriage, while on a Snapmaker U1 a slot select IS a
    /// physical tool change (do_select_slot forwards to do_change_tool, which
    /// emits `T{n}`). Override to true when yours moves the toolhead.
    ///
    /// This can only ADD motion, never remove it. There is deliberately no hook
    /// for declaring an operation gate-free.
    [[nodiscard]] virtual bool select_slot_moves_toolhead() const {
        return false;
    }

    /// Which gate the public filament operations run. Neither value means
    /// "no gate" — the print-active refusal applies either way.
    enum class FilamentOpGate {
        /// check_preconditions(): backend started, AMS not busy, and — for a
        /// toolhead-motion op — no print owning the toolhead.
        Standard,
        /// refuse_if_printing() only, skipping the running_/busy half. For a
        /// backend that does not drive its filament ops off those at all; see
        /// AmsBackendQidi, which has never consulted them.
        PrintActiveOnly,
    };
    [[nodiscard]] virtual FilamentOpGate filament_op_gate() const {
        return FilamentOpGate::Standard;
    }

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
    /// The four gated operations, so motion can be classified per METHOD in one
    /// place instead of per backend at every call site.
    enum class FilamentOp { Load, Unload, SelectSlot, ChangeTool };

    /// Motion classification, stated ONCE for every backend.
    [[nodiscard]] bool op_moves_toolhead(FilamentOp op) const;

    /// Run the gate this backend declares, for this operation.
    [[nodiscard]] AmsError gate_filament_op(FilamentOp op) const;

    EventCallback event_callback_;
    SubscriptionGuard subscription_;
};
