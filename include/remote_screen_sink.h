// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>

namespace helix {

/**
 * @brief A single dirty-area frame update handed to remote-screen sinks.
 *
 * A non-owning view of the rendered pixels for one LVGL flush area. It is
 * built in the display flush path (main thread) and passed by const-ref to
 * every active sink. Coordinates are inclusive LVGL area coords
 * (`x2`/`y2` are the last pixel, not one-past-end).
 *
 * `color_format` carries the `lv_color_format_t` value as a plain int so
 * consumers that don't need LVGL headers can still route frames. This header
 * intentionally does NOT include `lvgl.h`.
 */
struct RemoteScreenFrame {
    const uint8_t* px_map = nullptr; ///< Source pixels for this dirty area.
    int32_t        x1     = 0;       ///< Inclusive left of the dirty area.
    int32_t        y1     = 0;       ///< Inclusive top of the dirty area.
    int32_t        x2     = 0;       ///< Inclusive right of the dirty area.
    int32_t        y2     = 0;       ///< Inclusive bottom of the dirty area.
    int32_t        disp_w = 0;       ///< Full display horizontal resolution.
    int32_t        disp_h = 0;       ///< Full display vertical resolution.
    int            color_format = 0; ///< lv_color_format_t as int.
    uint32_t       src_stride   = 0; ///< Bytes per row of `px_map`.
};

/**
 * @brief Abstract remote-screen sink — the extension point.
 *
 * A sink consumes dirty-area frames and mirrors them somewhere (fb0 today,
 * a future in-process HTTP server later). Sinks are owned by
 * `RemoteScreenManager`. All calls are on the main (LVGL) thread.
 */
class RemoteScreenSink {
  public:
    virtual ~RemoteScreenSink() = default;

    /** @brief Acquire resources. Return false to stay inactive (UI unaffected). */
    virtual bool start() = 0;

    /** @brief Release resources. Idempotent. */
    virtual void stop() = 0;

    /** @brief Gate: true when this sink wants dirty-area frames right now. */
    virtual bool wants_frames() const = 0;

    /** @brief Consume one dirty-area update. */
    virtual void on_frame(const RemoteScreenFrame& frame) = 0;

    /** @brief Human-readable sink name for logging. */
    virtual const char* name() const = 0;
};

} // namespace helix
