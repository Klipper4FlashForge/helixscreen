// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_print_start_gates.cpp
 * @brief Pure-rule tests for the print-start gate core (print_start_checks.h).
 *
 * Run with: ./build/bin/helix-tests "[print-start][gate-pipeline]"
 */

#include "ams_types.h"
#include "filament_mapper.h"
#include "moonraker_types.h"
#include "print_start_checks.h"

#include "../catch_amalgamated.hpp"

using namespace helix;

namespace {
/// Minimal context with the field(s) a rule reads; everything else default.
PrintStartContext ctx_with(std::function<void(PrintStartContext&)> seed) {
    PrintStartContext ctx;
    seed(ctx);
    return ctx;
}
} // namespace

// ---------------------------------------------------------------------------
// unresolved_tools_in — ported from PrintStartController::unresolved_tools_for
// ---------------------------------------------------------------------------

TEST_CASE("unresolved_tools_in: single-color never warns", "[print-start][gate-pipeline]") {
    auto ctx = ctx_with([](PrintStartContext& c) {
        c.filament_color_count = 1;
        ToolMapping m; // would be unresolved if evaluated
        m.tool_index = 0;
        m.is_auto = true;
        c.mappings = {m};
    });
    CHECK(unresolved_tools_in(ctx).empty());
}

TEST_CASE("unresolved_tools_in: bypass suppresses", "[print-start][gate-pipeline]") {
    auto ctx = ctx_with([](PrintStartContext& c) {
        c.filament_color_count = 3;
        c.any_bypass_active = true;
        ToolMapping m;
        m.tool_index = 0;
        m.is_auto = true;
        c.mappings = {m};
    });
    CHECK(unresolved_tools_in(ctx).empty());
}

TEST_CASE("unresolved_tools_in: empty mappings stay silent", "[print-start][gate-pipeline]") {
    auto ctx = ctx_with([](PrintStartContext& c) { c.filament_color_count = 3; });
    CHECK(unresolved_tools_in(ctx).empty());
}

TEST_CASE("unresolved_tools_in: multi-color unresolved tool is reported",
          "[print-start][gate-pipeline]") {
    auto ctx = ctx_with([](PrintStartContext& c) {
        c.filament_color_count = 2;
        ToolMapping unresolved;
        unresolved.tool_index = 1;
        unresolved.is_auto = true;
        ToolMapping resolved;
        resolved.tool_index = 0;
        resolved.mapped_slot = 2;
        c.mappings = {resolved, unresolved};
    });
    auto out = unresolved_tools_in(ctx);
    REQUIRE(out.size() == 1);
    CHECK(out[0] == 1);
}

// ---------------------------------------------------------------------------
// insufficient_spool_weight_in — ported from initiate() inline math
// ---------------------------------------------------------------------------

TEST_CASE("insufficient_spool_weight_in: no spool / no weight / no metadata",
          "[print-start][gate-pipeline]") {
    CHECK_FALSE(insufficient_spool_weight_in(ctx_with([](PrintStartContext&) {})).has_value());
    CHECK_FALSE(insufficient_spool_weight_in(ctx_with([](PrintStartContext& c) {
                    SlotInfo spool;
                    spool.remaining_weight_g = 5.0f;
                    c.external_spool = spool; // no metadata
                })).has_value());
    CHECK_FALSE(insufficient_spool_weight_in(ctx_with([](PrintStartContext& c) {
                    SlotInfo spool;
                    spool.remaining_weight_g = 0.0f;
                    c.external_spool = spool;
                    FileMetadata md;
                    md.filament_weight_total = 100.0;
                    c.metadata = md;
                })).has_value());
}

TEST_CASE("insufficient_spool_weight_in: weight from metadata, enough vs short",
          "[print-start][gate-pipeline]") {
    auto enough = ctx_with([](PrintStartContext& c) {
        SlotInfo spool;
        spool.remaining_weight_g = 50.0f;
        spool.material = "PLA";
        c.external_spool = spool;
        FileMetadata md;
        md.filament_weight_total = 40.0;
        c.metadata = md;
    });
    CHECK_FALSE(insufficient_spool_weight_in(enough).has_value());

    auto short_ = ctx_with([](PrintStartContext& c) {
        SlotInfo spool;
        spool.remaining_weight_g = 30.0f;
        spool.material = "PLA";
        c.external_spool = spool;
        FileMetadata md;
        md.filament_weight_total = 40.0;
        c.metadata = md;
    });
    auto r = insufficient_spool_weight_in(short_);
    REQUIRE(r.has_value());
    CHECK(r->first == 40.0f);
    CHECK(r->second == 30.0f);
}

