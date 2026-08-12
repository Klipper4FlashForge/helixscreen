// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../../include/temp_graph_internal.h"
#include "../../include/temp_graph_tooltip.h"
#include "../../include/theme_manager.h"
#include "../../include/ui_temp_graph.h"
#include "../ui_test_utils.h"
#include "lvgl/lvgl.h"

#include "../catch_amalgamated.hpp"

using helix::temp_graph_internal::temp_graph_compute_geometry;
using helix::temp_graph_internal::temp_graph_geometry_t;
using helix::temp_graph_internal::TEMP_GRAPH_TOOLTIP_HIT_RADIUS_PX;
using helix::temp_graph_internal::tooltip_hit_test;

// Mirrors TempGraphTestFixture in test_temp_graph.cpp (headless display + a
// parent screen). Kept local rather than shared because that fixture is defined
// inline in its own TU.
class TooltipTestFixture {
  public:
    TooltipTestFixture() {
        lv_init_safe();
        lv_display_t* disp = lv_display_create(800, 480);
        alignas(64) static lv_color_t buf1[800 * 10];
        lv_display_set_buffers(disp, buf1, NULL, sizeof(buf1), LV_DISPLAY_RENDER_MODE_PARTIAL);
        screen = lv_obj_create(NULL);
    }
    lv_obj_t* screen;

    /// Graph with a settled layout, a 0-300 deci range, and a known plot rect.
    ui_temp_graph_t* make_graph() {
        ui_temp_graph_t* g = ui_temp_graph_create(screen);
        REQUIRE(g != nullptr);
        lv_obj_set_size(g->chart, 600, 300);
        ui_temp_graph_set_temp_range(g, 0.0f, 300.0f);
        lv_obj_update_layout(screen);
        return g;
    }

    /// Absolute pixel position of logical sample `idx` for `series_id`, using the
    /// same mapping the chart draws with.
    lv_point_t point_pos(ui_temp_graph_t* g, int series_id, int idx) {
        temp_graph_geometry_t geo{};
        REQUIRE(temp_graph_compute_geometry(g, &geo));
        const int32_t pc = static_cast<int32_t>(geo.point_count);
        int32_t* y = lv_chart_get_y_array(g->chart, g->series_meta[series_id].chart_series);
        uint32_t sp = lv_chart_get_x_start_point(g->chart, g->series_meta[series_id].chart_series);
        int32_t v = y[(sp + idx) % pc];
        lv_point_t p;
        p.x = geo.cx1 + idx * (geo.cw - 1) / (pc - 1);
        p.y = (geo.cy1 + geo.ch) - lv_map(v, geo.y_min, geo.y_max, 0, geo.ch);
        return p;
    }
};

TEST_CASE_METHOD(TooltipTestFixture, "hit test on an empty graph misses", "[ui][tooltip]") {
    ui_temp_graph_t* g = make_graph();
    REQUIRE_FALSE(tooltip_hit_test(g, 300, 150).has_value());
    ui_temp_graph_destroy(g);
}

TEST_CASE_METHOD(TooltipTestFixture, "hit test is null-safe", "[ui][tooltip]") {
    REQUIRE_FALSE(tooltip_hit_test(nullptr, 0, 0).has_value());
}

TEST_CASE_METHOD(TooltipTestFixture, "hit test finds a sample under the tap", "[ui][tooltip]") {
    ui_temp_graph_t* g = make_graph();
    int id = ui_temp_graph_add_series(g, "Nozzle", lv_color_hex(0xFF4444));
    for (int i = 0; i < 20; i++) {
        ui_temp_graph_update_series_with_time(g, id, 100.0f + i, 1000000000000LL + i * 3000);
    }
    const int last = g->point_count - 1;
    lv_point_t p = point_pos(g, id, last);

    auto hit = tooltip_hit_test(g, p.x, p.y);
    REQUIRE(hit.has_value());
    CHECK(hit->series_id == id);
    CHECK(hit->logical_index == last);
    CHECK(hit->deci_temp == 1190); // 119.0 deg, the 20th sample
    CHECK(hit->timestamp_ms == g->latest_point_time_ms);

    ui_temp_graph_destroy(g);
}

