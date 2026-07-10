// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_remote_screen_fb0_sink.cpp
 * @brief Fb0MailboxSink dirty-rect blit tests, backed by a temp file.
 *
 * The sink is pointed at a regular temp file sized like the U1's fb0
 * (480x320, stride 1920, 32bpp = 614400 bytes). The fbdev ioctls fail on a
 * regular file, so start() falls back to configure_geometry().
 *
 * CRITICAL contract: px_map is the DRAW-BUFFER ORIGIN (0,0), not the dirty-area
 * origin (LVGL direct/full render mode). A dirty rect's pixels live at the
 * rect's ABSOLUTE coordinates within px_map. So the source buffers here are
 * FULL-framebuffer sized, with content painted at the area's position — and a
 * dedicated test asserts a partial rect copies ITS content, not the buffer's
 * top-left corner (the #1031 ghosting regression).
 */

#include "remote_screen_fb0_sink.h"
#include "remote_screen_sink.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include <unistd.h>

#include "../catch_amalgamated.hpp"

using namespace helix;

namespace {

constexpr int      kFbW      = 480;
constexpr int      kFbH      = 320;
constexpr uint32_t kFbStride = 1920; // 480 * 4, no row padding
constexpr size_t   kFbSize   = static_cast<size_t>(kFbStride) * kFbH; // 614400

std::vector<uint8_t> read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)),
                                std::istreambuf_iterator<char>());
}

std::string make_temp_fb() {
    char tmpl[] = "/tmp/helix_fb0_XXXXXX";
    int  fd     = ::mkstemp(tmpl);
    REQUIRE(fd >= 0);
    REQUIRE(::ftruncate(fd, static_cast<off_t>(kFbSize)) == 0);
    ::close(fd);
    return std::string(tmpl);
}

// A frame whose px_map is a FULL-buffer-origin source (stride spans the whole
// display width). x1..y2 are absolute coords into that buffer.
RemoteScreenFrame make_frame(const uint8_t* px, int32_t x1, int32_t y1, int32_t x2, int32_t y2,
                             uint32_t src_stride,
                             RemoteScreenPixelFormat fmt = RemoteScreenPixelFormat::BGRA8888) {
    RemoteScreenFrame f;
    f.px_map       = px;
    f.x1           = x1;
    f.y1           = y1;
    f.x2           = x2;
    f.y2           = y2;
    f.disp_w       = kFbW;
    f.disp_h       = kFbH;
    f.color_format = 18;
    f.src_stride   = src_stride;
    f.src_format   = fmt;
    return f;
}

// Fill a BGRA rect [x1,x2]x[y1,y2] within a full-size (kFbW x kFbH) buffer.
void fill_bgra_rect(std::vector<uint8_t>& buf, uint32_t stride, int x1, int y1, int x2, int y2,
                    uint8_t b, uint8_t g, uint8_t r, uint8_t a) {
    for (int y = y1; y <= y2; ++y) {
        for (int x = x1; x <= x2; ++x) {
            size_t o      = static_cast<size_t>(y) * stride + static_cast<size_t>(x) * 4;
            buf[o + 0] = b; buf[o + 1] = g; buf[o + 2] = r; buf[o + 3] = a;
        }
    }
}

} // namespace

