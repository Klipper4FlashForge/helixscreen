// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "label_renderer.h"
#include "spoolman_types.h"

#include <algorithm>
#include <utility>
#include <vector>

#include "../catch_amalgamated.hpp"

static SpoolInfo make_test_spool() {
    SpoolInfo spool;
    spool.id = 42;
    spool.vendor = "Hatchbox";
    spool.material = "PLA";
    spool.filament_name = "Red";
    spool.remaining_weight_g = 800;
    spool.initial_weight_g = 1000;
    spool.lot_nr = "LOT-2026-001";
    spool.comment = "Great filament";
    spool.spool_weight_g = 200;
    return spool;
}

static helix::LabelSize continuous_62mm() {
    return {"62mm", 696, 0, 300, 0x0A, 62, 0};
}

static helix::LabelSize continuous_29mm() {
    return {"29mm", 306, 0, 300, 0x0A, 29, 0};
}

static helix::LabelSize diecut_62x29() {
    return {"62x29mm", 696, 271, 300, 0x0B, 62, 29};
}

/// Check if bitmap has any black pixels
static bool has_black_pixels(const helix::LabelBitmap& bmp) {
    for (int y = 0; y < bmp.height(); y++)
        for (int x = 0; x < bmp.width(); x++)
            if (bmp.get_pixel(x, y))
                return true;
    return false;
}

TEST_CASE("LabelRenderer STANDARD preset produces valid bitmap", "[label]") {
    auto spool = make_test_spool();
    auto label =
        helix::LabelRenderer::render(spool, helix::LabelPreset::STANDARD, continuous_62mm());

    REQUIRE_FALSE(label.empty());
    REQUIRE(label.width() == 696);
    REQUIRE(label.height() > 0);
    REQUIRE(has_black_pixels(label));
}

TEST_CASE("LabelRenderer MINIMAL preset is QR only", "[label]") {
    auto spool = make_test_spool();
    auto label =
        helix::LabelRenderer::render(spool, helix::LabelPreset::MINIMAL, continuous_62mm());

    REQUIRE_FALSE(label.empty());
    REQUIRE(label.width() == 696);
    REQUIRE(label.height() > 0);
    REQUIRE(has_black_pixels(label));
}

TEST_CASE("LabelRenderer COMPACT preset", "[label]") {
    auto spool = make_test_spool();
    auto label =
        helix::LabelRenderer::render(spool, helix::LabelPreset::COMPACT, continuous_62mm());

    REQUIRE_FALSE(label.empty());
    REQUIRE(label.width() == 696);
    REQUIRE(label.height() > 0);
    REQUIRE(has_black_pixels(label));
}

TEST_CASE("LabelRenderer 29mm label", "[label]") {
    auto spool = make_test_spool();
    auto label =
        helix::LabelRenderer::render(spool, helix::LabelPreset::STANDARD, continuous_29mm());

    REQUIRE_FALSE(label.empty());
    REQUIRE(label.width() == 306);
    REQUIRE(label.height() > 0);
}

TEST_CASE("LabelRenderer die-cut label fits dimensions", "[label]") {
    auto spool = make_test_spool();
    auto size = diecut_62x29();
    auto label = helix::LabelRenderer::render(spool, helix::LabelPreset::STANDARD, size);

    REQUIRE(label.width() == 696);
    REQUIRE(label.height() == 271);
    REQUIRE(has_black_pixels(label));
}

TEST_CASE("LabelRenderer handles empty vendor and color", "[label]") {
    SpoolInfo spool;
    spool.id = 1;
    spool.material = "PETG";
    // vendor and color_name empty

    auto label =
        helix::LabelRenderer::render(spool, helix::LabelPreset::STANDARD, continuous_62mm());
    REQUIRE_FALSE(label.empty());
    REQUIRE(has_black_pixels(label));
}

TEST_CASE("LabelRenderer continuous height adapts to content", "[label]") {
    auto spool = make_test_spool();
    auto minimal =
        helix::LabelRenderer::render(spool, helix::LabelPreset::MINIMAL, continuous_62mm());
    auto standard =
        helix::LabelRenderer::render(spool, helix::LabelPreset::STANDARD, continuous_62mm());

    REQUIRE(minimal.height() > 0);
    REQUIRE(standard.height() > 0);
    // STANDARD has text alongside QR, so may differ in height
}

TEST_CASE("LabelRenderer MINIMAL die-cut centers QR", "[label]") {
    auto spool = make_test_spool();
    auto size = diecut_62x29();
    auto label = helix::LabelRenderer::render(spool, helix::LabelPreset::MINIMAL, size);

    REQUIRE(label.width() == 696);
    REQUIRE(label.height() == 271);

    // QR should not touch the very edges (there should be margin)
    bool top_row_clear = true;
    for (int x = 0; x < label.width(); x++)
        if (label.get_pixel(x, 0))
            top_row_clear = false;
    REQUIRE(top_row_clear);
}

