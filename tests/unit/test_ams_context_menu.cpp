// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_ams_context_menu.h"

#include "ams_types.h"

#include "../catch_amalgamated.hpp"

using namespace helix;
using namespace helix::ui;

// Forwards the private static predicates (friend access).
class AmsContextMenuTestAccess {
  public:
    static bool should_show_clear_spool(const SlotInfo& slot) {
        return AmsContextMenu::should_show_clear_spool(slot);
    }

    using UnloadMode = AmsContextMenu::UnloadMode;

    static UnloadMode decide_unload_mode(bool toolhead_unload, bool can_recover,
                                         bool recovery_attributed, bool supports_eject,
                                         bool slot_has_filament, bool supports_force_eject,
                                         bool slot_empty) {
        return AmsContextMenu::decide_unload_mode(toolhead_unload, can_recover,
                                                  recovery_attributed, supports_eject,
                                                  slot_has_filament, supports_force_eject,
                                                  slot_empty);
    }

    static bool decide_can_load(bool system_busy, bool toolhead_unload, bool slot_has_filament,
                                bool print_active) {
        return AmsContextMenu::decide_can_load(system_busy, toolhead_unload, slot_has_filament,
                                               print_active);
    }

    static bool decide_unload_enabled(bool system_busy, UnloadMode mode, bool print_active) {
        return AmsContextMenu::decide_unload_enabled(system_busy, mode, print_active);
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

// AmsContextMenu::decide_unload_mode() — Unload button operation selection.
//
// The BoxTurtle hub sensor is shared across every lane on a unit, so
// can_recover_lane_position() can read true for EVERY lane at once when AFC
// names no active lane. A prior version of this chain checked RecoverPosition
// unconditionally before Eject, which meant one unattributed stranded lane
// hid Eject from every seated lane sharing its hub — the bug this ruling
// fixes. lane_recovery_is_attributed() breaks the tie: attributed recovery
// outranks Eject, unattributed recovery defers to it.
using UnloadMode = AmsContextMenuTestAccess::UnloadMode;

TEST_CASE("AmsContextMenu::decide_unload_mode toolhead-loaded wins over everything",
          "[ams][context_menu]") {
    // Even if the backend also claims recovery is possible and attributed, a
    // slot that unloads via the heated toolhead path must take Unload.
    auto mode = AmsContextMenuTestAccess::decide_unload_mode(
        /*toolhead_unload=*/true, /*can_recover=*/true, /*recovery_attributed=*/true,
        /*supports_eject=*/true, /*slot_has_filament=*/true, /*supports_force_eject=*/true,
        /*slot_empty=*/false);
    CHECK(mode == UnloadMode::Unload);
}

TEST_CASE("AmsContextMenu::decide_unload_mode attributed strand outranks Eject",
          "[ams][context_menu]") {
    // AFC named this exact lane as active (lane_recovery_is_attributed==true).
    // Even though the lane also has filament present (would otherwise take
    // Eject), the confident diagnosis wins.
    auto mode = AmsContextMenuTestAccess::decide_unload_mode(
        /*toolhead_unload=*/false, /*can_recover=*/true, /*recovery_attributed=*/true,
        /*supports_eject=*/true, /*slot_has_filament=*/true, /*supports_force_eject=*/false,
        /*slot_empty=*/false);
    CHECK(mode == UnloadMode::RecoverPosition);
}

TEST_CASE(
    "AmsContextMenu::decide_unload_mode unattributed strand does not take Eject from a seated lane",
    "[ams][context_menu]") {
    // This is the regression the ruling fixes: an unattributed hub-wide trigger
    // (can_recover=true, recovery_attributed=false) must NOT preempt Eject on a
    // lane that is simply seated (slot_has_filament=true).
    auto mode = AmsContextMenuTestAccess::decide_unload_mode(
        /*toolhead_unload=*/false, /*can_recover=*/true, /*recovery_attributed=*/false,
        /*supports_eject=*/true, /*slot_has_filament=*/true, /*supports_force_eject=*/false,
        /*slot_empty=*/false);
    CHECK(mode == UnloadMode::Eject);
}

TEST_CASE(
    "AmsContextMenu::decide_unload_mode unattributed lane with nothing ejectable still gets Recover",
    "[ams][context_menu]") {
    // No filament present to eject (slot_has_filament=false), so Eject is not an
    // option regardless of attribution — the unattributed Recover arm is the
    // last resort that still offers a way out for a lane with no other option.
    auto mode = AmsContextMenuTestAccess::decide_unload_mode(
        /*toolhead_unload=*/false, /*can_recover=*/true, /*recovery_attributed=*/false,
        /*supports_eject=*/true, /*slot_has_filament=*/false, /*supports_force_eject=*/false,
        /*slot_empty=*/true);
    CHECK(mode == UnloadMode::RecoverPosition);
}

TEST_CASE("AmsContextMenu::decide_unload_mode falls through to ForceEject and Unavailable",
          "[ams][context_menu]") {
    // No toolhead unload, no recovery possible at all, no eject support: an
    // empty lane with force-eject support gets ForceEject...
    auto force_eject = AmsContextMenuTestAccess::decide_unload_mode(
        /*toolhead_unload=*/false, /*can_recover=*/false, /*recovery_attributed=*/false,
        /*supports_eject=*/false, /*slot_has_filament=*/false, /*supports_force_eject=*/true,
        /*slot_empty=*/true);
    CHECK(force_eject == UnloadMode::ForceEject);

    // ...and with nothing at all supported, there is genuinely nothing to do.
    auto unavailable = AmsContextMenuTestAccess::decide_unload_mode(
        /*toolhead_unload=*/false, /*can_recover=*/false, /*recovery_attributed=*/false,
        /*supports_eject=*/false, /*slot_has_filament=*/false, /*supports_force_eject=*/false,
        /*slot_empty=*/true);
    CHECK(unavailable == UnloadMode::Unavailable);
}

// AmsSubscriptionBackend::refuse_if_printing() rejects load/unload while a print
// is PRINTING *or* PAUSED, because the macros home the toolhead. The menu offered
// Load anyway, so a runout-paused AD5X user tapped it — following Klipper's own
// "load it and press RESUME" instruction — and got "Cannot run filament operation
// while printing" (bundle JX2FVRB9). The affordance has to agree with the guard.
//
// Mutation check: drop the `!print_active` term from decide_can_load() and
// "Load is refused while a print owns the toolhead" fails.
TEST_CASE("AmsContextMenu::decide_can_load agrees with the backend print guard",
          "[ams][context_menu][print_guard]") {
    SECTION("Load is offered for a filled, non-seated lane when no print is running") {
        CHECK(AmsContextMenuTestAccess::decide_can_load(
            /*system_busy=*/false, /*toolhead_unload=*/false, /*slot_has_filament=*/true,
            /*print_active=*/false));
    }

    SECTION("Load is refused while a print owns the toolhead") {
        CHECK_FALSE(AmsContextMenuTestAccess::decide_can_load(
            /*system_busy=*/false, /*toolhead_unload=*/false, /*slot_has_filament=*/true,
            /*print_active=*/true));
    }

    SECTION("The pre-existing terms still hold") {
        CHECK_FALSE(AmsContextMenuTestAccess::decide_can_load(true, false, true, false)); // busy
        CHECK_FALSE(AmsContextMenuTestAccess::decide_can_load(false, true, true, false)); // seated
        CHECK_FALSE(AmsContextMenuTestAccess::decide_can_load(false, false, false, false)); // empty
    }
}

// Only the heated toolhead unload is blocked mid-print. The cold lane ops leave
// the toolhead parked where the print left it and the backend permits them via
// check_preconditions(false), so blocking the whole button would strand filament
// a paused user could legitimately eject.
TEST_CASE("AmsContextMenu::decide_unload_enabled blocks only the toolhead unload mid-print",
          "[ams][context_menu][print_guard]") {
    SECTION("Toolhead unload is refused while a print owns the toolhead") {
        CHECK(AmsContextMenuTestAccess::decide_unload_enabled(false, UnloadMode::Unload, false));
        CHECK_FALSE(
            AmsContextMenuTestAccess::decide_unload_enabled(false, UnloadMode::Unload, true));
    }

    SECTION("Cold lane ops stay available mid-print") {
        CHECK(AmsContextMenuTestAccess::decide_unload_enabled(false, UnloadMode::Eject, true));
        CHECK(AmsContextMenuTestAccess::decide_unload_enabled(false, UnloadMode::RecoverPosition,
                                                              true));
        CHECK(AmsContextMenuTestAccess::decide_unload_enabled(false, UnloadMode::ForceEject, true));
    }

    SECTION("Busy and Unavailable still win over everything") {
        CHECK_FALSE(
            AmsContextMenuTestAccess::decide_unload_enabled(true, UnloadMode::Eject, false));
        CHECK_FALSE(
            AmsContextMenuTestAccess::decide_unload_enabled(false, UnloadMode::Unavailable, false));
    }
}
