// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file panel_lifecycle.h
 * @brief Common lifecycle interface for panels and overlays
 *
 * Defines the minimal interface that both PanelBase and OverlayBase
 * implement for NavigationManager to dispatch lifecycle events.
 *
 * ## Implemented by:
 * - PanelBase: Main UI panels (enum-indexed, setup() pattern)
 * - OverlayBase: Modal overlays (widget-indexed, create() pattern)
 *
 * ## Lifecycle Contract:
 * - on_deactivate() called BEFORE a panel/overlay becomes hidden
 * - on_activate() called AFTER animation completes and panel/overlay is visible
 * - get_name() used for debugging/logging only
 *
 * @threading Main thread only
 */

#pragma once

/**
 * @class IPanelLifecycle
 * @brief Common lifecycle interface for NavigationManager dispatch
 *
 * This interface enables NavigationManager to handle both panels and overlays
 * polymorphically for lifecycle event dispatch.
 */
class IPanelLifecycle {
  public:
    virtual ~IPanelLifecycle() = default;

    /**
     * @brief Called when panel/overlay becomes visible
     *
     * Used to start background operations (scanning, subscriptions, timers).
     * Safe to call multiple times (implementations should be idempotent).
     */
    virtual void on_activate() = 0;

    /**
     * @brief Called when panel/overlay is being hidden
     *
     * Used to stop background operations before animation starts.
     * Safe to call multiple times (implementations should be idempotent).
     */
    virtual void on_deactivate() = 0;

    /**
     * @brief Tear down and re-create this view from its XML component definition
     *
     * Dev-only hook called by NavigationManager after XML hot-reload re-registers
     * a component. Default is a no-op for lifecycles that wrap non-XML or
     * non-rebuildable widgets. Concrete panel/overlay bases override to rebuild.
     *
     * @return true if rebuilt, false if skipped (not yet shown, not this instance, etc.)
     */
    virtual bool rebuild() {
        return false;
    }

    /**
     * @brief Whether this view is a destination rather than a transient layer
     *
     * Destinations render full width (screen - nav) and occlude the backdrop;
     * their drill-downs inherit that. Transient layers render gapped
     * (screen - nav - space_lg) so the backdrop shows at the leading edge,
     * signalling "you opened this and will return."
     *
     * Default false — most overlays are tools you return from. Override to true
     * only for screens users park on for long stretches (AMS, AMS Overview,
     * Print Status). Declaring it here rather than at the push site means the
     * promotion travels with the panel: AmsPanel is reachable from Home, the
     * Printer Manager overlay and the AMS Overview, and must be full width from
     * all three.
     *
     * NavigationManager::push_overlay() reads this. See include/overlay_class.h
     * and prestonbrown/helixscreen#1178.
     */
    virtual bool is_destination() const {
        return false;
    }

    /**
     * @brief Re-apply C++-side content to a freshly rebuilt widget tree
     *
     * rebuild() re-runs setup()/create() and nothing else, so it reproduces
     * exactly what the XML describes. Content a view writes into its widgets
     * afterwards is not in the XML and does not come back on its own: dropdown
     * option lists, imperatively built rows, text set with lv_textarea_set_text,
     * colors applied to a swatch. Views that populate from a separate entry
     * point — a show_for_*(), a click handler — override this to re-apply that
     * content from the state they already hold, and must not re-seed that state
     * (doing so would discard the user's in-progress edits).
     *
     * Nothing is needed here for content bound to a subject: the rebuilt widgets
     * read the subject's current value when they bind. Nothing is needed either
     * for content populated inside create()/setup() or on_activate(), both of
     * which rebuild() already re-runs.
     *
     * Called after the new tree exists and before on_activate(). Dev-only path,
     * reached only via XML hot-reload.
     */
    virtual void repopulate() {}

    /**
     * @brief Get human-readable name for logging
     * @return Panel/overlay name (e.g., "Motion Panel", "Network Settings")
     */
    virtual const char* get_name() const = 0;
};
