// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_ams_endless_spool.cpp
 * @brief The shared endless-spool abstraction: types, projection, base-class
 *        validation/reset, per-backend capability states and eligibility.
 *
 * The pure model (helix::printer::EndlessSpool*) is tested without any backend;
 * the backend sections then pin the ONE capability state each backend reports and
 * the ONE place its guards now live (AmsBackend::set_endless_spool_backup).
 */

#include "ams_backend_afc.h"
#include "ams_backend_happy_hare.h"
#include "ams_backend_mock.h"
#include "ams_types.h"
#include "filament_database.h"

#include <algorithm>

#include "../catch_amalgamated.hpp"

using namespace helix;
using namespace helix::printer;

// =============================================================================
// Capability type - the three axes must be independently expressible
// =============================================================================

TEST_CASE("EndlessSpoolCapabilities separates availability, enablement and editability",
          "[ams][endless_spool][types]") {
    SECTION("default construction is 'no such feature'") {
        EndlessSpoolCapabilities caps;

        CHECK(caps.availability == EndlessSpoolAvailability::Unsupported);
        CHECK(caps.enabled == EndlessSpoolEnabled::Unknown);
        CHECK(caps.editability == EndlessSpoolEditability::ReadOnly);
        CHECK(caps.restriction == EndlessSpoolRestriction::None);
        CHECK(caps.provider.empty());
        CHECK_FALSE(caps.available());
        CHECK_FALSE(caps.editable());
    }

    SECTION("available-but-off is distinguishable from available-and-on") {
        // The state CFS could not express: the old struct hardcoded
        // supported=true and buried on/off in a free-text description.
        EndlessSpoolCapabilities off{.availability = EndlessSpoolAvailability::Available,
                                     .enabled = EndlessSpoolEnabled::Off,
                                     .editability = EndlessSpoolEditability::ReadOnly,
                                     .restriction = EndlessSpoolRestriction::FirmwareManaged};
        EndlessSpoolCapabilities on = off;
        on.enabled = EndlessSpoolEnabled::On;

        CHECK(off.available());
        CHECK(on.available());
        CHECK(off.enabled != on.enabled);
    }

    SECTION("unknown enablement is not off") {
        EndlessSpoolCapabilities caps{.availability = EndlessSpoolAvailability::Available,
                                      .enabled = EndlessSpoolEnabled::Unknown};
        CHECK(caps.enabled != EndlessSpoolEnabled::Off);
        CHECK(static_cast<int>(EndlessSpoolEnabled::Unknown) < 0);
    }

    SECTION("requires-plugin is available()==false but not Unsupported") {
        EndlessSpoolCapabilities caps{.availability = EndlessSpoolAvailability::RequiresPlugin,
                                      .enabled = EndlessSpoolEnabled::Off,
                                      .editability = EndlessSpoolEditability::ReadOnly,
                                      .restriction = EndlessSpoolRestriction::PluginMissing};
        CHECK_FALSE(caps.available());
        CHECK(caps.availability != EndlessSpoolAvailability::Unsupported);
    }

    SECTION("editable() requires both availability and a writable shape") {
        EndlessSpoolCapabilities per_slot{.availability = EndlessSpoolAvailability::Available,
                                          .enabled = EndlessSpoolEnabled::On,
                                          .editability = EndlessSpoolEditability::PerSlot};
        CHECK(per_slot.editable());

        EndlessSpoolCapabilities group = per_slot;
        group.editability = EndlessSpoolEditability::Group;
        CHECK(group.editable());

        // Editable shape but the feature is not there: still not editable.
        EndlessSpoolCapabilities absent = per_slot;
        absent.availability = EndlessSpoolAvailability::RequiresPlugin;
        CHECK_FALSE(absent.editable());
    }
}

