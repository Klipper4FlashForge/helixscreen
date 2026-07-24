// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "screenshot.h"

#include "app_globals.h"
#include "ui_error_reporting.h"

#include <spdlog/spdlog.h>

#include <cstdio>
#include <ctime>
#include <lvgl.h>
#include <memory>
#include <string>

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

std::string save_screenshot() {
    // Generate unique filename with timestamp in the writable runtime dir
    std::string filename = app_get_runtime_dir() + "/ui-screenshot-" +
                           std::to_string(static_cast<unsigned long>(time(nullptr))) + ".bmp";

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

    // Write BMP file
    if (write_bmp(filename.c_str(), snapshot->data, snapshot->header.w, snapshot->header.h)) {
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
