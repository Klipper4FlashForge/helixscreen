// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_ams_context_menu.h"

#include "ams_types.h"

#include "../catch_amalgamated.hpp"

using namespace helix;
using namespace helix::ui;

// Forwards the private static predicate (friend access).
class AmsContextMenuTestAccess {
  public:
    static bool should_show_clear_spool(const SlotInfo& slot) {
        return AmsContextMenu::should_show_clear_spool(slot);
    }
};

// "Clear Spool" was revealed only when `!slot_has_filament`, so it vanished the
// moment a new spool went into the lane — precisely when a stale assignment is
// most harmful, because that is when the wrong metadata gets printed with and
// when an edit will aim a Spoolman write at the previous spool.
//
// Stale metadata on an EMPTY lane is cosmetic. Stale metadata on a LOADED lane
// is the actual failure. Presence must not gate the affordance.
TEST_CASE("AmsContextMenu::should_show_clear_spool ignores whether filament is present",
          "[ams][context_menu]") {
    SECTION("assigned AND loaded still offers the clear — the regression") {
        SlotInfo slot;
        slot.status = SlotStatus::LOADED;
        slot.spoolman_id = 86;
        slot.material = "ASA";
        CHECK(AmsContextMenuTestAccess::should_show_clear_spool(slot));
    }

    SECTION("assigned and empty offers the clear") {
        SlotInfo slot;
        slot.status = SlotStatus::EMPTY;
        slot.spoolman_id = 86;
        slot.material = "ASA";
        CHECK(AmsContextMenuTestAccess::should_show_clear_spool(slot));
    }

    SECTION("material-only assignment counts, with no Spoolman link") {
        SlotInfo slot;
        slot.status = SlotStatus::LOADED;
        slot.spoolman_id = 0;
        slot.material = "PLA";
        CHECK(AmsContextMenuTestAccess::should_show_clear_spool(slot));
    }

    SECTION("spoolman-link-only assignment counts, with no material") {
        SlotInfo slot;
        slot.status = SlotStatus::AVAILABLE;
        slot.spoolman_id = 86;
        slot.material.clear();
        CHECK(AmsContextMenuTestAccess::should_show_clear_spool(slot));
    }

    SECTION("nothing assigned offers nothing to clear") {
        SlotInfo slot;
        slot.status = SlotStatus::AVAILABLE;
        slot.spoolman_id = 0;
        slot.material.clear();
        CHECK_FALSE(AmsContextMenuTestAccess::should_show_clear_spool(slot));
    }

    SECTION("empty and unassigned offers nothing to clear") {
        SlotInfo slot;
        slot.status = SlotStatus::EMPTY;
        slot.spoolman_id = 0;
        slot.material.clear();
        CHECK_FALSE(AmsContextMenuTestAccess::should_show_clear_spool(slot));
    }
}