TEST_CASE("Every restriction reason yields display text", "[ams][endless_spool][types][i18n]") {
    // No raw English may live in the capability struct; the reason is an enum and
    // this function is the single translation point. A missing case here would
    // ship a blank explanation to the user.
    for (auto restriction :
         {EndlessSpoolRestriction::MultiUnit, EndlessSpoolRestriction::FirmwareManaged,
          EndlessSpoolRestriction::NotReady, EndlessSpoolRestriction::PluginMissing,
          EndlessSpoolRestriction::PluginReadOnly}) {
        CHECK_FALSE(endless_spool_restriction_text(restriction).empty());
    }
    CHECK(endless_spool_restriction_text(EndlessSpoolRestriction::None).empty());
}

// =============================================================================
// Group model + the single group-to-edge projection
// =============================================================================

TEST_CASE("Endless spool config models groups, not single successors",
          "[ams][endless_spool][types][projection]") {
    SECTION("directed edges become ordered two-member groups") {
        // AFC's shape: lane 0 -> lane 2, lane 1 has none, lane 3 -> lane 0.
        auto cfg = endless_spool_config_from_edges({2, -1, -1, 0});

        REQUIRE(cfg.groups.size() == 2);
        CHECK(cfg.groups[0].ordered);
        CHECK(cfg.groups[0].members == std::vector<int>{0, 2});
        CHECK(cfg.groups[1].members == std::vector<int>{3, 0});
    }

    SECTION("a self-edge is not a group") {
        auto cfg = endless_spool_config_from_edges({0, -1});
        CHECK(cfg.empty());
    }

    SECTION("two lanes pointing at the same backup stay two groups") {
        // AFC permits 0->2 and 1->2. An 'ordered chain' model would lose one.
        auto cfg = endless_spool_config_from_edges({2, 2, -1});
        REQUIRE(cfg.groups.size() == 2);
        auto edges = endless_spool_backup_edges(cfg, 3);
        CHECK(edges == std::vector<int>{2, 2, -1});
    }

    SECTION("group ids become one unordered group each") {
        // Happy Hare's shape: gates 0,1 in group 0; gates 2,3 in group 1.
        auto cfg = endless_spool_config_from_groups({0, 0, 1, 1});

        REQUIRE(cfg.groups.size() == 2);
        CHECK_FALSE(cfg.groups[0].ordered);
        CHECK(cfg.groups[0].id == 0);
        CHECK(cfg.groups[0].members == std::vector<int>{0, 1});
        CHECK(cfg.groups[1].id == 1);
        CHECK(cfg.groups[1].members == std::vector<int>{2, 3});
    }

    SECTION("a group of one is dropped - it backs nothing up") {
        // This is the shape Happy Hare hands us for an ungrouped MMU: every gate
        // gets its own standalone id.
        auto cfg = endless_spool_config_from_groups({0, 1, 2, 3});
        CHECK(cfg.empty());
        CHECK(endless_spool_backup_edges(cfg, 4) == std::vector<int>{-1, -1, -1, -1});
    }

    SECTION("ungrouped gates (-1) are excluded") {
        auto cfg = endless_spool_config_from_groups({-1, -1, 5, 5});
        REQUIRE(cfg.groups.size() == 1);
        CHECK(cfg.groups[0].members == std::vector<int>{2, 3});
        CHECK(endless_spool_backup_edges(cfg, 4) == std::vector<int>{-1, -1, 3, 2});
    }

    SECTION("a 3+ member group survives as ONE group") {
        // The whole point of decision (a): a 4-gate Happy Hare group must not be
        // stored as four arbitrary arrows. It is one group; the arrows are a
        // projection of it.
        auto cfg = endless_spool_config_from_groups({7, 7, 7, 7});

        REQUIRE(cfg.groups.size() == 1);
        CHECK(cfg.groups[0].members == std::vector<int>{0, 1, 2, 3});

        // Projection: every member falls back to the first OTHER member, which is
        // exactly what the old in-backend "use first match" loop produced.
        CHECK(endless_spool_backup_edges(cfg, 4) == std::vector<int>{1, 0, 0, 0});
        CHECK(endless_spool_backup_for(cfg, 0) == 1);
        CHECK(endless_spool_backup_for(cfg, 3) == 0);
    }

    SECTION("a 3-member ordered group projects as a chain, tail terminates") {
        EndlessSpoolConfig cfg;
        cfg.groups.push_back({.id = -1, .members = {0, 1, 2}, .ordered = true});

        CHECK(endless_spool_backup_edges(cfg, 3) == std::vector<int>{1, 2, -1});
        CHECK(endless_spool_backup_for(cfg, 0) == 1);
        CHECK(endless_spool_backup_for(cfg, 1) == 2);
        CHECK(endless_spool_backup_for(cfg, 2) == -1);
    }

    SECTION("backup_for and backup_edges never disagree") {
        // Both entry points, one rule. Includes an overlapping-ordered-group case
        // (slot 1 is a tail in one group and a head in another).
        EndlessSpoolConfig cfg;
        cfg.groups.push_back({.id = -1, .members = {0, 1}, .ordered = true});
        cfg.groups.push_back({.id = -1, .members = {1, 2}, .ordered = true});
        cfg.groups.push_back({.id = 9, .members = {3, 4, 5}, .ordered = false});

        const auto edges = endless_spool_backup_edges(cfg, 6);
        for (int slot = 0; slot < 6; ++slot) {
            CHECK(edges[static_cast<size_t>(slot)] == endless_spool_backup_for(cfg, slot));
        }
        CHECK(edges == std::vector<int>{1, 2, -1, 4, 3, 3});
    }

    SECTION("out-of-range members are ignored, not written out of bounds") {
        EndlessSpoolConfig cfg;
        cfg.groups.push_back({.id = -1, .members = {9, 10}, .ordered = true});
        CHECK(endless_spool_backup_edges(cfg, 2) == std::vector<int>{-1, -1});
        CHECK(endless_spool_backup_for(cfg, -1) == -1);
        CHECK(endless_spool_backup_edges(cfg, 0).empty());
    }
}

