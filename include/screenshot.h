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

namespace helix {

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
 * @brief Take a screenshot of the active LVGL screen
 *
 * @param out_path Destination file. Empty (the default) writes a timestamped
 *        `ui-screenshot-<timestamp>.bmp` in the runtime dir. A path ending in
 *        `.png` is encoded as PNG; anything else is written as BMP.
 * @return The filename actually written, empty string on failure
 */
std::string save_screenshot(const std::string& out_path = "");

} // namespace helix
