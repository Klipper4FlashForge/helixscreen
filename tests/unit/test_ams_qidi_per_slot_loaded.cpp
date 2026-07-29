// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_ams_qidi_per_slot_loaded.cpp
 * @brief QIDI Box per-slot load authority (#1199).
 *
 * save_variables carries two independent statements about what is loaded:
 * `slot<N>` (0 empty / 1 available / 2 loaded / 3 transitional / negative
 * blocked) is the Box's own per-slot state word, and `last_load_slot` names the
 * slot in the extruder. parse_save_variables reads them in that order, and only
 * the second one wrote the aggregate current_slot / filament_loaded pair, so:
 *
 *  - a `variables` payload that repeats `slot<M>` without `last_load_slot`
 *    demoted the seated slot to AVAILABLE while the aggregate still named it;
 *  - a Box that never writes `last_load_slot` left the aggregate empty forever,
 *    so nothing was ever reported loaded even with `slot<M>: 2` on the wire.
 *
 * The LOADED stamp is now reconciled against the aggregate after both blocks,
 * and the backend claims per-slot authority so the firmware's own `slot<N>: 2`
 * drives the active-lane highlight on its own.
 */

#include "ams_backend_qidi.h"
#include "ams_types.h"
#include "test_helpers/qidi_box_test_access.h"

#include "../catch_amalgamated.hpp"
#include "hv/json.hpp"

using json = nlohmann::json;
using namespace helix;

// ============================================================================
// The authority seam
// ============================================================================

TEST_CASE("QIDI Box claims per-slot load authority", "[ams][qidi_box][1199]") {
    AmsBackendQidi backend(nullptr, nullptr);
    CHECK(backend.has_per_slot_loaded_authority());
}

// ============================================================================
// slot<N>: 2 is firmware truth on its own
// ============================================================================

TEST_CASE("QIDI Box reports a slot loaded from its state word alone", "[ams][qidi_box][1199]") {
    // No last_load_slot has ever been seen, so the aggregate pair is empty.
    // Pre-fix that made slot_is_actively_loaded() false for every slot even
    // though the Box published slot2 = 2 (loaded).
    AmsBackendQidi backend(nullptr, nullptr);
    QidiBoxTestAccess::parse_vars(backend, json{{"slot0", 1}, {"slot1", 0}, {"slot2", 2}});

    REQUIRE(backend.get_slot_info(2).status == SlotStatus::LOADED);
    CHECK(backend.slot_is_actively_loaded(2));
    CHECK_FALSE(backend.slot_is_actively_loaded(0));
    CHECK_FALSE(backend.slot_is_actively_loaded(1));
    CHECK_FALSE(backend.slot_is_actively_loaded(3));
}

TEST_CASE("QIDI Box out-of-range slots are false, not a crash", "[ams][qidi_box][1199]") {
    AmsBackendQidi backend(nullptr, nullptr);
    QidiBoxTestAccess::parse_vars(backend, json{{"slot2", 2}, {"last_load_slot", "slot2"}});

    CHECK_FALSE(backend.slot_is_actively_loaded(-1));
    CHECK_FALSE(backend.slot_is_actively_loaded(99));
}

// ============================================================================
// The ordering hole: a slot<N> repeat must not demote the seated slot
// ============================================================================

TEST_CASE("QIDI Box keeps the seated slot LOADED across a slot-only payload",
          "[ams][qidi_box][1199]") {
    AmsBackendQidi backend(nullptr, nullptr);
    QidiBoxTestAccess::parse_vars(
        backend, json{{"slot2", 2}, {"last_load_slot", "slot2"}, {"enable_box", 1}});
    REQUIRE(backend.get_system_info().current_slot == 2);
    REQUIRE(backend.is_filament_loaded());
    REQUIRE(backend.get_slot_info(2).status == SlotStatus::LOADED);

    // The Box drops slot2's state word back to "available" without saying
    // anything about last_load_slot. The slot<N> loop runs first and wrote
    // AVAILABLE; the aggregate still names slot 2, so the two disagreed.
    QidiBoxTestAccess::parse_vars(backend, json{{"slot2", 1}});

    REQUIRE(backend.get_system_info().current_slot == 2);
    REQUIRE(backend.is_filament_loaded());
    CHECK(backend.get_slot_info(2).status == SlotStatus::LOADED);
    CHECK(backend.slot_is_actively_loaded(2));
}

TEST_CASE("QIDI Box last_load_slot=slot-1 still clears every LOADED slot",
          "[ams][qidi_box][1199]") {
    // The reconciliation must not fight the explicit unload signal.
    AmsBackendQidi backend(nullptr, nullptr);
    QidiBoxTestAccess::parse_vars(backend, json{{"slot2", 2}, {"last_load_slot", "slot2"}});
    REQUIRE(backend.slot_is_actively_loaded(2));

    QidiBoxTestAccess::parse_vars(backend, json{{"slot2", 1}, {"last_load_slot", "slot-1"}});

    REQUIRE_FALSE(backend.is_filament_loaded());
    REQUIRE(backend.get_system_info().current_slot == -1);
    CHECK(backend.get_slot_info(2).status == SlotStatus::AVAILABLE);
    CHECK_FALSE(backend.slot_is_actively_loaded(2));
}

TEST_CASE("QIDI Box a blocked seated slot stays BLOCKED", "[ams][qidi_box][1199]") {
    // A negative state word is the Box reporting a fault on that slot. Painting
    // it LOADED because the aggregate still names it would hide the error the
    // UI exists to show, so BLOCKED wins the reconciliation.
    AmsBackendQidi backend(nullptr, nullptr);
    QidiBoxTestAccess::parse_vars(backend, json{{"slot2", 2}, {"last_load_slot", "slot2"}});
    REQUIRE(backend.get_slot_info(2).status == SlotStatus::LOADED);

    QidiBoxTestAccess::parse_vars(backend, json{{"slot2", -1}});

    CHECK(backend.get_slot_info(2).status == SlotStatus::BLOCKED);
    CHECK_FALSE(backend.slot_is_actively_loaded(2));
}

// ============================================================================
// A toolchange moves the stamp, and never leaves two slots loaded
// ============================================================================

TEST_CASE("QIDI Box moves the LOADED stamp on a tool change", "[ams][qidi_box][1199]") {
    AmsBackendQidi backend(nullptr, nullptr);
    QidiBoxTestAccess::parse_vars(
        backend, json{{"slot0", 2}, {"slot1", 1}, {"slot2", 1}, {"last_load_slot", "slot0"}});
    REQUIRE(backend.slot_is_actively_loaded(0));

    QidiBoxTestAccess::parse_vars(
        backend, json{{"slot0", 1}, {"slot1", 2}, {"slot2", 1}, {"last_load_slot", "slot1"}});

    CHECK(backend.get_slot_info(1).status == SlotStatus::LOADED);
    CHECK(backend.slot_is_actively_loaded(1));
    CHECK(backend.get_slot_info(0).status == SlotStatus::AVAILABLE);
    CHECK_FALSE(backend.slot_is_actively_loaded(0));
}