// =============================================================================
// Base class: one set of guards, one reset loop, one eligibility default
// =============================================================================

TEST_CASE("Base class owns endless spool validation", "[ams][endless_spool][interface]") {
    AmsBackendMock backend(4);
    backend.set_operation_delay(0);
    REQUIRE(backend.start());

    SECTION("mock defaults to available, on, per-slot editable") {
        auto caps = backend.get_endless_spool_capabilities();

        CHECK(caps.availability == EndlessSpoolAvailability::Available);
        CHECK(caps.enabled == EndlessSpoolEnabled::On);
        CHECK(caps.editability == EndlessSpoolEditability::PerSlot);
        CHECK(caps.editable());
    }

    SECTION("a successful write yields a relation") {
        auto result = backend.set_endless_spool_backup(0, 2);
        REQUIRE(result);
        CHECK(result.technical_msg.empty());

        auto cfg = backend.get_endless_spool_config();
        CHECK(endless_spool_backup_for(cfg, 0) == 2);
        CHECK(endless_spool_backup_for(cfg, 1) == -1);
    }

    SECTION("rejection 1 of 3: source slot out of range") {
        for (int bad : {99, -1, -2}) {
            auto result = backend.set_endless_spool_backup(bad, 2);
            CHECK_FALSE(result);
            CHECK(result.result == AmsResult::INVALID_SLOT);
        }
    }

    SECTION("rejection 2 of 3: backup slot out of range (but -1 is legal)") {
        auto too_high = backend.set_endless_spool_backup(0, 99);
        CHECK_FALSE(too_high);
        CHECK(too_high.result == AmsResult::INVALID_SLOT);

        auto negative = backend.set_endless_spool_backup(0, -2);
        CHECK_FALSE(negative);
        CHECK(negative.result == AmsResult::INVALID_SLOT);

        CHECK(backend.set_endless_spool_backup(0, -1));
    }

    SECTION("rejection 3 of 3: a slot cannot back itself up") {
        auto result = backend.set_endless_spool_backup(2, 2);
        CHECK_FALSE(result);
        CHECK(result.result == AmsResult::INVALID_SLOT);
        // One wording, from AmsBackend - three backends used to phrase this
        // three different ways.
        CHECK(result.technical_msg.find("its own endless spool backup") != std::string::npos);
    }

    SECTION("the hook is never reached for a rejected write") {
        REQUIRE(backend.set_endless_spool_backup(1, 3));
        REQUIRE_FALSE(backend.set_endless_spool_backup(1, 1));
        // Still the accepted value: a rejected write must not mutate anything.
        CHECK(endless_spool_backup_for(backend.get_endless_spool_config(), 1) == 3);
    }

    SECTION("read-only backends are refused with the restriction reason") {
        backend.set_endless_spool_editable(false);
        auto caps = backend.get_endless_spool_capabilities();
        REQUIRE(caps.available());
        REQUIRE_FALSE(caps.editable());

        auto result = backend.set_endless_spool_backup(0, 2);
        CHECK_FALSE(result);
        CHECK(result.result == AmsResult::NOT_SUPPORTED);
        CHECK(result.user_msg == endless_spool_restriction_text(caps.restriction));
    }

    SECTION("unavailable backends are refused") {
        backend.set_endless_spool_supported(false);
        auto caps = backend.get_endless_spool_capabilities();
        CHECK(caps.availability == EndlessSpoolAvailability::Unsupported);
        CHECK_FALSE(caps.editable());

        auto result = backend.set_endless_spool_backup(0, 2);
        CHECK_FALSE(result);
        CHECK(result.result == AmsResult::NOT_SUPPORTED);
    }

    backend.stop();
}

