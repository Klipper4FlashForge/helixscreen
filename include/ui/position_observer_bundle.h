// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file position_observer_bundle.h
 * @brief Bundle for managing position subject observers (X, Y, Z axes)
 *
 * Encapsulates the repetitive pattern of subscribing to 3 position subjects
 * (gcode_position_x, gcode_position_y, gcode_position_z) that appears in
 * multiple panels.
 *
 * Reduces ~9-12 lines of boilerplate per panel to a single setup call.
 *
 * @pattern Observer Bundle - groups related observers for common use cases
 */

#pragma once

#include "ui_observer_guard.h"

#include "observer_factory.h"
#include "printer_state.h"

namespace helix::ui {

/**
 * @brief Bundle for position observers (X, Y, Z axes)
 *
 * Use when a panel needs to observe all 3 position subjects from PrinterState.
 * Supports two patterns:
 * 1. Sync observers with per-axis callbacks (UI thread only)
 * 2. Async observers for background thread updates with unified UI callback
 *
 * @tparam Panel The panel class type (must be pointer-safe)
 *
 * @code{.cpp}
 * // Pattern 1: Sync with individual handlers (simpler, UI thread only)
 * PositionObserverBundle<MyPanel> pos_observers_;
 *
 * pos_observers_.setup_sync(
 *     this,
 *     printer_state_,
 *     [](MyPanel* p, int v) { p->format_x(v); p->update_display(); },
 *     [](MyPanel* p, int v) { p->format_y(v); p->update_display(); },
 *     [](MyPanel* p, int v) { p->format_z(v); p->update_display(); }
 * );
 *
 * // Pattern 2: Async with caching + unified update (thread-safe)
 * pos_observers_.setup_async(
 *     this,
 *     printer_state_,
 *     [](MyPanel* p, int v) { p->cached_x_ = v; },
 *     [](MyPanel* p, int v) { p->cached_y_ = v; },
 *     [](MyPanel* p, int v) { p->cached_z_ = v; },
 *     [](MyPanel* p) { p->update_all_position_displays(); }
 * );
 * @endcode
 */
template <typename Panel> class PositionObserverBundle {
  public:
    /**
     * @brief Setup synchronous position observers with individual callbacks
     *
     * Use when handlers run on UI thread and each position update needs
     * its own handler logic. Callbacks receive raw centimillimeter values.
     *
     * @param panel Panel instance (must outlive observers)
     * @param state PrinterState reference for position subjects
     * @param on_x_pos Called when X position changes
     * @param on_y_pos Called when Y position changes
     * @param on_z_pos Called when Z position changes
     */
    template <typename XPosHandler, typename YPosHandler, typename ZPosHandler>
    void setup_sync(Panel* panel, PrinterState& state, XPosHandler&& on_x_pos,
                    YPosHandler&& on_y_pos, ZPosHandler&& on_z_pos) {
        clear();

        x_pos_observer_ = observe_int_sync<Panel>(state.get_gcode_position_x_subject(), panel,
                                                  std::forward<XPosHandler>(on_x_pos));

        y_pos_observer_ = observe_int_sync<Panel>(state.get_gcode_position_y_subject(), panel,
                                                  std::forward<YPosHandler>(on_y_pos));

        z_pos_observer_ = observe_int_sync<Panel>(state.get_gcode_position_z_subject(), panel,
                                                  std::forward<ZPosHandler>(on_z_pos));
    }

    /**
     * @brief Setup async position observers with unified update callback
     *
     * Use when updates come from background threads and need thread-safe
     * caching followed by a single UI update. The value handlers cache
     * data directly, then update_handler is called via ui_queue_update().
     *
     * @param panel Panel instance (must outlive observers)
     * @param state PrinterState reference for position subjects
     * @param cache_x_pos Handler to cache X position (any thread)
     * @param cache_y_pos Handler to cache Y position (any thread)
     * @param cache_z_pos Handler to cache Z position (any thread)
     * @param update_handler Called on UI thread after any position changes
     */
    template <typename CacheXPos, typename CacheYPos, typename CacheZPos, typename UpdateHandler>
    void setup_async(Panel* panel, PrinterState& state, CacheXPos&& cache_x_pos,
                     CacheYPos&& cache_y_pos, CacheZPos&& cache_z_pos,
                     UpdateHandler&& update_handler) {
        clear();

        // Copy update_handler since it's used by all 3 observers
        // (forwarding an rvalue multiple times would move-from it)
        auto update_copy = update_handler;

        x_pos_observer_ =
            observe_int_async<Panel>(state.get_gcode_position_x_subject(), panel,
                                     std::forward<CacheXPos>(cache_x_pos), update_copy);

        y_pos_observer_ =
            observe_int_async<Panel>(state.get_gcode_position_y_subject(), panel,
                                     std::forward<CacheYPos>(cache_y_pos), update_copy);

        z_pos_observer_ =
            observe_int_async<Panel>(state.get_gcode_position_z_subject(), panel,
                                     std::forward<CacheZPos>(cache_z_pos), std::move(update_copy));
    }

    /**
     * @brief Clear all observers (automatic on destruction)
     *
     * Safe to call multiple times. Observers are released via RAII.
     */
    void clear() {
        x_pos_observer_ = ObserverGuard();
        y_pos_observer_ = ObserverGuard();
        z_pos_observer_ = ObserverGuard();
    }

    /**
     * @brief Check if bundle has active observers
     * @return true if any observer is set up
     */
    [[nodiscard]] bool is_active() const {
        return x_pos_observer_ || y_pos_observer_ || z_pos_observer_;
    }

  private:
    ObserverGuard x_pos_observer_;
    ObserverGuard y_pos_observer_;
    ObserverGuard z_pos_observer_;
};

} // namespace helix::ui