TEST_CASE_METHOD(TooltipTestFixture, "hit test misses beyond the radius", "[ui][tooltip]") {
    ui_temp_graph_t* g = make_graph();
    int id = ui_temp_graph_add_series(g, "Nozzle", lv_color_hex(0xFF4444));
    for (int i = 0; i < 20; i++) {
        ui_temp_graph_update_series_with_time(g, id, 100.0f, 1000000000000LL + i * 3000);
    }
    lv_point_t p = point_pos(g, id, g->point_count - 1);

    // Just inside, then clearly outside, straight down from the point.
    REQUIRE(tooltip_hit_test(g, p.x, p.y + TEMP_GRAPH_TOOLTIP_HIT_RADIUS_PX - 2).has_value());
    REQUIRE_FALSE(tooltip_hit_test(g, p.x, p.y + TEMP_GRAPH_TOOLTIP_HIT_RADIUS_PX + 5).has_value());

    ui_temp_graph_destroy(g);
}

TEST_CASE_METHOD(TooltipTestFixture, "hit test picks the nearest of two series", "[ui][tooltip]") {
    ui_temp_graph_t* g = make_graph();
    int hot = ui_temp_graph_add_series(g, "Nozzle", lv_color_hex(0xFF4444));
    int cold = ui_temp_graph_add_series(g, "Bed", lv_color_hex(0x44FF44));
    for (int i = 0; i < 20; i++) {
        int64_t ts = 1000000000000LL + i * 3000;
        ui_temp_graph_update_series_with_time(g, hot, 200.0f, ts);
        ui_temp_graph_update_series_with_time(g, cold, 60.0f, ts);
    }
    const int last = g->point_count - 1;
    lv_point_t hot_p = point_pos(g, hot, last);

    // 3px below the hot line: far from the cold line, which sits much lower.
    auto hit = tooltip_hit_test(g, hot_p.x, hot_p.y + 3);
    REQUIRE(hit.has_value());
    CHECK(hit->series_id == hot);

    ui_temp_graph_destroy(g);
}

TEST_CASE_METHOD(TooltipTestFixture, "hidden series are never candidates", "[ui][tooltip]") {
    ui_temp_graph_t* g = make_graph();
    int id = ui_temp_graph_add_series(g, "Nozzle", lv_color_hex(0xFF4444));
    for (int i = 0; i < 20; i++) {
        ui_temp_graph_update_series_with_time(g, id, 150.0f, 1000000000000LL + i * 3000);
    }
    lv_point_t p = point_pos(g, id, g->point_count - 1);
    REQUIRE(tooltip_hit_test(g, p.x, p.y).has_value());

    ui_temp_graph_show_series(g, id, false);
    REQUIRE_FALSE(tooltip_hit_test(g, p.x, p.y).has_value());

    ui_temp_graph_destroy(g);
}

TEST_CASE_METHOD(TooltipTestFixture, "empty leading slots are never candidates", "[ui][tooltip]") {
    ui_temp_graph_t* g = make_graph();
    int id = ui_temp_graph_add_series(g, "Nozzle", lv_color_hex(0xFF4444));
    // update_series_with_time backfills the WHOLE buffer to the first pushed value
    // (avoids a visual ramp from zero — see ui_temp_graph.cpp), so 3 pushes through
    // it leave no LV_CHART_POINT_NONE slots at all. set_series_data (array mode) is
    // the API that actually produces the "only 3 real samples" state: it clears to
    // POINT_NONE, then writes exactly `count` points. Only 3 samples: logical slots
    // [0, pc-4] are LV_CHART_POINT_NONE.
    float temps[3] = {150.0f, 150.0f, 150.0f};
    ui_temp_graph_set_series_data(g, id, temps, 3);
    temp_graph_geometry_t geo{};
    REQUIRE(temp_graph_compute_geometry(g, &geo));

    // Tap over the far left of the plot, at the same height as the real data.
    lv_point_t real = point_pos(g, id, g->point_count - 1);
    auto hit = tooltip_hit_test(g, geo.cx1 + 2, real.y);
    if (hit.has_value()) {
        CHECK(hit->logical_index >= g->point_count - 3);
    }

    ui_temp_graph_destroy(g);
}