TEST_CASE("Base reset loop clears every slot", "[ams][endless_spool][interface]") {
    AmsBackendMock backend(4);
    backend.set_operation_delay(0);
    REQUIRE(backend.start());

    SECTION("clears all backups") {
        REQUIRE(backend.set_endless_spool_backup(0, 1));
        REQUIRE(backend.set_endless_spool_backup(2, 3));
        REQUIRE_FALSE(backend.get_endless_spool_config().empty());

        // The mock never implemented reset and used to return NOT_SUPPORTED
        // while advertising editable=true. It inherits a working one now.
        auto result = backend.reset_endless_spool();
        CHECK(result);
        CHECK(backend.get_endless_spool_config().empty());
    }

    SECTION("read-only backends refuse reset instead of half-clearing") {
        REQUIRE(backend.set_endless_spool_backup(0, 1));
        backend.set_endless_spool_editable(false);

        auto result = backend.reset_endless_spool();
        CHECK_FALSE(result);
        CHECK(result.result == AmsResult::NOT_SUPPORTED);
        // Nothing was cleared.
        CHECK(endless_spool_backup_for(backend.get_endless_spool_config(), 0) == 1);
    }

    SECTION("unavailable backends refuse reset") {
        backend.set_endless_spool_supported(false);
        auto result = backend.reset_endless_spool();
        CHECK_FALSE(result);
        CHECK(result.result == AmsResult::NOT_SUPPORTED);
    }

    backend.stop();
}

TEST_CASE("Default backup eligibility is material compatibility",
          "[ams][endless_spool][eligibility]") {
    AmsBackendMock backend(4);
    backend.set_operation_delay(0);
    REQUIRE(backend.start());

    // The mock's default slots carry materials; find a same-material pair and a
    // cross-material pair from what the backend actually reports rather than
    // hardcoding the fixture's data.
    const std::string m0 = backend.get_slot_info(0).material;
    REQUIRE_FALSE(m0.empty());

    SECTION("a slot is never its own backup, whatever the material") {
        CHECK_FALSE(backend.is_endless_spool_backup_eligible(0, 0));
    }

    SECTION("out-of-range indices are not eligible") {
        CHECK_FALSE(backend.is_endless_spool_backup_eligible(-1, 0));
        CHECK_FALSE(backend.is_endless_spool_backup_eligible(0, -1));
    }

    SECTION("identical materials are eligible") {
        // Same slot's material against a sibling we force to match by asking the
        // compatibility helper directly, so this asserts the wiring, not the
        // fixture's colour choices.
        for (int other = 1; other < 4; ++other) {
            const std::string mo = backend.get_slot_info(other).material;
            if (mo.empty()) {
                continue;
            }
            CHECK(backend.is_endless_spool_backup_eligible(0, other) ==
                  filament::are_materials_compatible(m0, mo));
        }
    }

    backend.stop();
}

