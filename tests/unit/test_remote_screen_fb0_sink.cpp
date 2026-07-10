// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_remote_screen_fb0_sink.cpp
 * @brief Fb0MailboxSink dirty-rect blit tests, backed by a temp file.
 *
 * The sink is pointed at a regular temp file sized like the U1's fb0
 * (480x320, stride 1920, 32bpp = 614400 bytes). The fbdev ioctls fail on a
 * regular file, so start() falls back to configure_geometry(). Frames are
 * synthetic BGRA buffers; assertions re-read the file and check that dirty
 * rects land at the right byte offset/stride, that out-of-bounds writes are
 * clamped, and that an inactive sink is a safe no-op.
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

// Read the whole backing file into a byte vector.
std::vector<uint8_t> read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)),
                                std::istreambuf_iterator<char>());
}

// Build a temp file sized to the fb, return its path. Caller unlinks.
std::string make_temp_fb() {
    char tmpl[] = "/tmp/helix_fb0_XXXXXX";
    int  fd     = ::mkstemp(tmpl);
    REQUIRE(fd >= 0);
    REQUIRE(::ftruncate(fd, static_cast<off_t>(kFbSize)) == 0);
    ::close(fd);
    return std::string(tmpl);
}

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
    f.color_format = 16;
    f.src_stride   = src_stride;
    f.src_format   = fmt;
    return f;
}

} // namespace

TEST_CASE("Fb0MailboxSink: full-frame magenta blit", "[remote_screen][fb0]") {
    std::string path = make_temp_fb();

    Fb0MailboxSink sink(path);
    sink.configure_geometry(kFbW, kFbH, kFbStride, 32);
    REQUIRE(sink.start());
    REQUIRE(sink.wants_frames());

    // 480x320 BGRA magenta (B=FF G=00 R=FF A=FF).
    std::vector<uint8_t> src(kFbSize);
    for (size_t i = 0; i < kFbSize; i += 4) {
        src[i + 0] = 0xFF;
        src[i + 1] = 0x00;
        src[i + 2] = 0xFF;
        src[i + 3] = 0xFF;
    }

    sink.on_frame(make_frame(src.data(), 0, 0, kFbW - 1, kFbH - 1, kFbStride));
    sink.stop();

    std::vector<uint8_t> fb = read_file(path);
    REQUIRE(fb.size() == kFbSize);

    // Center pixel (row 160, col 240).
    const size_t center = static_cast<size_t>(160) * kFbStride + static_cast<size_t>(240) * 4;
    REQUIRE(fb[center + 0] == 0xFF);
    REQUIRE(fb[center + 1] == 0x00);
    REQUIRE(fb[center + 2] == 0xFF);
    REQUIRE(fb[center + 3] == 0xFF);

    // Top-left corner.
    REQUIRE(fb[0] == 0xFF);
    REQUIRE(fb[1] == 0x00);
    REQUIRE(fb[2] == 0xFF);
    REQUIRE(fb[3] == 0xFF);

    // Bottom-right corner (row 319, col 479).
    const size_t br = static_cast<size_t>(319) * kFbStride + static_cast<size_t>(479) * 4;
    REQUIRE(fb[br + 0] == 0xFF);
    REQUIRE(fb[br + 2] == 0xFF);

    ::unlink(path.c_str());
}

