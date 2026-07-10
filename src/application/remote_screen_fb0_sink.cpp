// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "remote_screen_fb0_sink.h"

#include <spdlog/spdlog.h>

#include <cstring>
#include <utility>

#include <fcntl.h>
#include <linux/fb.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

namespace helix {

Fb0MailboxSink::Fb0MailboxSink(std::string dev) : dev_(std::move(dev)) {}

Fb0MailboxSink::~Fb0MailboxSink() {
    stop();
}

void Fb0MailboxSink::configure_geometry(int w, int h, uint32_t stride, int bpp) {
    has_cfg_    = true;
    cfg_w_      = w;
    cfg_h_      = h;
    cfg_stride_ = stride;
    cfg_bpp_    = bpp;
}

void Fb0MailboxSink::warn_once(const char* what) {
    if (!warned_) {
        warned_ = true;
        spdlog::warn("[RemoteScreen] fb0 sink disabled ({}): dev={}", what, dev_);
    }
}

bool Fb0MailboxSink::start() {
    if (active_) {
        return true;
    }

    fd_ = ::open(dev_.c_str(), O_RDWR);
    if (fd_ < 0) {
        warn_once("open failed");
        return false;
    }

    // A real fbdev answers these ioctls; a regular file (test backing) does not.
    struct fb_var_screeninfo vinfo;
    struct fb_fix_screeninfo finfo;
    std::memset(&vinfo, 0, sizeof(vinfo));
    std::memset(&finfo, 0, sizeof(finfo));

    bool have_ioctl = ::ioctl(fd_, FBIOGET_VSCREENINFO, &vinfo) == 0 &&
                      ::ioctl(fd_, FBIOGET_FSCREENINFO, &finfo) == 0;

    if (have_ioctl) {
        fb_w_      = static_cast<int>(vinfo.xres);
        fb_h_      = static_cast<int>(vinfo.yres);
        fb_bpp_    = static_cast<int>(vinfo.bits_per_pixel);
        fb_stride_ = static_cast<uint32_t>(finfo.line_length);
    } else if (has_cfg_) {
        // Test path: fall back to configured geometry so the blit math is
        // exercisable without a device.
        fb_w_      = cfg_w_;
        fb_h_      = cfg_h_;
        fb_bpp_    = cfg_bpp_;
        fb_stride_ = cfg_stride_;
    } else {
        warn_once("no fbdev ioctls and no configured geometry");
        ::close(fd_);
        fd_ = -1;
        return false;
    }

    if (fb_bpp_ != 32) {
        warn_once("unsupported bpp (need 32)");
        ::close(fd_);
        fd_ = -1;
        return false;
    }

    if (fb_w_ <= 0 || fb_h_ <= 0 || fb_stride_ == 0) {
        warn_once("invalid geometry");
        ::close(fd_);
        fd_ = -1;
        return false;
    }

    map_size_ = static_cast<size_t>(fb_stride_) * static_cast<size_t>(fb_h_);
    void* m   = ::mmap(nullptr, map_size_, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0);
    if (m == MAP_FAILED) {
        warn_once("mmap failed");
        map_size_ = 0;
        ::close(fd_);
        fd_ = -1;
        return false;
    }

    map_    = static_cast<uint8_t*>(m);
    active_ = true;
    spdlog::info("[RemoteScreen] fb0 sink active: dev={} {}x{} stride={} bpp={}", dev_, fb_w_, fb_h_,
                 fb_stride_, fb_bpp_);
    return true;
}

void Fb0MailboxSink::stop() {
    if (map_) {
        ::munmap(map_, map_size_);
        map_      = nullptr;
        map_size_ = 0;
    }
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
    active_ = false;
}

bool Fb0MailboxSink::wants_frames() const {
    return active_;
}

void Fb0MailboxSink::on_frame(const RemoteScreenFrame& f) {
    if (!active_ || map_ == nullptr || f.px_map == nullptr) {
        return;
    }

    int32_t x1 = f.x1;
    int32_t y1 = f.y1;
    int32_t w  = f.x2 - f.x1 + 1;
    int32_t h  = f.y2 - f.y1 + 1;
    if (w <= 0 || h <= 0) {
        return;
    }

    // Track how many source pixels/rows we skip while clamping to the mapping.
    int32_t src_col_skip = 0;
    int32_t src_row_skip = 0;

    if (x1 < 0) {
        src_col_skip = -x1;
        w += x1; // shrink width by the clipped amount
        x1 = 0;
    }
    if (y1 < 0) {
        src_row_skip = -y1;
        h += y1;
        y1 = 0;
    }
    if (x1 + w > fb_w_) {
        w = fb_w_ - x1;
    }
    if (y1 + h > fb_h_) {
        h = fb_h_ - y1;
    }
    if (w <= 0 || h <= 0) {
        return;
    }

    const size_t row_bytes = static_cast<size_t>(w) * 4;
    for (int32_t row = 0; row < h; ++row) {
        const int32_t dst_y = y1 + row;
        const size_t  dst   = static_cast<size_t>(dst_y) * fb_stride_ + static_cast<size_t>(x1) * 4;
        const size_t  src   = static_cast<size_t>(src_row_skip + row) * f.src_stride +
                           static_cast<size_t>(src_col_skip) * 4;
        std::memcpy(map_ + dst, f.px_map + src, row_bytes);
    }
}

const char* Fb0MailboxSink::name() const {
    return "Fb0MailboxSink";
}

} // namespace helix