// =============================================================================
// AFC - PerSlot, always enabled, SET_RUNOUT transport
// =============================================================================

class AmsBackendAfcEndlessSpoolHelper : public AmsBackendAfc {
  public:
    AmsBackendAfcEndlessSpoolHelper() : AmsBackendAfc(nullptr, nullptr) {}

    void initialize_test_lanes(int count) {
        system_info_.units.clear();
        system_info_.total_slots = count;
        std::vector<std::string> names;

        AmsUnit unit;
        unit.unit_index = 0;
        unit.name = "AFC Test Unit";
        unit.slot_count = count;
        unit.first_slot_global_index = 0;

        for (int i = 0; i < count; ++i) {
            std::string name = "lane" + std::to_string(i + 1);
            names.push_back(name);

            SlotInfo slot;
            slot.slot_index = i;
            slot.global_index = i;
            slot.status = SlotStatus::AVAILABLE;
            slot.mapped_tool = i;
            unit.slots.push_back(slot);
        }

        system_info_.units.push_back(unit);
        slots_.initialize("AFC Test Unit", names);
    }

    // G-code capture for verification
    std::vector<std::string> captured_gcodes;
    AmsError gcode_result = AmsErrorHelper::success();

    AmsError execute_gcode(const std::string& gcode) override {
        captured_gcodes.push_back(gcode);
        return gcode_result;
    }

    bool has_gcode_containing(const std::string& substring) const {
        return std::any_of(
            captured_gcodes.begin(), captured_gcodes.end(),
            [&](const std::string& gc) { return gc.find(substring) != std::string::npos; });
    }

    void clear_gcodes() {
        captured_gcodes.clear();
    }
};

TEST_CASE("AFC endless spool - per-slot, always on", "[ams][endless_spool][afc]") {
    AmsBackendAfcEndlessSpoolHelper helper;
    helper.initialize_test_lanes(4);

    SECTION("capability state") {
        auto caps = helper.get_endless_spool_capabilities();

        CHECK(caps.availability == EndlessSpoolAvailability::Available);
        // AFC has no on/off switch - a lane names a runout lane or it does not.
        CHECK(caps.enabled == EndlessSpoolEnabled::On);
        CHECK(caps.editability == EndlessSpoolEditability::PerSlot);
        CHECK(caps.restriction == EndlessSpoolRestriction::None);
        CHECK(caps.editable());
    }

    SECTION("no lanes configured means an empty relation") {
        CHECK(helper.get_endless_spool_config().empty());
    }

    SECTION("set sends SET_RUNOUT and records the edge") {
        auto result = helper.set_endless_spool_backup(0, 2);

        REQUIRE(result);
        CHECK(helper.has_gcode_containing("SET_RUNOUT"));
        CHECK(helper.has_gcode_containing("LANE=lane1"));
        CHECK(helper.has_gcode_containing("RUNOUT=lane3"));
        CHECK(endless_spool_backup_for(helper.get_endless_spool_config(), 0) == 2);
    }

    SECTION("clearing sends RUNOUT=NONE") {
        REQUIRE(helper.set_endless_spool_backup(0, 2));
        helper.clear_gcodes();

        auto result = helper.set_endless_spool_backup(0, -1);
        REQUIRE(result);
        CHECK(helper.has_gcode_containing("SET_RUNOUT LANE=lane1 RUNOUT=NONE"));
        CHECK(endless_spool_backup_for(helper.get_endless_spool_config(), 0) == -1);
    }

    SECTION("a rejected G-code leaves the registry alone") {
        // The registry used to be written BEFORE the send, so a printer that
        // refused the command left the UI drawing an arrow the hardware never had.
        helper.gcode_result = AmsErrorHelper::command_failed("SET_RUNOUT", "unknown command");

        auto result = helper.set_endless_spool_backup(1, 3);
        CHECK_FALSE(result);
        CHECK(helper.get_endless_spool_config().empty());
        CHECK(endless_spool_backup_for(helper.get_endless_spool_config(), 1) == -1);
    }

    SECTION("reset comes from the base and clears every lane") {
        REQUIRE(helper.set_endless_spool_backup(0, 1));
        REQUIRE(helper.set_endless_spool_backup(2, 3));
        helper.clear_gcodes();

        auto result = helper.reset_endless_spool();
        CHECK(result);
        CHECK(helper.get_endless_spool_config().empty());
        // One SET_RUNOUT ... RUNOUT=NONE per lane.
        CHECK(helper.captured_gcodes.size() == 4);
        CHECK(std::all_of(
            helper.captured_gcodes.begin(), helper.captured_gcodes.end(),
            [](const std::string& gc) { return gc.find("RUNOUT=NONE") != std::string::npos; }));
    }
}