TEST_CASE("Fb0MailboxSink: partial dirty rect lands at the right offset", "[remote_screen][fb0]") {
    std::string path = make_temp_fb();

    Fb0MailboxSink sink(path);
    sink.configure_geometry(kFbW, kFbH, kFbStride, 32);
    REQUIRE(sink.start());

    // 32x32 solid green (B=00 G=FF R=00 A=FF), tightly packed stride 128.
    constexpr int      kW      = 32;
    constexpr int      kH      = 32;
    constexpr uint32_t kStride = kW * 4; // 128
    std::vector<uint8_t> src(static_cast<size_t>(kStride) * kH);
    for (size_t i = 0; i < src.size(); i += 4) {
        src[i + 0] = 0x00;
        src[i + 1] = 0xFF;
        src[i + 2] = 0x00;
        src[i + 3] = 0xFF;
    }

    // Dirty area (100,50) .. (131,81) inclusive = 32x32.
    sink.on_frame(make_frame(src.data(), 100, 50, 131, 81, kStride));
    sink.stop();

    std::vector<uint8_t> fb = read_file(path);
    REQUIRE(fb.size() == kFbSize);

    // Each dirty row r in [50,82) has green at column 100.
    for (int32_t r = 50; r < 82; ++r) {
        const size_t off = static_cast<size_t>(r) * kFbStride + static_cast<size_t>(100) * 4;
        INFO("row " << r);
        REQUIRE(fb[off + 0] == 0x00);
        REQUIRE(fb[off + 1] == 0xFF);
        REQUIRE(fb[off + 2] == 0x00);
        REQUIRE(fb[off + 3] == 0xFF);
        // Last written column of the rect (col 131).
        const size_t last = static_cast<size_t>(r) * kFbStride + static_cast<size_t>(131) * 4;
        REQUIRE(fb[last + 1] == 0xFF);
    }

    // A pixel outside the rect stays zero (top-left of the framebuffer).
    REQUIRE(fb[0] == 0x00);
    REQUIRE(fb[1] == 0x00);
    REQUIRE(fb[2] == 0x00);
    REQUIRE(fb[3] == 0x00);

    // Just left of the rect (col 99, row 60) is untouched.
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

    // Area (460,300) .. (500,340) = 41x41, extends past the 480x320 fb.
    constexpr int      kW      = 41;
    constexpr int      kH      = 41;
    constexpr uint32_t kStride = kW * 4; // 164
    std::vector<uint8_t> src(static_cast<size_t>(kStride) * kH);
    for (size_t i = 0; i < src.size(); i += 4) {
        src[i + 0] = 0x11;
        src[i + 1] = 0x22;
        src[i + 2] = 0x33;
        src[i + 3] = 0x44;
    }

    // Must not crash or write past the mapping.
    sink.on_frame(make_frame(src.data(), 460, 300, 500, 340, kStride));
    sink.stop();

    std::vector<uint8_t> fb = read_file(path);
    // File size unchanged (no write past the 614400-byte mapping).
    REQUIRE(fb.size() == kFbSize);

    // In-bounds portion written: rows [300,320), cols [460,480).
    const size_t tl = static_cast<size_t>(300) * kFbStride + static_cast<size_t>(460) * 4;
    REQUIRE(fb[tl + 0] == 0x11);
    REQUIRE(fb[tl + 1] == 0x22);
    REQUIRE(fb[tl + 2] == 0x33);
    REQUIRE(fb[tl + 3] == 0x44);

    // Last in-bounds pixel (row 319, col 479).
    const size_t last = static_cast<size_t>(319) * kFbStride + static_cast<size_t>(479) * 4;
    REQUIRE(fb[last + 0] == 0x11);
    REQUIRE(fb[last + 2] == 0x33);

    ::unlink(path.c_str());
}

TEST_CASE("Fb0MailboxSink: RGB565 source is converted to BGRA", "[remote_screen][fb0]") {
    std::string path = make_temp_fb();

    Fb0MailboxSink sink(path);
    sink.configure_geometry(kFbW, kFbH, kFbStride, 32);
    REQUIRE(sink.start());

    // 16x16 RGB565 source, stride = 16*2 = 32 bytes. Fill with pure red in
    // RGB565: R=0x1F, G=0, B=0 -> 0xF800 (stored little-endian: 0x00, 0xF8).
    constexpr int      kW      = 16;
    constexpr int      kH      = 16;
    constexpr uint32_t kStride = kW * 2; // 32
    std::vector<uint8_t> src(static_cast<size_t>(kStride) * kH);
    for (size_t i = 0; i < src.size(); i += 2) {
        src[i + 0] = 0x00; // low byte
        src[i + 1] = 0xF8; // high byte -> 0xF800 = pure red
    }

    // Dirty area (10,10)..(25,25) inclusive = 16x16.
    sink.on_frame(make_frame(src.data(), 10, 10, 25, 25, kStride, RemoteScreenPixelFormat::RGB565));
    sink.stop();

    std::vector<uint8_t> fb = read_file(path);
    REQUIRE(fb.size() == kFbSize);

    // 0xF800 -> R5=0x1F -> R8 = (0x1F<<3)|(0x1F>>2) = 0xFF, G=0, B=0.
    // fb0 is BGRA: [B=0x00, G=0x00, R=0xFF, A=0xFF].
    const size_t off = static_cast<size_t>(12) * kFbStride + static_cast<size_t>(12) * 4;
    REQUIRE(fb[off + 0] == 0x00); // B
    REQUIRE(fb[off + 1] == 0x00); // G
    REQUIRE(fb[off + 2] == 0xFF); // R
    REQUIRE(fb[off + 3] == 0xFF); // A

    // Outside the rect is untouched.
    REQUIRE(fb[0] == 0x00);
    REQUIRE(fb[2] == 0x00);

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

    // Nothing written — the fb stays zero.
    std::vector<uint8_t> fb = read_file(path);
    REQUIRE(fb.size() == kFbSize);
    REQUIRE(fb[0] == 0x00);
    REQUIRE(fb[kFbSize / 2] == 0x00);

    ::unlink(path.c_str());
}

TEST_CASE("Fb0MailboxSink: failed start leaves an inactive no-op sink", "[remote_screen][fb0]") {
    // An unwritable / nonexistent path — open O_RDWR fails.
    Fb0MailboxSink sink("/proc/nonexistent_helix_fb0");
    sink.configure_geometry(kFbW, kFbH, kFbStride, 32);

    REQUIRE_FALSE(sink.start());
    REQUIRE_FALSE(sink.wants_frames());

    // on_frame must be a safe no-op (no map, no crash).
    std::vector<uint8_t> src(kFbSize, 0xFF);
    sink.on_frame(make_frame(src.data(), 0, 0, kFbW - 1, kFbH - 1, kFbStride));

    // stop() on a never-started sink is idempotent.
    sink.stop();
    REQUIRE_FALSE(sink.wants_frames());
}
