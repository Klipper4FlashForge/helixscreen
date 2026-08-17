// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file print_start_checks.cpp
 * @brief Pure-rule implementations for the print-start gate pipeline.
 *
 * Rule bodies are ports of the imperative checks in
 * src/ui/ui_print_start_controller.cpp (unresolved_tools_for, the initiate()
 * spool-weight math, find_material_mismatches). Behavior is ported as-is; the
 * only deltas are the PrintStartContext substitutions the design mandates
 * (detail view / AmsState / SettingsManager reads become ctx fields).
 */

#include "print_start_checks.h"

#include "filament_database.h"

#include <spdlog/spdlog.h>

namespace helix {

namespace {
/// Mirror of FilamentMappingCard::find_by_tool_index (a UI header, unusable
/// from this LVGL-free core): look up by real tool_index because tool_info may
/// be used-filtered (compacted), so vector position != tool number.
const GcodeToolInfo* find_tool_by_index(const std::vector<GcodeToolInfo>& tool_info,
                                        int tool_index) {
    for (const auto& t : tool_info) {
        if (t.tool_index == tool_index) {
            return &t;
        }
    }
    return nullptr;
}
} // namespace

std::vector<int> unresolved_tools_in(const PrintStartContext& ctx) {
    // Single-color prints need no mapping.
    if (ctx.filament_color_count <= 1) {
        return {};
    }

    // Bypass / external spool: filament reaches the nozzle without passing through
    // any slot, so every tool is "unresolved" by construction and the warning is
    // guaranteed noise. Same reasoning as PreflightValidator's bypass early-out —
    // this is the second gate on the same Print tap, and skipping only the first
    // one just moves the nag rather than removing it.
    if (ctx.any_bypass_active) {
        spdlog::debug("[PrintStartController] Bypass active - skipping unresolved-tool check");
        return {};
    }

    if (ctx.mappings.empty()) {
        // No mappings = AMS not available or card not shown
        spdlog::debug("[PrintStartController] No filament mappings available");
        return {};
    }

    auto unresolved = FilamentMapper::find_unresolved_tools(ctx.mappings);
    if (!unresolved.empty()) {
        spdlog::info("[PrintStartController] {} tools have no matching AMS slot",
                     unresolved.size());
    }
    return unresolved;
}

std::optional<std::pair<float, float>> insufficient_spool_weight_in(const PrintStartContext& ctx) {
    const auto& spool = ctx.external_spool;
    if (!spool.has_value() || !(spool->remaining_weight_g > 0.0f)) {
        return std::nullopt;
    }
    if (!ctx.metadata.has_value()) {
        return std::nullopt;
    }

    float needed_g = static_cast<float>(ctx.metadata->filament_weight_total);
    if (needed_g <= 0.0f && ctx.metadata->filament_total > 0.0) {
        // Fall back to length-based estimate using the spool's material.
        auto mat = filament::find_material(spool->material);
        if (mat.has_value() && mat->density_g_cm3 > 0.0f) {
            needed_g = filament::length_to_weight_g(
                static_cast<float>(ctx.metadata->filament_total), mat->density_g_cm3, 1.75f);
        }
    }
    if (needed_g > 0.0f && needed_g > spool->remaining_weight_g) {
        spdlog::info("[PrintStartController] Pre-print warning: needs {} g, spool has {} g",
                     needed_g, spool->remaining_weight_g);
        return std::make_pair(needed_g, spool->remaining_weight_g);
    }
    return std::nullopt;
}

std::vector<MaterialMismatchDetail> material_mismatches_in(const PrintStartContext& ctx) {
    std::vector<MaterialMismatchDetail> mismatches;

    if (!ctx.has_detail_view) {
        return mismatches;
    }

    // Per-tool filament weights from gcode metadata. Used to skip tools the
    // slicer assigned a material to but that never actually extrude (common
    // when a multi-tool profile prints a single-tool file: T0..T2 inherit the
    // profile's defaults but only T3 is used). Empty vector means the slicer
    // didn't emit per-tool data — in that case we keep the old behavior and
    // check every tool. Graceful fallback, no false-negatives possible.
    std::vector<double> filament_weights;
    if (ctx.metadata.has_value()) {
        filament_weights = ctx.metadata->filament_weights;
    }
    auto tool_is_used = [&filament_weights](int tool_index) -> bool {
        if (filament_weights.empty()) {
            return true; // No data → check everything (old behavior).
        }
        if (tool_index < 0 || tool_index >= static_cast<int>(filament_weights.size())) {
            return true; // Out-of-range → can't prove unused, be safe.
        }
        return filament_weights[tool_index] > 0.0;
    };

    if (ctx.ams_available) {
        // AMS path: check ToolMapping.material_mismatch flags
        const auto& mappings = ctx.mappings;
        const auto& tool_info = ctx.tool_info;
        const auto& slots = ctx.available_slots;

        for (const auto& m : mappings) {
            if (!m.material_mismatch) {
                continue;
            }
            if (!tool_is_used(m.tool_index)) {
                spdlog::debug("[PrintStartController] Skipping T{} mismatch — "
                              "tool has zero filament usage in gcode",
                              m.tool_index);
                continue;
            }

            MaterialMismatchDetail detail;
            detail.tool_index = m.tool_index;

            // Get expected material from gcode tool info. Look up by real
            // tool_index — tool_info may be used-filtered (compacted), so its
            // vector position no longer equals the tool number.
            if (const auto* tool = find_tool_by_index(tool_info, m.tool_index)) {
                detail.expected_material = tool->material;
            }

            // Get loaded material from the mapped AMS slot
            for (const auto& slot : slots) {
                if (slot.slot_index == m.mapped_slot && slot.backend_index == m.mapped_backend) {
                    detail.loaded_material = slot.material;
                    break;
                }
            }

            // Skip if either material is unknown (can't warn about unknowns)
            if (detail.expected_material.empty() || detail.loaded_material.empty()) {
                continue;
            }

            // Look up temperature ranges from the filament database
            auto expected_info = filament::find_material(detail.expected_material);
            if (expected_info) {
                detail.expected_nozzle_min = expected_info->nozzle_min;
                detail.expected_nozzle_max = expected_info->nozzle_max;
                detail.expected_bed_temp = expected_info->bed_temp;
            }

            auto loaded_info = filament::find_material(detail.loaded_material);
            if (loaded_info) {
                detail.loaded_nozzle_min = loaded_info->nozzle_min;
                detail.loaded_nozzle_max = loaded_info->nozzle_max;
                detail.loaded_bed_temp = loaded_info->bed_temp;
            }

            mismatches.push_back(std::move(detail));
        }
    } else {
        // Non-AMS path: compare gcode filament_type vs external spool.
        // ctx.external_spool is sourced from AmsState's layered getter
        // (SettingsManager + in-memory override) rather than raw
        // SettingsManager — deliberate unification; the in-memory override
        // only carries consumption hot-updates, so material values are
        // identical in practice.
        const auto& gcode_materials = ctx.filament_materials;
        if (gcode_materials.empty()) {
            return mismatches;
        }

        const auto& spool_info = ctx.external_spool;
        if (!spool_info || spool_info->material.empty()) {
            return mismatches;
        }

        // Check the first tool (single extruder)
        const auto& expected = gcode_materials[0];
        if (expected.empty()) {
            return mismatches;
        }

        if (!FilamentMapper::materials_match(expected, spool_info->material)) {
            MaterialMismatchDetail detail;
            detail.tool_index = 0;
            detail.expected_material = expected;
            detail.loaded_material = spool_info->material;

            // Temperature from filament database for expected material
            auto expected_info = filament::find_material(expected);
            if (expected_info) {
                detail.expected_nozzle_min = expected_info->nozzle_min;
                detail.expected_nozzle_max = expected_info->nozzle_max;
                detail.expected_bed_temp = expected_info->bed_temp;
            }

            // Temperature from external spool (user-set) or fall back to database
            if (spool_info->nozzle_temp_min > 0 && spool_info->nozzle_temp_max > 0) {
                detail.loaded_nozzle_min = spool_info->nozzle_temp_min;
                detail.loaded_nozzle_max = spool_info->nozzle_temp_max;
                detail.loaded_bed_temp = spool_info->bed_temp;
            } else {
                auto loaded_info = filament::find_material(spool_info->material);
                if (loaded_info) {
                    detail.loaded_nozzle_min = loaded_info->nozzle_min;
                    detail.loaded_nozzle_max = loaded_info->nozzle_max;
                    detail.loaded_bed_temp = loaded_info->bed_temp;
                }
            }

            mismatches.push_back(std::move(detail));
        }
    }

    if (!mismatches.empty()) {
        spdlog::info("[PrintStartController] {} material mismatch(es) detected", mismatches.size());
    }
    return mismatches;
}

const std::vector<PrintStartGate>& default_print_start_gates() {
    // Filled by Task 2; empty until the gate evaluations land.
    static const std::vector<PrintStartGate> gates = {};
    return gates;
}

} // namespace helix
