// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file ams_endless_spool.cpp
 * @brief Backend-agnostic endless-spool model: restriction text, group
 *        construction, and the one group-to-edge projection.
 *
 * Nothing here touches a backend, a mutex or LVGL widgets, so it is directly
 * unit-testable. The projection in particular must exist exactly once - four
 * backends previously each invented their own, and Happy Hare's
 * `// Use first match` loop is what made a 4-gate group render as four
 * arbitrary arrows.
 */

#include "ams_types.h"
#include "lvgl/src/others/translation/lv_translation.h"

#include <algorithm>
#include <map>

namespace helix::printer {

std::string endless_spool_restriction_text(EndlessSpoolRestriction restriction) {
    switch (restriction) {
    case EndlessSpoolRestriction::None:
        return {};
    case EndlessSpoolRestriction::MultiUnit:
        return lv_tr("Cannot be changed from here on a multi-unit MMU");
    case EndlessSpoolRestriction::FirmwareManaged:
        return lv_tr("The printer's firmware chooses the backup spool itself");
    case EndlessSpoolRestriction::NotReady:
        return lv_tr("Waiting for the filament system to report its state");
    case EndlessSpoolRestriction::PluginMissing:
        return lv_tr("No automatic backup-spool package is installed");
    case EndlessSpoolRestriction::PluginReadOnly:
        return lv_tr("Configured in the backup-spool package, not from here");
    }
    return {};
}

EndlessSpoolConfig endless_spool_config_from_edges(const std::vector<int>& edges) {
    EndlessSpoolConfig cfg;
    for (int slot = 0; slot < static_cast<int>(edges.size()); ++slot) {
        const int backup = edges[static_cast<size_t>(slot)];
        if (backup < 0 || backup == slot) {
            continue;
        }
        EndlessSpoolGroup group;
        group.members = {slot, backup};
        group.ordered = true;
        cfg.groups.push_back(std::move(group));
    }
    return cfg;
}

EndlessSpoolConfig endless_spool_config_from_groups(const std::vector<int>& group_ids) {
    // std::map, not unordered_map: the group order in the result is observable
    // (it drives the projection's iteration and any future group rendering), and
    // a stable ascending-by-id order is the one a reader can predict.
    std::map<int, std::vector<int>> by_id;
    for (int slot = 0; slot < static_cast<int>(group_ids.size()); ++slot) {
        const int id = group_ids[static_cast<size_t>(slot)];
        if (id < 0) {
            continue;
        }
        by_id[id].push_back(slot);
    }

    EndlessSpoolConfig cfg;
    for (auto& [id, members] : by_id) {
        // A group of one backs nothing up. Emitting it would make "is grouped"
        // and "has a backup" disagree, and Happy Hare hands us exactly this
        // shape: every ungrouped gate gets its own standalone id.
        if (members.size() < 2) {
            continue;
        }
        EndlessSpoolGroup group;
        group.id = id;
        group.members = std::move(members);
        group.ordered = false;
        cfg.groups.push_back(std::move(group));
    }
    return cfg;
}

std::vector<int> endless_spool_backup_edges(const EndlessSpoolConfig& cfg, int slot_count) {
    std::vector<int> edges(slot_count < 0 ? 0 : static_cast<size_t>(slot_count), -1);
    const auto in_range = [&edges](int slot) {
        return slot >= 0 && slot < static_cast<int>(edges.size());
    };

    // First group to give a slot a successor wins, so this agrees with
    // endless_spool_backup_for()'s single-slot walk. Neither builder can produce
    // a slot with two successors, but a hand-built config can, and the two
    // entry points must not disagree about it.
    const auto assign = [&](int from, int to) {
        if (in_range(from) && to >= 0 && from != to && edges[static_cast<size_t>(from)] < 0) {
            edges[static_cast<size_t>(from)] = to;
        }
    };

    for (const auto& group : cfg.groups) {
        if (group.ordered) {
            for (size_t i = 0; i + 1 < group.members.size(); ++i) {
                assign(group.members[i], group.members[i + 1]);
            }
            continue;
        }
        // Undirected: each member falls back to the first other member. This is
        // the projection Happy Hare's backend used to perform inline, kept
        // behaviour-identical so the arrows do not move under this refactor.
        for (const int member : group.members) {
            for (const int other : group.members) {
                if (other != member) {
                    assign(member, other);
                    break;
                }
            }
        }
    }
    return edges;
}

int endless_spool_backup_for(const EndlessSpoolConfig& cfg, int slot) {
    if (slot < 0) {
        return -1;
    }
    // Same rules as endless_spool_backup_edges(), evaluated for one slot so the
    // caller does not have to know the system's slot count. The first group that
    // yields a successor wins - the same rule endless_spool_backup_edges() uses.
    for (const auto& group : cfg.groups) {
        const auto it = std::find(group.members.begin(), group.members.end(), slot);
        if (it == group.members.end()) {
            continue;
        }
        if (group.ordered) {
            const auto next = std::next(it);
            if (next != group.members.end()) {
                return *next;
            }
            continue; // Tail of an ordered chain has no successor.
        }
        for (const int other : group.members) {
            if (other != slot) {
                return other;
            }
        }
    }
    return -1;
}

} // namespace helix::printer
