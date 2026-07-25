// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "screenshot.h"

#include "app_globals.h"
#include "ui_error_reporting.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <lvgl.h>
#include <memory>
#include <string>
#include <vector>

// lodepng.h declares its C++ convenience overloads inside its own extern "C"
// block, so including it from C++ fails to compile. We need exactly one entry
// point out of it — declare that directly. (Built in via LV_USE_LODEPNG.)
//
// Encode to memory, not lodepng_encode32_file(): LVGL routes lodepng's disk I/O
// through lv_fs, which rejects a plain filesystem path for want of a driver
// letter. We write the encoded buffer ourselves.
extern "C" unsigned lodepng_encode32(unsigned char** out, size_t* outsize,
                                     const unsigned char* image, unsigned w, unsigned h);

namespace helix {

bool write_bmp(const char* filename, const uint8_t* data, int width, int height) {
    // RAII for file handle - automatically closes on all return paths
    std::unique_ptr<FILE, decltype(&fclose)> f(fopen(filename, "wb"), fclose);
    if (!f)
        return false;

    // BMP header (54 bytes total)
    uint32_t file_size = 54U + (static_cast<uint32_t>(width) * static_cast<uint32_t>(height) * 4U);
    uint32_t pixel_offset = 54;
    uint32_t dib_size = 40;
    uint16_t planes = 1;
    uint16_t bpp = 32;
    uint32_t reserved = 0;
    uint32_t compression = 0;
    uint32_t ppm = 2835; // pixels per meter
    uint32_t colors = 0;

    // BMP file header (14 bytes)
    fputc('B', f.get());
    fputc('M', f.get());                  // Signature
    fwrite(&file_size, 4, 1, f.get());    // File size
    fwrite(&reserved, 4, 1, f.get());     // Reserved
    fwrite(&pixel_offset, 4, 1, f.get()); // Pixel data offset

    // DIB header (40 bytes)
    fwrite(&dib_size, 4, 1, f.get());    // DIB header size
    fwrite(&width, 4, 1, f.get());       // Width
    fwrite(&height, 4, 1, f.get());      // Height
    fwrite(&planes, 2, 1, f.get());      // Planes
    fwrite(&bpp, 2, 1, f.get());         // Bits per pixel
    fwrite(&compression, 4, 1, f.get()); // Compression (none)
    uint32_t image_size = static_cast<uint32_t>(width) * static_cast<uint32_t>(height) * 4U;
    fwrite(&image_size, 4, 1, f.get()); // Image size
    fwrite(&ppm, 4, 1, f.get());        // X pixels per meter
    fwrite(&ppm, 4, 1, f.get());        // Y pixels per meter
    fwrite(&colors, 4, 1, f.get());     // Colors in palette
    fwrite(&colors, 4, 1, f.get());     // Important colors

    // Write pixel data (BMP is bottom-up, so flip rows)
    for (int y = height - 1; y >= 0; y--) {
        fwrite(data + (static_cast<size_t>(y) * static_cast<size_t>(width) * 4), 4,
               static_cast<size_t>(width), f.get());
    }

    // File automatically closed by unique_ptr destructor
    return true;
}

std::vector<uint8_t> argb8888_to_rgba(const uint8_t* src, size_t pixel_count) {
    std::vector<uint8_t> rgba(pixel_count * 4);
    for (size_t i = 0; i < pixel_count; i++) {
        size_t o = i * 4;
        rgba[o + 0] = src[o + 2]; // R  <- ARGB8888 byte 2
        rgba[o + 1] = src[o + 1]; // G
        rgba[o + 2] = src[o + 0]; // B  <- ARGB8888 byte 0
        rgba[o + 3] = src[o + 3]; // A
    }
    return rgba;
}

bool write_png(const char* filename, const uint8_t* data, int width, int height) {
    if (width <= 0 || height <= 0) {
        return false;
    }
    size_t px = static_cast<size_t>(width) * static_cast<size_t>(height);
    std::vector<uint8_t> rgba = argb8888_to_rgba(data, px);
    unsigned char* encoded = nullptr;
    size_t encoded_size = 0;
    unsigned err = lodepng_encode32(&encoded, &encoded_size, rgba.data(),
                                    static_cast<unsigned>(width), static_cast<unsigned>(height));
    // LVGL builds lodepng with lv_malloc/lv_free as its allocators, so the
    // encoded buffer MUST go back through lv_free — libc free() on an LVGL pool
    // pointer corrupts the heap on any build with a real custom allocator.
    std::unique_ptr<unsigned char, void (*)(void*)> owned(encoded, lv_free);
    if (err != 0 || !encoded) {
        spdlog::error("[Screenshot] PNG encode failed ({}): {}", err, filename);
        return false;
    }

    std::unique_ptr<FILE, decltype(&fclose)> f(fopen(filename, "wb"), fclose);
    if (!f) {
        spdlog::error("[Screenshot] Cannot open for writing: {}", filename);
        return false;
    }
    if (fwrite(encoded, 1, encoded_size, f.get()) != encoded_size) {
        spdlog::error("[Screenshot] Short write: {}", filename);
        return false;
    }
    return true;
}

namespace {

bool has_suffix_ci(const std::string& s, const std::string& suffix) {
    if (s.size() < suffix.size()) {
        return false;
    }
    return std::equal(suffix.rbegin(), suffix.rend(), s.rbegin(), [](char a, char b) {
        return std::tolower(static_cast<unsigned char>(a)) ==
               std::tolower(static_cast<unsigned char>(b));
    });
}

} // namespace

std::string save_screenshot(const std::string& out_path) {
    // No destination given -> timestamped BMP in the writable runtime dir.
    std::string filename =
        out_path.empty() ? app_get_runtime_dir() + "/ui-screenshot-" +
                               std::to_string(static_cast<unsigned long>(time(nullptr))) + ".bmp"
                         : out_path;
    bool as_png = has_suffix_ci(filename, ".png");

    // Take snapshot using LVGL's native API (platform-independent)
    lv_obj_t* screen = lv_screen_active();
    lv_draw_buf_t* snapshot = lv_snapshot_take(screen, LV_COLOR_FORMAT_ARGB8888);

    if (!snapshot) {
        spdlog::error("[Screenshot] Failed to take screenshot");
        return {};
    }

    // lv_snapshot_take() only captures the active screen. Full-screen overlays that
    // live on the top layer (e.g. the PIN lock screen) are rendered separately and
    // would otherwise be missing. Composite the top layer over the base when present.
    lv_obj_t* top = lv_layer_top();
    if (top && lv_obj_get_child_count(top) > 0) {
        lv_draw_buf_t* top_snap = lv_snapshot_take(top, LV_COLOR_FORMAT_ARGB8888);
        if (top_snap && top_snap->header.w == snapshot->header.w &&
            top_snap->header.h == snapshot->header.h) {
            // ARGB8888 memory order is B,G,R,A per pixel. Alpha-blend top over base.
            uint8_t* base = snapshot->data;
            const uint8_t* over = top_snap->data;
            size_t px = static_cast<size_t>(snapshot->header.w) * snapshot->header.h;
            for (size_t i = 0; i < px; i++) {
                size_t o = i * 4;
                uint32_t a = over[o + 3];
                if (a == 0)
                    continue; // fully transparent — keep base pixel
                uint32_t ia = 255U - a;
                base[o + 0] = static_cast<uint8_t>((over[o + 0] * a + base[o + 0] * ia) / 255U);
                base[o + 1] = static_cast<uint8_t>((over[o + 1] * a + base[o + 1] * ia) / 255U);
                base[o + 2] = static_cast<uint8_t>((over[o + 2] * a + base[o + 2] * ia) / 255U);
                base[o + 3] = 255;
            }
        }
        if (top_snap)
            lv_draw_buf_destroy(top_snap);
    }

    bool ok = as_png ? write_png(filename.c_str(), snapshot->data, snapshot->header.w,
                                 snapshot->header.h)
                     : write_bmp(filename.c_str(), snapshot->data, snapshot->header.w,
                                 snapshot->header.h);
    if (ok) {
        spdlog::info("[Screenshot] saved: {}", filename);
    } else {
        NOTIFY_ERROR("Failed to save screenshot");
        LOG_ERROR_INTERNAL("Failed to save screenshot to {}", filename);
        filename.clear();
    }

    // Free snapshot buffer
    lv_draw_buf_destroy(snapshot);
    return filename;
}

} // namespace helix
