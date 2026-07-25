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
