// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_filament_panel_op_slot.cpp
 * @brief Integration guard: FilamentPanel::execute_load / execute_unload act on
 *        the DROPDOWN-SELECTED slot, resolved once through selected_op_slot().
 *
 * Run with: ./build/bin/helix-tests "[filament][op_slot][panel]"
 *
 * Unlike test_filament_op_slot_resolver.cpp (which pins the pure resolver and a
 * hand-written mirror of the panel's branching), this test drives the ACTUAL
 * production methods on a real FilamentPanel view built from filament_panel.xml,
 * with a real ToolState topology + a recording AMS backend injected into the real
 * AmsState singleton. It asserts WHICH slot argument reaches the backend.
 *
 * The bug this guards (single-source-of-truth fix): execute_load/execute_unload
 * used to read backend->get_system_info().current_slot and act on THAT, so on a
 * BoxTurtle where lane 4 (slot 3) was loaded to the toolhead but the dropdown
 * defaulted to T0, Load acted on the already-loaded lane 3 instead of the
 * selected lane 0. Both executors now call the private selected_op_slot() — the
 * same resolution the button gating uses — so the op can never diverge.
 *
 * Mutation check: reverting execute_load() to load_filament(sys.current_slot)
 * makes "BoxTurtle: Load follows the selected tool" FAIL (load_filament gets 3,
 * not 0). If it still passes, the test does not reach the callsite.
 */

#include "../lvgl_ui_test_fixture.h"
#include "../test_helpers/filament_panel_test_access.h"

#include "ams_backend_mock.h"
#include "ams_state.h"
#include "ams_types.h"
#include "tool_state.h"
#include "ui_panel_filament.h"

#include <lvgl.h>
#include <memory>
#include <vector>

#include "../catch_amalgamated.hpp"

using helix::ToolState;
using helix::ToolTopology;
using TA = helix::ui::FilamentPanelTestAccess;
// AmsState / AmsBackendMock / AmsSystemInfo / AmsType live in the global namespace.

namespace {

// Recording backend: overrides only what the panel's op path reads, so the test
// controls the system snapshot and observes the slot the executors dispatch.
// Subclasses the production mock (test infrastructure per CLAUDE L065) so all the
// unrelated pure-virtuals are already satisfied.
class RecordingBackend : public AmsBackendMock {
  public:
    RecordingBackend() : AmsBackendMock(4) {}

    AmsSystemInfo sys_{}; ///< Snapshot returned to the panel (test sets fields)
    int loaded_slot_ = -1; ///< Which slot reports "loaded at toolhead"

    // Observed dispatches
    int last_load_slot = -999;
    int load_calls = 0;
    int last_unload_slot = -999;
    int unload_calls = 0;

    [[nodiscard]] AmsSystemInfo get_system_info() const override {
        return sys_;
    }
    [[nodiscard]] AmsType get_type() const override {
        return sys_.type;
    }
    // Force the AMS load branch in execute_load() (never the raw-macro fallback).
    [[nodiscard]] bool requires_slot_selection_for_load() const override {
        return true;
    }
    [[nodiscard]] bool slot_is_actively_loaded(int slot) const override {
        return slot == loaded_slot_;
    }
    [[nodiscard]] bool slot_has_filament_at_toolhead(int slot) const override {
        return slot == loaded_slot_;
    }

    AmsError load_filament(int slot) override {
        last_load_slot = slot;
        ++load_calls;
        return AmsErrorHelper::success();
    }
    AmsError unload_filament(int slot) override {
        last_unload_slot = slot;
        ++unload_calls;
        return AmsErrorHelper::success();
    }
};

// Builds a real FilamentPanel view over an injected RecordingBackend + topology,
// and tears the whole thing down in the right order (UI subtree, then panel, then
// the shared singletons) so the next test in the shard starts clean.
struct OpSlotHarness {
    LVGLUITestFixture& fx;
    RecordingBackend* mock = nullptr;
    std::unique_ptr<FilamentPanel> panel;
    lv_obj_t* root = nullptr;

    OpSlotHarness(LVGLUITestFixture& f, const AmsSystemInfo& sys, int loaded_slot,
                  const ToolTopology& topo)
        : fx(f) {
        // The panel wires observers on ToolState + AmsState in its ctor, so their
        // subjects must exist first.
        ToolState::instance().init_subjects(true);
        AmsState::instance().init_subjects(true);

        // Inject the recording backend + topology BEFORE constructing the panel.
        auto owned = std::make_unique<RecordingBackend>();
        owned->sys_ = sys;
        owned->loaded_slot_ = loaded_slot;
        mock = owned.get();
        AmsState::instance().set_backend(std::move(owned));
        AmsState::instance().sync_from_backend(); // publishes ams_type != NONE
        ToolState::instance().set_ams_topology(topo);

        panel = std::make_unique<FilamentPanel>(fx.state(), fx.api());
        panel->init_subjects();

        root = static_cast<lv_obj_t*>(lv_xml_create(fx.test_screen(), "filament_panel", nullptr));
        REQUIRE(root != nullptr);
        panel->setup(root, fx.test_screen());

        // Flush deferred observer callbacks (active-tool sync, gating recompute).
        fx.process_lvgl(30);

        // Ensure the dropdown carries every tool so a T3 selection is reachable
        // (setup() already calls this, but the topology publish may have raced).
        TA::populate_extruder_dropdown(*panel);
    }

    void select_tool(int idx) {
        lv_obj_t* dd = TA::extruder_dropdown(*panel);
        REQUIRE(dd != nullptr);
        REQUIRE(lv_dropdown_get_option_count(dd) >= static_cast<uint32_t>(idx + 1));
        lv_dropdown_set_selected(dd, static_cast<uint32_t>(idx));
    }

    ~OpSlotHarness() {
        if (root) {
            lv_obj_delete(root); // delete UI subtree while panel subjects live
        }
        fx.process_lvgl(10);
        panel.reset(); // dtor deinits subjects + removes observers (subjects valid)
        AmsState::instance().set_backend(nullptr);
        ToolState::instance().clear_ams_topology();
        AmsState::instance().deinit_subjects();
        ToolState::instance().deinit_subjects();
    }
};

// Identity BoxTurtle/AFC snapshot: 4 lanes, tool i -> slot i, lane 4 (slot 3)
// loaded to the toolhead, aggregate current_slot == 3.
AmsSystemInfo boxturtle_sys() {
    AmsSystemInfo sys;
    sys.type = AmsType::AFC;
    sys.total_slots = 4;
    sys.current_slot = 3;
    sys.filament_loaded = true;
    sys.tool_to_slot_map = {0, 1, 2, 3};
    return sys;
}

ToolTopology identity_topo() {
    ToolTopology topo;
    topo.tool_count = 4;
    topo.active_tool = 0;
    topo.tool_to_slot = {0, 1, 2, 3};
    return topo;
}

} // namespace

TEST_CASE_METHOD(LVGLUITestFixture,
                 "BoxTurtle: execute_load targets the SELECTED tool, not current_slot",
                 "[filament][op_slot][panel]") {
    // Dropdown = T0 while lane 4 (slot 3) is loaded. execute_load() must dispatch
    // load_filament(0) — the selected lane — never load_filament(3). This is the
    // exact single-source-of-truth bug the fix closes, and the mutation target.
    OpSlotHarness h(*this, boxturtle_sys(), /*loaded_slot=*/3, identity_topo());
    h.select_tool(0);

    // selected_op_slot() (the real production resolver) resolves T0 -> slot 0.
    REQUIRE(TA::selected_op_slot(*h.panel) == 0);

    TA::execute_load(*h.panel);

    REQUIRE(h.mock->load_calls == 1);
    CHECK(h.mock->last_load_slot == 0); // NOT 3 (the loaded current_slot)
}

TEST_CASE_METHOD(LVGLUITestFixture,
                 "BoxTurtle: execute_unload targets the selected loaded slot",
                 "[filament][op_slot][panel]") {
    // Dropdown = T3 and slot 3 is loaded -> unload_filament(3).
    OpSlotHarness h(*this, boxturtle_sys(), /*loaded_slot=*/3, identity_topo());
    h.select_tool(3);

    REQUIRE(TA::selected_op_slot(*h.panel) == 3);

    TA::execute_unload(*h.panel);

    REQUIRE(h.mock->unload_calls == 1);
    CHECK(h.mock->last_unload_slot == 3);
}

TEST_CASE_METHOD(LVGLUITestFixture,
                 "BoxTurtle: execute_unload on an unloaded selected slot makes NO backend call",
                 "[filament][op_slot][panel]") {
    // Dropdown = T0, slot 0 is NOT loaded (only slot 3 is). The "nothing loaded"
    // guard must refuse the unload — the backend is never asked to unload.
    OpSlotHarness h(*this, boxturtle_sys(), /*loaded_slot=*/3, identity_topo());
    h.select_tool(0);

    REQUIRE(TA::selected_op_slot(*h.panel) == 0);

    TA::execute_unload(*h.panel);

    CHECK(h.mock->unload_calls == 0);
}

TEST_CASE_METHOD(LVGLUITestFixture,
                 "Snapmaker U1 shape: selected tool survives a stuck current_slot",
                 "[filament][op_slot][panel]") {
    // Identity tool->slot map, current_slot stuck at -1 (the original U1 root
    // cause), dropdown = T3. selected_op_slot() must yield 3 via the identity map,
    // and execute_load() must dispatch load_filament(3) — never a bare-default 0.
    AmsSystemInfo u1;
    u1.type = AmsType::SNAPMAKER;
    u1.total_slots = 4;
    u1.current_slot = -1; // stuck
    u1.filament_loaded = false;
    u1.tool_to_slot_map = {0, 1, 2, 3};

    OpSlotHarness h(*this, u1, /*loaded_slot=*/-1, identity_topo());
    h.select_tool(3);

    REQUIRE(TA::selected_op_slot(*h.panel) == 3);

    TA::execute_load(*h.panel);

    REQUIRE(h.mock->load_calls == 1);
    CHECK(h.mock->last_load_slot == 3); // NOT 0 (bare default / stuck current_slot)
}
