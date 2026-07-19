// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// ESP32-only (Task 11 R2). PSRAM-resident PNG thumbnail wrapper for the
// print-select card view. LittleFS is too small for a disk thumbnail cache
// on this platform (Task 10 R6 hard gate — see thumbnail_cache.cpp), so
// thumbnails fetched via download_file_partial are decoded straight into an
// lv_image_dsc_t backed by a PSRAM buffer instead of a cache file. LVGL's
// lodepng decoder (LV_USE_LODEPNG, enabled in the ESP32 lv_conf.h override)
// decodes the raw PNG bytes on demand at draw time — lv_image_src_get_type()
// auto-detects an lv_image_dsc_t* via its header.magic byte, so no manual
// RGB/ARGB pre-decode or separate API call is needed here.
#if defined(HELIX_PLATFORM_ESP32)

#include "lvgl.h"

#include "esp_heap_caps.h"

#include <cstring>
#include <memory>
#include <string>

namespace helix::ui {

/**
 * @brief Owns one PSRAM-allocated PNG byte buffer plus the lv_image_dsc_t
 *        wrapping it, for a single print-file thumbnail.
 *
 * Instances are shared_ptr-managed and held both by the PrintFileData entry
 * (source of truth) and by the card widget slot currently displaying it
 * (CardWidgetData::esp_thumbnail) so the buffer stays alive for as long as
 * any widget's `src` still points at its descriptor, independent of when the
 * owning PrintFileData is replaced during a list refresh/sort.
 */
class EspPsramThumbnail {
  public:
    EspPsramThumbnail(const EspPsramThumbnail&) = delete;
    EspPsramThumbnail& operator=(const EspPsramThumbnail&) = delete;

    ~EspPsramThumbnail() {
        if (data_) {
            heap_caps_free(data_);
        }
    }

    /// Copies png_bytes into a fresh PSRAM allocation and builds the
    /// lv_image_dsc_t wrapper. Returns nullptr if the PSRAM allocation fails
    /// — never falls back to internal RAM (Task 11 R2 hard constraint).
    static std::shared_ptr<EspPsramThumbnail> create(const std::string& png_bytes) {
        if (png_bytes.empty()) {
            return nullptr;
        }
        auto* buf = static_cast<uint8_t*>(heap_caps_malloc(png_bytes.size(), MALLOC_CAP_SPIRAM));
        if (!buf) {
            return nullptr;
        }
        memcpy(buf, png_bytes.data(), png_bytes.size());
        // Private ctor blocks make_shared; a single small control-block alloc
        // per thumbnail fetch is not perf sensitive (not per-frame).
        return std::shared_ptr<EspPsramThumbnail>(new EspPsramThumbnail(buf, png_bytes.size()));
    }

    /// Pointer suitable for lv_image_set_src().
    const lv_image_dsc_t* dsc() const {
        return &dsc_;
    }

  private:
    EspPsramThumbnail(uint8_t* data, size_t size) : data_(data) {
        dsc_.header.magic = LV_IMAGE_HEADER_MAGIC;
        dsc_.header.cf = LV_COLOR_FORMAT_RAW;
        dsc_.header.flags = LV_IMAGE_FLAGS_COMPRESSED;
        dsc_.header.w = 0;
        dsc_.header.h = 0;
        dsc_.data_size = static_cast<uint32_t>(size);
        dsc_.data = data_;
    }

    uint8_t* data_ = nullptr;
    lv_image_dsc_t dsc_{};
};

} // namespace helix::ui

#endif // HELIX_PLATFORM_ESP32