TEST_CASE("Fb0MailboxSink: full-frame magenta blit", "[remote_screen][fb0]") {
    std::string path = make_temp_fb();

    Fb0MailboxSink sink(path);
    sink.configure_geometry(kFbW, kFbH, kFbStride, 32);
    REQUIRE(sink.start());
    REQUIRE(sink.wants_frames());

    std::vector<uint8_t> src(kFbSize);
    for (size_t i = 0; i < kFbSize; i += 4) {
        src[i + 0] = 0xFF; src[i + 1] = 0x00; src[i + 2] = 0xFF; src[i + 3] = 0xFF; // magenta BGRA
    }

    sink.on_frame(make_frame(src.data(), 0, 0, kFbW - 1, kFbH - 1, kFbStride));
    sink.stop();

    std::vector<uint8_t> fb = read_file(path);
    REQUIRE(fb.size() == kFbSize);
    const size_t center = static_cast<size_t>(160) * kFbStride + static_cast<size_t>(240) * 4;
    REQUIRE(fb[center + 0] == 0xFF);
    REQUIRE(fb[center + 2] == 0xFF);
    REQUIRE(fb[0] == 0xFF);
    const size_t br = static_cast<size_t>(319) * kFbStride + static_cast<size_t>(479) * 4;
    REQUIRE(fb[br + 0] == 0xFF);
    REQUIRE(fb[br + 2] == 0xFF);

    ::unlink(path.c_str());
}

TEST_CASE("Fb0MailboxSink: partial rect copies ITS content, not top-left (ghost regression)",
          "[remote_screen][fb0]") {
    std::string path = make_temp_fb();

    Fb0MailboxSink sink(path);
    sink.configure_geometry(kFbW, kFbH, kFbStride, 32);
    REQUIRE(sink.start());

    // Full-buffer source: RED across the whole buffer (incl. top-left), GREEN
    // only in the dirty rect (100,50)..(131,81). px_map is buffer origin.
    std::vector<uint8_t> src(kFbSize);
    fill_bgra_rect(src, kFbStride, 0, 0, kFbW - 1, kFbH - 1, 0x00, 0x00, 0xFF, 0xFF); // red everywhere
    fill_bgra_rect(src, kFbStride, 100, 50, 131, 81, 0x00, 0xFF, 0x00, 0xFF);         // green rect

    // Flush only the green rect.
    sink.on_frame(make_frame(src.data(), 100, 50, 131, 81, kFbStride));
    sink.stop();

    std::vector<uint8_t> fb = read_file(path);
    REQUIRE(fb.size() == kFbSize);

    // fb0 at the rect must be GREEN (the rect's content) — NOT red (top-left).
    for (int r = 50; r <= 81; ++r) {
        const size_t off = static_cast<size_t>(r) * kFbStride + static_cast<size_t>(100) * 4;
        INFO("row " << r);
        REQUIRE(fb[off + 0] == 0x00); // B
        REQUIRE(fb[off + 1] == 0xFF); // G  <-- green, would be 0x00 if ghosting top-left
        REQUIRE(fb[off + 2] == 0x00); // R  <-- would be 0xFF (red) under the old bug
    }
    // Last column of the rect (131) is green too.
    const size_t last = static_cast<size_t>(60) * kFbStride + static_cast<size_t>(131) * 4;
    REQUIRE(fb[last + 1] == 0xFF);

    // Outside the flushed rect: fb0 was never written for this frame -> stays 0.
    REQUIRE(fb[0] == 0x00);
    const size_t left = static_cast<size_t>(60) * kFbStride + static_cast<size_t>(99) * 4;
    REQUIRE(fb[left + 0] == 0x00);
    REQUIRE(fb[left + 1] == 0x00);

    ::unlink(path.c_str());
}

TEST_CASE("Fb0MailboxSink: dirty rect past the edge is clamped", "[remote_screen][fb0]") {
    std::string path = make_temp_fb();

    Fb0MailboxSink sink(path);
    sink.configure_geometry(kFbW, kFbH, kFbStride, 32);
    REQUIRE(sink.start());

    // Full buffer, distinct color in the in-bounds corner region.
    std::vector<uint8_t> src(kFbSize);
    fill_bgra_rect(src, kFbStride, 460, 300, kFbW - 1, kFbH - 1, 0x11, 0x22, 0x33, 0x44);

    // Area (460,300)..(500,340) extends past the 480x320 fb — must clamp.
    sink.on_frame(make_frame(src.data(), 460, 300, 500, 340, kFbStride));
    sink.stop();

    std::vector<uint8_t> fb = read_file(path);
    REQUIRE(fb.size() == kFbSize); // no write past the mapping

    const size_t tl = static_cast<size_t>(300) * kFbStride + static_cast<size_t>(460) * 4;
    REQUIRE(fb[tl + 0] == 0x11);
    REQUIRE(fb[tl + 2] == 0x33);
    const size_t last = static_cast<size_t>(319) * kFbStride + static_cast<size_t>(479) * 4;
    REQUIRE(fb[last + 0] == 0x11);
    REQUIRE(fb[last + 2] == 0x33);

    ::unlink(path.c_str());
}