// =============================================================================
// Happy Hare - Group editing, gated on the ENABLE bit
// =============================================================================

class AmsBackendHappyHareEndlessSpoolHelper : public AmsBackendHappyHare {
  public:
    AmsBackendHappyHareEndlessSpoolHelper() : AmsBackendHappyHare(nullptr, nullptr) {}

    void initialize_test_gates(int count) {
        system_info_.units.clear();
        system_info_.total_slots = count;
        // Happy Hare's endless spool must be ON for GROUPS= to be honoured; the
        // real backend reads this from mmu.endless_spool_enabled.
        system_info_.endless_spool_enabled = true;

        AmsUnit unit;
        unit.unit_index = 0;
        unit.name = "Happy Hare MMU";
        unit.slot_count = count;
        unit.first_slot_global_index = 0;

        for (int i = 0; i < count; ++i) {
            SlotInfo slot;
            slot.slot_index = i;
            slot.global_index = i;
            slot.status = SlotStatus::AVAILABLE;
            slot.mapped_tool = i;
            slot.endless_spool_group = -1; // No group by default
            unit.slots.push_back(slot);
        }

        system_info_.units.push_back(unit);

        // Initialize SlotRegistry to match
        std::vector<std::string> slot_names;
        for (int i = 0; i < count; ++i) {
            slot_names.push_back(std::to_string(i));
        }
        slots_.initialize("MMU", slot_names);
        for (int i = 0; i < count; ++i) {
            auto* entry = slots_.get_mut(i);
            if (entry) {
                entry->info.status = SlotStatus::AVAILABLE;
                entry->info.endless_spool_group = -1;
            }
        }
    }

    void set_endless_spool_groups(const std::vector<int>& groups) {
        // Simulate data from printer.mmu.endless_spool_groups via registry
        for (size_t i = 0; i < groups.size(); ++i) {
            auto* entry = slots_.get_mut(static_cast<int>(i));
            if (entry) {
                entry->info.endless_spool_group = groups[i];
            }
        }
    }

    void set_enabled(bool enabled) {
        system_info_.endless_spool_enabled = enabled;
    }

    void add_second_unit() {
        AmsUnit unit;
        unit.unit_index = 1;
        unit.name = "MMU Unit 2";
        unit.slot_count = 4;
        unit.first_slot_global_index = 4;
        system_info_.units.push_back(unit);
    }

    // Capture G-code instead of dispatching to a (null) Moonraker API.
    std::vector<std::string> captured_gcodes;
    AmsError execute_gcode(const std::string& gcode) override {
        captured_gcodes.push_back(gcode);
        return AmsErrorHelper::success();
    }
    bool has_gcode(const std::string& expected) const {
        return std::find(captured_gcodes.begin(), captured_gcodes.end(), expected) !=
               captured_gcodes.end();
    }
};

