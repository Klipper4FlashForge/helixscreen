// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "remote_screen_sink.h"

#include <memory>
#include <vector>

namespace helix {

/**
 * @brief Fans dirty-area frames out to a set of remote-screen sinks.
 *
 * Owned by `DisplayManager` (it needs the display lifecycle, so it is not a
 * global singleton). The display flush path calls `on_frame()` per dirty
 * area; the manager forwards to each sink whose `wants_frames()` is true.
 * With no sinks (all platforms but the U1) the routing is a cheap early-out.
 *
 * All calls are on the main (LVGL) thread; no locking.
 */
class RemoteScreenManager {
  public:
    /** @brief Take ownership of a sink. Does not start it. */
    void add_sink(std::unique_ptr<RemoteScreenSink> sink);

    /** @brief Start every sink. A sink whose start() returns false is retained but inactive. */
    void start();

    /** @brief Stop every sink. Idempotent. */
    void stop();

    /** @brief True if any sink currently wants frames. */
    bool any_active() const;

    /** @brief Forward a frame to every active sink. No-op if no sinks. */
    void on_frame(const RemoteScreenFrame& frame);

  private:
    std::vector<std::unique_ptr<RemoteScreenSink>> sinks_;
};

} // namespace helix
