// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "remote_screen_sink.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace helix {

/**
 * @brief Remote-screen sink that mirrors dirty rects into a Linux framebuffer.
 *
 * On the Snapmaker U1 the firmware's fb-http daemon serves PNG snapshots of
 * `/dev/fb0`, which HelixScreen does not otherwise feed (it owns the panel via
 * DRM). This sink mmaps `/dev/fb0` and blits each dirty area straight in. Both
 * 32bpp (BGRA) and 16bpp (RGB565) fb0 geometries are supported: a 32bpp BGRA
 * source maps through to a 32bpp fb0 with no per-pixel conversion
 * (hardware-verified 2026-07-09: 480x320, 32bpp, stride 1920), and the four
 * source x destination bpp combinations are each handled in `on_frame`.
 *
 * The device path is injectable so tests can back the sink with a temp file.
 * When the path is a regular file the fbdev ioctls fail, so `start()` falls
 * back to geometry supplied via `configure_geometry()`.
 *
 * All calls are on the main (LVGL) thread. Failures never affect the UI: a
 * failed `start()` leaves the sink inactive and it silently ignores frames.
 */
class Fb0MailboxSink : public RemoteScreenSink {
  public:
    explicit Fb0MailboxSink(std::string dev = "/dev/fb0");
    ~Fb0MailboxSink() override;

    Fb0MailboxSink(const Fb0MailboxSink&) = delete;
    Fb0MailboxSink& operator=(const Fb0MailboxSink&) = delete;

    bool start() override;
    void stop() override;
    bool wants_frames() const override;
    void on_frame(const RemoteScreenFrame& frame) override;
    const char* name() const override;

    /**
     * @brief Test seam: geometry to use when the fbdev ioctls are unavailable.
     *
     * Only applied when `start()`'s `FBIOGET_VSCREENINFO`/`FBIOGET_FSCREENINFO`
     * calls fail (i.e. a regular-file test backing). On a real fbdev the ioctl
     * values win and these are ignored.
     */
    void configure_geometry(int w, int h, uint32_t stride, int bpp);

  private:
    void warn_once(const char* what);

    std::string dev_;

    int fd_ = -1;
    uint8_t* map_ = nullptr;
    size_t map_size_ = 0;
    int fb_w_ = 0;
    int fb_h_ = 0;
    uint32_t fb_stride_ = 0;
    int fb_bpp_ = 0;

    bool active_ = false;
    bool warned_ = false;

    // One-shot diagnostics (first few frames + first OOB skip).
    int log_count_ = 0;
    int log_done_ = 0;
    bool oob_warned_ = false;

    // Configured fallback geometry (used only when ioctls fail).
    bool has_cfg_ = false;
    int cfg_w_ = 0;
    int cfg_h_ = 0;
    uint32_t cfg_stride_ = 0;
    int cfg_bpp_ = 0;
};

} // namespace helix