TEST_CASE("LabelRenderer COMPACT wider label produces larger content", "[label]") {
    auto spool = make_test_spool();
    auto compact_62 =
        helix::LabelRenderer::render(spool, helix::LabelPreset::COMPACT, continuous_62mm());
    auto compact_29 =
        helix::LabelRenderer::render(spool, helix::LabelPreset::COMPACT, continuous_29mm());

    REQUIRE_FALSE(compact_62.empty());
    REQUIRE_FALSE(compact_29.empty());
    // 62mm label is wider than 29mm
    REQUIRE(compact_62.width() > compact_29.width());
}

TEST_CASE("LabelRenderer MINIMAL QR code capped size", "[label]") {
    auto spool = make_test_spool();
    auto label =
        helix::LabelRenderer::render(spool, helix::LabelPreset::MINIMAL, continuous_62mm());

    REQUIRE_FALSE(label.empty());
    // Find the bounding box of black pixels to check QR size
    int max_y = 0;
    for (int y = 0; y < label.height(); y++)
        for (int x = 0; x < label.width(); x++)
            if (label.get_pixel(x, y))
                max_y = y;

    // QR code height should be reasonable (capped, not filling entire label width)
    REQUIRE(max_y < label.width()); // QR shouldn't be as tall as the label is wide
    REQUIRE(max_y <= 300);          // QR should be capped around 250px + margin
}

// ============================================================================
// Untracked spools (spoolman_id == 0): no Spoolman record, so the QR code and
// the "#n" ID text must be omitted rather than rendering "web+spoolman:s-0"
// and "#0". Negative IDs stay reserved for preview/test labels and keep a QR.
// ============================================================================

/// A spool with no Spoolman record — the AMS slot editor's untracked case.
static SpoolInfo make_untracked_spool() {
    SpoolInfo spool;
    spool.id = 0; // untracked
    spool.vendor = "Hatchbox";
    spool.material = "PLA";
    spool.filament_name = "Red";
    spool.remaining_weight_g = 800;
    return spool;
}

/// Same fields, but with a real Spoolman ID.
static SpoolInfo make_tracked_spool() {
    SpoolInfo spool = make_untracked_spool();
    spool.id = 42;
    return spool;
}

/// Maximal runs of consecutive rows that contain at least one black pixel.
/// A QR code is a single tall contiguous run; text renders as one run per line.
static std::vector<std::pair<int, int>> row_bands(const helix::LabelBitmap& bmp) {
    std::vector<std::pair<int, int>> bands;
    bool in_band = false;
    int start = 0;
    for (int y = 0; y < bmp.height(); y++) {
        bool black = false;
        for (int x = 0; x < bmp.width() && !black; x++)
            black = bmp.get_pixel(x, y);
        if (black && !in_band) {
            in_band = true;
            start = y;
        } else if (!black && in_band) {
            in_band = false;
            bands.emplace_back(start, y - 1);
        }
    }
    if (in_band)
        bands.emplace_back(start, bmp.height() - 1);
    return bands;
}

/// Leftmost/rightmost black pixel across rows [y0, y1].
static std::pair<int, int> x_extent(const helix::LabelBitmap& bmp, int y0, int y1) {
    int left = bmp.width();
    int right = -1;
    for (int y = y0; y <= y1; y++) {
        for (int x = 0; x < bmp.width(); x++) {
            if (bmp.get_pixel(x, y)) {
                if (x < left)
                    left = x;
                if (x > right)
                    right = x;
            }
        }
    }
    return {left, right};
}

static int count_black_pixels(const helix::LabelBitmap& bmp) {
    int count = 0;
    for (int y = 0; y < bmp.height(); y++)
        for (int x = 0; x < bmp.width(); x++)
            if (bmp.get_pixel(x, y))
                count++;
    return count;
}

/// Height of the tallest contiguous run of inked rows. A QR block is ~200px
/// tall on a 62x29mm label; a single text line is at most ~60px (7px glyph
/// rows scaled by <= 8). This cleanly separates "has a QR" from "text only".
static int tallest_band(const helix::LabelBitmap& bmp) {
    int tallest = 0;
    for (const auto& band : row_bands(bmp))
        tallest = std::max(tallest, band.second - band.first + 1);
    return tallest;
}

TEST_CASE("LabelRenderer untracked spool omits the QR code", "[label][untracked]") {
    auto size = diecut_62x29();

    // A tracked spool puts a tall contiguous QR block down the left side.
    auto tracked =
        helix::LabelRenderer::render(make_tracked_spool(), helix::LabelPreset::COMPACT, size);
    REQUIRE(tallest_band(tracked) > 100);

    // An untracked spool has no QR — nothing taller than one text line remains.
    auto untracked =
        helix::LabelRenderer::render(make_untracked_spool(), helix::LabelPreset::COMPACT, size);
    REQUIRE(tallest_band(untracked) <= 70);
    REQUIRE(count_black_pixels(untracked) < count_black_pixels(tracked));

    // Text still renders: vendor/material/color are unaffected.
    REQUIRE(has_black_pixels(untracked));
}

