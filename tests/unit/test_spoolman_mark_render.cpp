// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

// Regression guard for the chip_spoolman_mark badge in ams_edit_overlay.xml.
//
// The badge originally rendered zero visible pixels. Root cause: an lv_image
// with a small explicit widget size (20x20), a downscaling scale transform,
// and the default inner_align (CENTER). LVGL first re-centers the *unscaled*
// 64x64 source area on the 20x20 widget (top-left lands ~22px outside the
// box), then applies the scale around the pivot — so the shrunk result is
// anchored outside the widget's clip rect and nothing draws. See
// lv_image.c draw_image() (image_area is built from img->w/img->h then
// lv_area_align'd before scaling).
//
// The fix renders the mark at the asset's intrinsic size (a pre-scaled
// spoolman_24.png) with recolor, matching the working AMS-logo draw path.
// These tests lock in that the intrinsic+recolor config renders and that the
// old transform config does not, so the trap isn't reintroduced.

#include "../catch_amalgamated.hpp"
#include "../lvgl_test_fixture.h"

#include <lvgl.h>

namespace {

// Count pixels with non-zero alpha in an ARGB8888 snapshot of the object.
int count_rendered_pixels(lv_obj_t* obj) {
    lv_draw_buf_t* snap = lv_snapshot_take(obj, LV_COLOR_FORMAT_ARGB8888);
    if (!snap) return -1;
    int count = 0;
    const uint8_t* data = snap->data;
    uint32_t w = snap->header.w;
    uint32_t h = snap->header.h;
    uint32_t stride = snap->header.stride ? snap->header.stride : w * 4;
    for (uint32_t y = 0; y < h; ++y) {
        const uint8_t* row = data + y * stride;
        for (uint32_t x = 0; x < w; ++x) {
            if (row[x * 4 + 3] != 0) count++; // ARGB8888 memory order: B,G,R,A
        }
    }
    lv_draw_buf_destroy(snap);
    return count;
}

lv_obj_t* recolored_image(lv_obj_t* parent, const char* path) {
    lv_obj_t* img = lv_image_create(parent);
    lv_image_set_src(img, path);
    lv_obj_set_style_image_recolor(img, lv_color_hex(0x9E9E9E), 0); // ~text_muted
    lv_obj_set_style_image_recolor_opa(img, 255, 0);
    return img;
}

} // namespace

TEST_CASE_METHOD(LVGLTestFixture, "spoolman mark asset decodes at 24px", "[assets]") {
    lv_image_header_t header;
    lv_result_t dec = lv_image_decoder_get_info("A:assets/images/ams/spoolman_24.png", &header);
    INFO("decode=" << (int)dec << " w=" << header.w << " h=" << header.h);
    REQUIRE(dec == LV_RESULT_OK);
    REQUIRE(header.w == 24);
    REQUIRE(header.h == 24);
}

TEST_CASE_METHOD(LVGLTestFixture, "spoolman mark renders at intrinsic size with recolor",
                 "[assets]") {
    // This is the shipped configuration: intrinsic size, recolor, no transform.
    lv_obj_t* img = recolored_image(test_screen(), "A:assets/images/ams/spoolman_24.png");
    process_lvgl(50);

    int px = count_rendered_pixels(img);
    INFO("rendered pixels=" << px);
    REQUIRE(px > 0);
}

TEST_CASE_METHOD(LVGLTestFixture, "small-widget scale transform renders nothing (the trap)",
                 "[assets]") {
    // Reproduces the original broken config to guard against reintroduction:
    // explicit 20x20 size + downscale transform + default CENTER align.
    lv_obj_t* img = lv_image_create(test_screen());
    lv_image_set_src(img, "A:assets/images/ams/spoolman_64.png");
    lv_obj_set_size(img, 20, 20);
    lv_image_set_scale(img, 256 * 20 / 64); // 64px -> 20px
    lv_image_set_pivot(img, 0, 0);
    process_lvgl(50);

    REQUIRE(count_rendered_pixels(img) == 0);
}