TEST_CASE("Happy Hare endless spool - group editing", "[ams][endless_spool][happy_hare]") {
    AmsBackendHappyHareEndlessSpoolHelper helper;
    helper.initialize_test_gates(4);

    SECTION("capability state on a single unit with the feature on") {
        auto caps = helper.get_endless_spool_capabilities();

        CHECK(caps.availability == EndlessSpoolAvailability::Available);
        CHECK(caps.enabled == EndlessSpoolEnabled::On);
        // A write rewrites the whole gate->group array, so this is Group, not
        // PerSlot: editing one gate can move another gate's relation.
        CHECK(caps.editability == EndlessSpoolEditability::Group);
        CHECK(caps.restriction == EndlessSpoolRestriction::None);
    }

    SECTION("the ENABLE bit is reported, not silently forced") {
        helper.set_enabled(false);
        auto caps = helper.get_endless_spool_capabilities();

        CHECK(caps.available());
        CHECK(caps.enabled == EndlessSpoolEnabled::Off);
        // Still Group-editable as a shape; the transport is what refuses.
        CHECK(caps.editability == EndlessSpoolEditability::Group);
    }

    SECTION("multi-unit is read-only with the MultiUnit reason") {
        helper.add_second_unit();
        auto caps = helper.get_endless_spool_capabilities();

        CHECK(caps.available());
        CHECK(caps.editability == EndlessSpoolEditability::ReadOnly);
        CHECK(caps.restriction == EndlessSpoolRestriction::MultiUnit);

        auto result = helper.set_endless_spool_backup(0, 2);
        CHECK_FALSE(result);
        CHECK(result.result == AmsResult::NOT_SUPPORTED);
        CHECK(helper.captured_gcodes.empty());
    }

    SECTION("groups arrive as groups, and project to the same arrows as before") {
        helper.set_endless_spool_groups({0, 0, 1, 1});
        auto cfg = helper.get_endless_spool_config();

        REQUIRE(cfg.groups.size() == 2);
        CHECK(cfg.groups[0].members == std::vector<int>{0, 1});
        CHECK(cfg.groups[1].members == std::vector<int>{2, 3});
        CHECK(endless_spool_backup_edges(cfg, 4) == std::vector<int>{1, 0, 3, 2});
    }

    SECTION("ungrouped gates have no backup") {
        helper.set_endless_spool_groups({-1, -1, 0, 0});
        auto cfg = helper.get_endless_spool_config();
        CHECK(endless_spool_backup_edges(cfg, 4) == std::vector<int>{-1, -1, 3, 2});
    }

    SECTION("a gate alone in its group has no backup") {
        helper.set_endless_spool_groups({0, 1, 2, 3});
        CHECK(helper.get_endless_spool_config().empty());
    }

    SECTION("one 4-gate group is ONE group, not four arrows in the model") {
        helper.set_endless_spool_groups({2, 2, 2, 2});
        auto cfg = helper.get_endless_spool_config();

        REQUIRE(cfg.groups.size() == 1);
        CHECK(cfg.groups[0].id == 2);
        CHECK(cfg.groups[0].members.size() == 4);
        CHECK_FALSE(cfg.groups[0].ordered);
    }

    SECTION("set sends GROUPS without ENABLE=") {
        // Gates start ungrouped -> standalone ids 0,1,2,3; joining gate 0 to gate
        // 2's group yields 2,1,2,3.
        auto result = helper.set_endless_spool_backup(0, 2);

        CHECK(result.success());
        CHECK(helper.has_gcode("MMU_ENDLESS_SPOOL QUIET=1 GROUPS=2,1,2,3"));
        // ENABLE=1 on an edit persisted mmu_state_enable_endless_spool, turning
        // the feature on as a side effect of setting one backup gate.
        CHECK_FALSE(helper.has_gcode("MMU_ENDLESS_SPOOL ENABLE=1 QUIET=1 GROUPS=2,1,2,3"));
        for (const auto& gc : helper.captured_gcodes) {
            CHECK(gc.find("ENABLE=") == std::string::npos);
        }
    }

    SECTION("editing while endless spool is off is refused, not silently enabled") {
        helper.set_enabled(false);

        auto result = helper.set_endless_spool_backup(0, 2);
        CHECK_FALSE(result);
        CHECK(result.result == AmsResult::WRONG_STATE);
        CHECK(helper.captured_gcodes.empty());
    }

    SECTION("reset keeps the firmware primitive, including its required ENABLE=1") {
        // Required, and NOT a lasting side effect: cmd_MMU_ENDLESS_SPOOL
        // early-returns before honouring RESET while disabled, and
        // _reset_endless_spool() then persists default_endless_spool_enabled over
        // the momentary enable.
        auto result = helper.reset_endless_spool();
        CHECK(result);
        CHECK(helper.has_gcode("MMU_ENDLESS_SPOOL ENABLE=1 RESET=1 QUIET=1"));
        CHECK(helper.captured_gcodes.size() == 1);
    }

    SECTION("uninitialised registry is NotReady, not editable") {
        AmsBackendHappyHareEndlessSpoolHelper fresh;
        auto caps = fresh.get_endless_spool_capabilities();

        CHECK(caps.available());
        CHECK(caps.editability == EndlessSpoolEditability::ReadOnly);
        CHECK(caps.restriction == EndlessSpoolRestriction::NotReady);
        CHECK(caps.enabled == EndlessSpoolEnabled::Unknown);
        CHECK(fresh.get_endless_spool_config().empty());
    }
}