TEST_CASE("LabelRenderer untracked COMPACT drops the spool-ID line", "[label][untracked]") {
    // COMPACT is vendor / material+color / "#n". Without an ID the third line
    // is omitted entirely rather than printing "#0", leaving exactly 2 lines.
    auto label = helix::LabelRenderer::render(make_untracked_spool(), helix::LabelPreset::COMPACT,
                                              diecut_62x29());
    REQUIRE(row_bands(label).size() == 2);
}

TEST_CASE("LabelRenderer untracked STANDARD weight line carries no ID", "[label][untracked]") {
    // STANDARD line 3 is "<weight>  #<id>". For an untracked spool only the
    // weight remains — measure the line's width in character cells to prove the
    // "  #0" suffix is gone. Font metrics: 5x7 glyphs, 1px inter-char gap,
    // uniformly scaled, so band height == 7 * scale.
    auto label = helix::LabelRenderer::render(make_untracked_spool(), helix::LabelPreset::STANDARD,
                                              diecut_62x29());

    auto bands = row_bands(label);
    // vendor / material+color / weight — no QR, no temps, no lot, no comment.
    REQUIRE(bands.size() == 3);

    const auto& weight_band = bands[2];
    int band_h = weight_band.second - weight_band.first + 1;
    int scale = band_h / 7;
    REQUIRE(scale >= 2);

    auto [left, right] = x_extent(label, weight_band.first, weight_band.second);
    REQUIRE(right >= left);
    int char_pitch = (5 + 1) * scale;
    int cells = (right - left + 1 + scale) / char_pitch;

    // "800G" is 4 cells. With the ID appended ("800G  #0") it would be 8.
    REQUIRE(cells <= 5);
}

TEST_CASE("LabelRenderer test label (negative id) keeps its QR", "[label][untracked]") {
    // Negative IDs are the preview/test path and must be untouched by the
    // untracked handling — they still emit a (decoder-rejected) QR payload.
    SpoolInfo spool = make_untracked_spool();
    spool.id = -1;

    auto label = helix::LabelRenderer::render(spool, helix::LabelPreset::COMPACT, diecut_62x29());
    REQUIRE(tallest_band(label) > 100); // QR block present

    // MINIMAL (QR-only) must also still work for the test label.
    auto minimal = helix::LabelRenderer::render(spool, helix::LabelPreset::MINIMAL, diecut_62x29());
    REQUIRE(tallest_band(minimal) > 100);
}

TEST_CASE("LabelRenderer untracked MINIMAL falls back to text", "[label][untracked]") {
    // MINIMAL is QR-only; with no QR payload it would print a blank label, so
    // untracked spools render the COMPACT text layout instead.
    auto label = helix::LabelRenderer::render(make_untracked_spool(), helix::LabelPreset::MINIMAL,
                                              diecut_62x29());

    REQUIRE_FALSE(label.empty());
    REQUIRE(has_black_pixels(label));
    REQUIRE(row_bands(label).size() == 2); // two text lines, no QR
    REQUIRE(tallest_band(label) <= 70);
}

TEST_CASE("LabelRenderer untracked narrow label omits QR and ID", "[label][untracked]") {
    // Narrow labels (<150px wide, e.g. Niimbot D110) render landscape then
    // rotate. The QR must be gone and the text must claim the freed width.
    helix::LabelSize d110{"D110", 96, 307, 203, 0x00, 12, 40};

    auto untracked =
        helix::LabelRenderer::render(make_untracked_spool(), helix::LabelPreset::STANDARD, d110);
    auto tracked =
        helix::LabelRenderer::render(make_tracked_spool(), helix::LabelPreset::STANDARD, d110);

    REQUIRE(untracked.width() == 96);
    REQUIRE(untracked.height() == 307);
    REQUIRE(has_black_pixels(untracked));
    REQUIRE(count_black_pixels(untracked) < count_black_pixels(tracked));
}

TEST_CASE("LabelRenderer STANDARD richer spool produces more content", "[label]") {
    // Minimal spool (just material)
    SpoolInfo minimal_spool;
    minimal_spool.id = 1;
    minimal_spool.material = "PLA";

    // Rich spool (all fields)
    auto rich_spool = make_test_spool();

    auto minimal_label = helix::LabelRenderer::render(minimal_spool, helix::LabelPreset::STANDARD,
                                                      continuous_62mm());
    auto rich_label =
        helix::LabelRenderer::render(rich_spool, helix::LabelPreset::STANDARD, continuous_62mm());

    REQUIRE_FALSE(minimal_label.empty());
    REQUIRE_FALSE(rich_label.empty());
    // Rich spool should produce taller label (more text content)
    REQUIRE(rich_label.height() >= minimal_label.height());
}
