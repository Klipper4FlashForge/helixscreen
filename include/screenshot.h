// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/**
 * @file screenshot.h
 * @brief Screenshot capture utilities
 *
 * Provides BMP screenshot capture functionality using LVGL's snapshot API.
 */

#include <cstdint>
#include <string>
#include <vector>

struct _lv_obj_t;
typedef struct _lv_obj_t lv_obj_t;

namespace helix {

/**
 * @brief A captured frame in RGBA8888 (R,G,B,A per pixel), already composited
 *        (active screen + top layer, per capture_frame()'s doc comment).
 */
struct CapturedFrame {
    std::vector<uint8_t> rgba;
    int width = 0;
    int height = 0;
};

/**
 * @brief Capture the current screen into memory, without encoding it
 *
 * Takes an `lv_snapshot_take()` of the active screen and alpha-composites the
 * top layer over it (full-screen overlays like the PIN lock screen render
 * there and would otherwise be missing) — the same compositing
 * `save_screenshot()` has always done, now available without writing a file.
 *
 * @param out Filled with the composited frame on success. Left untouched on failure.
 * @param crop_to When non-null, the result is this widget's bounding box
 *        instead of the whole screen. The box is clamped to the captured
 *        buffer — a widget can extend past the screen edge — and capture
 *        fails cleanly if the clamped region is empty (offscreen or
 *        zero-size widget).
 * @return true on success, false on failure (logged via spdlog)
 */
bool capture_frame(CapturedFrame& out, lv_obj_t* crop_to = nullptr);

/**
 * @brief FNV-1a hash of a frame's pixels, for stability comparison
 *
 * Not a security hash — just cheap and sensitive to any pixel changing, so
 * `screenshot --stable` can poll until consecutive frames match.
 */
uint64_t frame_hash(const CapturedFrame& frame);

/**
 * @brief Encode and write an already-captured frame to disk
 *
 * @param frame The frame to write (from capture_frame())
 * @param out_path Destination file. Empty (the default) writes a timestamped
 *        `ui-screenshot-<timestamp>.bmp` in the runtime dir. A path ending in
 *        `.png` is encoded as PNG; anything else is written as BMP.
 * @return The filename actually written, empty string on failure
 */
std::string write_frame(const CapturedFrame& frame, const std::string& out_path = "");

/**
 * @brief Write raw ARGB8888 pixel data to a BMP file
 * @param filename Output file path
 * @param data Pixel data (ARGB8888 format)
 * @param width Image width in pixels
 * @param height Image height in pixels
 * @return true on success, false on failure
 */
bool write_bmp(const char* filename, const uint8_t* data, int width, int height);

/**
 * @brief Convert LVGL ARGB8888 pixels to the RGBA byte order PNG encoders want
 *
 * LVGL's ARGB8888 is B,G,R,A in memory. Swapping the R and B channels is the
 * whole of the conversion — get it wrong and images encode with red and blue
 * transposed, which is valid output and easy to miss.
 *
 * @param src Source pixels, ARGB8888 (B,G,R,A per pixel)
 * @param pixel_count Number of pixels (not bytes)
 * @return Freshly allocated R,G,B,A buffer of pixel_count * 4 bytes
 */
std::vector<uint8_t> argb8888_to_rgba(const uint8_t* src, size_t pixel_count);

/**
 * @brief Write raw ARGB8888 pixel data to a PNG file
 *
 * Encodes via lodepng (already linked for LV_USE_LODEPNG). Input is LVGL's
 * ARGB8888, whose memory order is B,G,R,A — the channels are swizzled to the
 * R,G,B,A order lodepng expects.
 *
 * @param filename Output file path
 * @param data Pixel data (ARGB8888 format)
 * @param width Image width in pixels
 * @param height Image height in pixels
 * @return true on success, false on failure
 */
bool write_png(const char* filename, const uint8_t* data, int width, int height);

/**
 * @brief Take a screenshot of the active LVGL screen and write it to disk
 *
 * A thin wrapper around capture_frame() + write_frame() — kept as its own
 * entry point for the callers that only ever want "capture the whole screen
 * and save it now" (SIGUSR1, the 'S' key shortcut, the auto-screenshot loop
 * handler) without touching the frame in between.
 *
 * @param out_path Destination file. Empty (the default) writes a timestamped
 *        `ui-screenshot-<timestamp>.bmp` in the runtime dir. A path ending in
 *        `.png` is encoded as PNG; anything else is written as BMP.
 * @return The filename actually written, empty string on failure
 */
std::string save_screenshot(const std::string& out_path = "");

} // namespace helix