TEST_CASE("insufficient_spool_weight_in: length fallback via material density",
          "[print-start][gate-pipeline]") {
    auto ctx = ctx_with([](PrintStartContext& c) {
        SlotInfo spool;
        spool.remaining_weight_g = 5.0f;
        spool.material = "PLA";
        c.external_spool = spool;
        FileMetadata md;
        md.filament_weight_total = 0.0;
        md.filament_total = 100000.0; // 100000mm (100m)
        c.metadata = md;
    });
    auto r = insufficient_spool_weight_in(ctx);
    REQUIRE(r.has_value());
    // 100m of 1.75mm PLA at 1.24 g/cm3 ≈ 298g — must exceed 5g by a wide margin.
    CHECK(r->first > 250.0f);
    CHECK(r->second == 5.0f);
}

// ---------------------------------------------------------------------------
// material_mismatches_in — ported from find_material_mismatches()
// ---------------------------------------------------------------------------

TEST_CASE("material_mismatches_in: no detail view -> none", "[print-start][gate-pipeline]") {
    CHECK(material_mismatches_in(ctx_with([](PrintStartContext&) {})).empty());
}

TEST_CASE("material_mismatches_in: AMS path flags mismatched mapped tool",
          "[print-start][gate-pipeline]") {
    auto ctx = ctx_with([](PrintStartContext& c) {
        c.has_detail_view = true;
        c.ams_available = true;
        ToolMapping m;
        m.tool_index = 0;
        m.mapped_slot = 0;
        m.material_mismatch = true;
        c.mappings = {m};
        GcodeToolInfo t;
        t.tool_index = 0;
        t.material = "PETG";
        c.tool_info = {t};
        AvailableSlot s;
        s.slot_index = 0;
        s.backend_index = -1;
        s.material = "PLA";
        c.available_slots = {s};
    });
    auto out = material_mismatches_in(ctx);
    REQUIRE(out.size() == 1);
    CHECK(out[0].expected_material == "PETG");
    CHECK(out[0].loaded_material == "PLA");
}

TEST_CASE("material_mismatches_in: zero-usage tool is skipped when weights known",
          "[print-start][gate-pipeline]") {
    auto ctx = ctx_with([](PrintStartContext& c) {
        c.has_detail_view = true;
        c.ams_available = true;
        ToolMapping m;
        m.tool_index = 0;
        m.mapped_slot = 0;
        m.material_mismatch = true;
        c.mappings = {m};
        GcodeToolInfo t;
        t.tool_index = 0;
        t.material = "PETG";
        c.tool_info = {t};
        AvailableSlot s;
        s.slot_index = 0;
        s.backend_index = -1;
        s.material = "PLA";
        c.available_slots = {s};
        FileMetadata md;
        md.filament_weights = {0.0};
        c.metadata = md;
    });
    CHECK(material_mismatches_in(ctx).empty());
}

TEST_CASE("material_mismatches_in: unknown material on either side is skipped",
          "[print-start][gate-pipeline]") {
    auto ctx = ctx_with([](PrintStartContext& c) {
        c.has_detail_view = true;
        c.ams_available = true;
        ToolMapping m;
        m.tool_index = 0;
        m.mapped_slot = 0;
        m.material_mismatch = true;
        c.mappings = {m}; // tool_info empty -> expected unknown
        AvailableSlot s;
        s.slot_index = 0;
        s.backend_index = -1;
        s.material = "PLA";
        c.available_slots = {s};
    });
    CHECK(material_mismatches_in(ctx).empty());
}

TEST_CASE("material_mismatches_in: non-AMS external spool mismatch",
          "[print-start][gate-pipeline]") {
    auto ctx = ctx_with([](PrintStartContext& c) {
        c.has_detail_view = true;
        c.ams_available = false;
        c.filament_materials = {"ABS"};
        SlotInfo spool;
        spool.material = "PLA";
        c.external_spool = spool;
    });
    auto out = material_mismatches_in(ctx);
    REQUIRE(out.size() == 1);
    CHECK(out[0].expected_material == "ABS");
    CHECK(out[0].loaded_material == "PLA");
}