TEST_CASE("Fb0MailboxSink: RGB565 source is converted to BGRA at the right position",
          "[remote_screen][fb0]") {
    std::string path = make_temp_fb();

    Fb0MailboxSink sink(path);
    sink.configure_geometry(kFbW, kFbH, kFbStride, 32);
    REQUIRE(sink.start());

    // Full-size RGB565 buffer (stride 480*2 = 960). Pure red (0xF800) only in the
    // rect (10,10)..(25,25); rest zero.
    constexpr uint32_t kRgbStride = kFbW * 2; // 960
    std::vector<uint8_t> src(static_cast<size_t>(kRgbStride) * kFbH, 0);
    for (int y = 10; y <= 25; ++y) {
        for (int x = 10; x <= 25; ++x) {
            size_t o = static_cast<size_t>(y) * kRgbStride + static_cast<size_t>(x) * 2;
            src[o + 0] = 0x00; src[o + 1] = 0xF8; // 0xF800 = pure red
        }
    }

    sink.on_frame(make_frame(src.data(), 10, 10, 25, 25, kRgbStride, RemoteScreenPixelFormat::RGB565));
    sink.stop();

    std::vector<uint8_t> fb = read_file(path);
    REQUIRE(fb.size() == kFbSize);

    // 0xF800 -> R8=0xFF, G=0, B=0. fb0 BGRA: [B=0, G=0, R=0xFF, A=0xFF] at (12,12).
    const size_t off = static_cast<size_t>(12) * kFbStride + static_cast<size_t>(12) * 4;
    REQUIRE(fb[off + 0] == 0x00);
    REQUIRE(fb[off + 1] == 0x00);
    REQUIRE(fb[off + 2] == 0xFF);
    REQUIRE(fb[off + 3] == 0xFF);
    // Outside the rect stays zero.
    REQUIRE(fb[0] == 0x00);

    ::unlink(path.c_str());
}

TEST_CASE("Fb0MailboxSink: unknown source format is skipped", "[remote_screen][fb0]") {
    std::string path = make_temp_fb();
    Fb0MailboxSink sink(path);
    sink.configure_geometry(kFbW, kFbH, kFbStride, 32);
    REQUIRE(sink.start());

    std::vector<uint8_t> src(kFbSize, 0xAB);
    sink.on_frame(make_frame(src.data(), 0, 0, kFbW - 1, kFbH - 1, kFbStride,
                             RemoteScreenPixelFormat::Unknown));
    sink.stop();

    std::vector<uint8_t> fb = read_file(path);
    REQUIRE(fb.size() == kFbSize);
    REQUIRE(fb[0] == 0x00);
    REQUIRE(fb[kFbSize / 2] == 0x00);

    ::unlink(path.c_str());
}

TEST_CASE("Fb0MailboxSink: failed start leaves an inactive no-op sink", "[remote_screen][fb0]") {
    Fb0MailboxSink sink("/proc/nonexistent_helix_fb0");
    sink.configure_geometry(kFbW, kFbH, kFbStride, 32);

    REQUIRE_FALSE(sink.start());
    REQUIRE_FALSE(sink.wants_frames());

    std::vector<uint8_t> src(kFbSize, 0xFF);
    sink.on_frame(make_frame(src.data(), 0, 0, kFbW - 1, kFbH - 1, kFbStride));

    sink.stop();
    REQUIRE_FALSE(sink.wants_frames());
}