// =============================================================================
// Chains, cycles, and the retired flag
// =============================================================================

TEST_CASE("Endless spool edge cases", "[ams][endless_spool][edge]") {
    AmsBackendMock backend(4);
    backend.set_operation_delay(0);
    REQUIRE(backend.start());

    SECTION("circular backup is allowed (A->B, B->A)") {
        CHECK(backend.set_endless_spool_backup(0, 1));
        CHECK(backend.set_endless_spool_backup(1, 0));

        auto cfg = backend.get_endless_spool_config();
        CHECK(endless_spool_backup_for(cfg, 0) == 1);
        CHECK(endless_spool_backup_for(cfg, 1) == 0);
    }

    SECTION("chain backup is allowed (A->B->C)") {
        backend.set_endless_spool_backup(0, 1);
        backend.set_endless_spool_backup(1, 2);

        auto cfg = backend.get_endless_spool_config();
        CHECK(endless_spool_backup_for(cfg, 0) == 1);
        CHECK(endless_spool_backup_for(cfg, 1) == 2);
        CHECK(endless_spool_backup_for(cfg, 2) == -1);
    }

    backend.stop();
}

TEST_CASE("Availability has exactly one source of truth",
          "[ams][endless_spool][integration][1250]") {
    AmsBackendMock backend(4);
    backend.set_operation_delay(0);
    REQUIRE(backend.start());

    SECTION("AmsSystemInfo carries the ENABLE bit only, and caps derive from it") {
        // AmsSystemInfo::supports_endless_spool used to answer the availability
        // question a second time and could disagree with the capabilities. There
        // is no such field any more; what remains is the enable bit, and
        // get_endless_spool_capabilities() reads it rather than answering itself.
        auto info = backend.get_system_info();
        auto caps = backend.get_endless_spool_capabilities();

        CHECK((caps.enabled == EndlessSpoolEnabled::On) == info.endless_spool_enabled);
    }

    SECTION("turning support off cannot leave a stale 'available' anywhere") {
        backend.set_endless_spool_supported(false);

        auto info = backend.get_system_info();
        auto caps = backend.get_endless_spool_capabilities();

        CHECK_FALSE(caps.available());
        CHECK_FALSE(info.endless_spool_enabled);
        CHECK((caps.enabled == EndlessSpoolEnabled::On) == info.endless_spool_enabled);
    }

    backend.stop();
}
