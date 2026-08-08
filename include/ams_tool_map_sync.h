// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

/**
 * @file ams_tool_map_sync.h
 * @brief One writer for the two directions AmsSystemInfo states its tool map in.
 *
 * AmsSystemInfo carries the tool<->slot mapping twice:
 *
 *   - forward: AmsSystemInfo::tool_to_slot_map[tool] = global slot
 *              read by helix::ui::resolve_op_button_slot (which lane the
 *              filament panel's Load/Unload/Purge buttons act on), by the
 *              print-start remap snapshot, and by AmsState::build_ams_topology
 *   - reverse: SlotInfo::mapped_tool per slot
 *              read by the AMS panel's per-lane tool badge
 *
 * A backend that writes one direction by hand and leaves the other to a default
 * drifts, and the two surfaces then disagree about which physical lane a tool
 * number names. That has shipped twice in opposite directions: QIDI Box wrote
 * only mapped_tool and badged the remapped lane while the buttons operated on
 * the original one; CFS wrote only tool_to_slot_map and did the inverse.
 *
 * SlotRegistry::set_tool_mapping() is the canonical bookkeeping for "a tool maps
 * to exactly one slot" — it maintains both directions in lockstep and evicts
 * whichever side loses a contested tool number. Backends that keep their slot
 * state on an AmsSystemInfo rather than in a registry (CFS, ToolChanger, QIDI)
 * can still use it: run the mapping through a throwaway registry and read both
 * directions back out. These helpers are that pass, so each backend states only
 * WHICH direction its firmware is authoritative in.
 */

#include "ams_types.h"
#include "slot_registry.h"

#include <string>
#include <vector>

namespace helix::printer {

namespace tool_map_detail {

/// Seed a scratch registry with @p slot_count anonymous lanes.
///
/// Names are positional placeholders only — nothing here looks a lane up by
/// name; the registry is used purely for its mapping bookkeeping.
inline void seed_ledger(SlotRegistry& ledger, int slot_count) {
    std::vector<std::string> names;
    names.reserve(static_cast<size_t>(slot_count < 0 ? 0 : slot_count));
    for (int i = 0; i < slot_count; ++i) {
        names.push_back("slot" + std::to_string(i));
    }
    ledger.initialize("tool map", names);
}

/// Write the registry's normalised view back onto @p info, both directions.
///
/// If two slots claimed the same tool number the registry already dropped the
/// loser, and the badge must not keep showing a tool that no longer routes
/// through that lane — so mapped_tool is overwritten unconditionally, not
/// merged.
inline void publish_ledger(AmsSystemInfo& info, const SlotRegistry& ledger, int slot_count) {
    for (int i = 0; i < slot_count; ++i) {
        if (auto* slot = info.get_slot_global(i)) {
            slot->mapped_tool = ledger.tool_for_slot(i);
        }
    }
    info.tool_to_slot_map = ledger.tool_map();
}

} // namespace tool_map_detail

/**
 * @brief Re-derive the forward map from the per-slot mapped_tool values.
 *
 * For firmware that states the mapping slot-first (QIDI's save_variables
 * `value_t<N>="slot<M>"`, a UI edit that sets SlotInfo::mapped_tool).
 * Slots with mapped_tool < 0 stay unmapped; no identity is invented.
 */
inline void sync_tool_map_from_slots(AmsSystemInfo& info) {
    const int slot_count = info.total_slots > 0 ? info.total_slots : 0;

    SlotRegistry ledger;
    tool_map_detail::seed_ledger(ledger, slot_count);

    for (int i = 0; i < slot_count; ++i) {
        const auto* slot = info.get_slot_global(i);
        if (slot && slot->mapped_tool >= 0) {
            ledger.set_tool_mapping(i, slot->mapped_tool);
        }
    }

    tool_map_detail::publish_ledger(info, ledger, slot_count);
}

/**
 * @brief Re-derive the per-slot mapped_tool values from the forward map.
 *
 * For firmware that states the mapping tool-first (CFS's `box.map`, an
 * ASSIGN_TOOL / BOX_MODIFY_TN write, a reset to identity).
 *
 * @param identity_fallback When true, any tool index in [0, total_slots) that
 *        the forward map leaves unmapped falls back to tool N -> slot N,
 *        provided slot N was not already claimed by an explicitly mapped tool.
 *        That is the default CFS and ToolChanger both start from — lanes are
 *        1:1 with tools until firmware says otherwise — and it must survive a
 *        payload that carries no map at all, or a partial one. Backends with no
 *        such default (a lane is unmapped until firmware names it) pass false.
 *
 * The "not already claimed" guard is what keeps the fallback from contradicting
 * the firmware: with only `T0 -> slot 2` stated, slot 2 belongs to T0, so T2 is
 * left with no lane rather than stealing the badge back.
 */
inline void sync_tool_map_from_forward(AmsSystemInfo& info, bool identity_fallback) {
    const int slot_count = info.total_slots > 0 ? info.total_slots : 0;

    SlotRegistry ledger;
    tool_map_detail::seed_ledger(ledger, slot_count);

    // set_tool_map() applies the whole forward vector at once, clearing every
    // prior mapped_tool first, so the scratch registry ends up stating exactly
    // what the caller's forward map says and nothing else.
    ledger.set_tool_map(info.tool_to_slot_map);

    if (identity_fallback) {
        for (int t = 0; t < slot_count; ++t) {
            if (ledger.slot_for_tool(t) >= 0) {
                continue; // firmware placed this tool explicitly
            }
            if (ledger.tool_for_slot(t) >= 0) {
                continue; // lane t is spoken for by a remapped tool
            }
            ledger.set_tool_mapping(t, t);
        }
    }

    tool_map_detail::publish_ledger(info, ledger, slot_count);
}

/**
 * @brief Apply a single "tool @p tool_number now sources lane @p global_slot"
 *        assignment on top of the map @p info already publishes.
 *
 * The optimistic local update a backend makes when it dispatches its own remap
 * gcode (BOX_MODIFY_TN, ASSIGN_TOOL) so the UI reflects the new mapping before
 * firmware echoes it back.
 *
 * Hand-editing tool_to_slot_map[tool] instead does NOT do this: it leaves the
 * lane's previous tool still pointing at the lane, so two tools claim one slot
 * and the reverse direction has to pick one — which is how a remap could badge
 * the lane with the tool it was moved AWAY from. SlotRegistry::set_tool_mapping
 * evicts both losing sides, and that is the behaviour reproduced here.
 *
 * Argument order matches the AmsBackend::set_tool_mapping(tool, slot) call
 * sites, not SlotRegistry's (slot, tool).
 */
inline void assign_tool_slot(AmsSystemInfo& info, int tool_number, int global_slot) {
    const int slot_count = info.total_slots > 0 ? info.total_slots : 0;
    if (tool_number < 0 || global_slot < 0 || global_slot >= slot_count) {
        return;
    }

    SlotRegistry ledger;
    tool_map_detail::seed_ledger(ledger, slot_count);
    ledger.set_tool_map(info.tool_to_slot_map);
    ledger.set_tool_mapping(global_slot, tool_number);

    tool_map_detail::publish_ledger(info, ledger, slot_count);
}

} // namespace helix::printer
