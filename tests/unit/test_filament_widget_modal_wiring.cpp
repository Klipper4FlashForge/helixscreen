// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_filament_widget_modal_wiring.cpp
 * @brief What the Filament Sensor tile's tap modal actually wires, per print state.
 *
 * Run with: ./build/bin/helix-tests "[filament][widget_tap][wiring]"
 *
 * test_filament_widget_tap_policy.cpp pins WHERE a tap goes. This file pins what
 * the dialog does once it is up, because the two are only correct together and
 * the seam between them is invisible at the UI:
 *
 *   decide_tap_destination() sends paused (print_state_enum == 2) to ModalFull.
 *   At that state runout_guidance_modal.xml hides the Close row and shows the
 *   Cancel Print / Resume Print row instead. RunoutGuidanceModal::on_cancel()
 *   and on_tertiary() null-check their callback and then hide() regardless — so
 *   a surface that shows that row without wiring it presents a live "Resume
 *   Print" button that closes the dialog and leaves the print paused. That is
 *   what shipped: three reviews read the diff and none caught it, because
 *   nothing about the rendered dialog distinguishes wired from unwired.
 *
 * Mutation check: drop the set_on_resume()/set_on_cancel_print() calls from
 * FilamentSensorWidget::show_tap_modal() and "Paused tap modal wires the buttons
 * its paused row shows" fails; drop the else-branch's Callback{} clears and
 * "Status-only reshow leaves no stale action callbacks" fails.
 */

#include "ui_update_queue.h"

#include "../lvgl_ui_test_fixture.h"
#include "../test_helpers/filament_sensor_widget_test_access.h"
#include "ams_state.h"
#include "app_globals.h"
#include "moonraker_api.h"
#include "moonraker_client_mock.h"
#include "printer_discovery.h"
#include "printer_state.h"
#include "src/ui/panel_widgets/filament_sensor_widget.h"
#include "standard_macros.h"

#include <lvgl.h>
#include <memory>
#include <string>

#include "../catch_amalgamated.hpp"

using helix::FilamentSensorWidget;
using helix::FilamentSensorWidgetTestAccess;

namespace {

class TapModalFixture : public LVGLUITestFixture {
  public:
    TapModalFixture() : mock_client(MoonrakerClientMock::PrinterType::VORON_24) {
        // READY, or MoonrakerAPI refuses every gcode with "Klipper is halted"
        // and each dispatch assertion below fails for a reason unrelated to it.
        state().set_klippy_state_sync(helix::KlippyState::READY);
        mock_client.connect("ws://mock/websocket", []() {}, []() {});
        mock_api = std::make_unique<MoonrakerAPI>(mock_client, state());

        previous_api_ = get_moonraker_api();
        set_moonraker_api(mock_api.get());

        // No AMS backend: the tile is only ever shown on printers without one,
        // and it keeps the dispatches on the macro/raw-gcode tiers where the
        // mock's gcode history can see them.
        AmsState::instance().clear_backends();

        widget.attach(test_screen(), test_screen());
    }

    ~TapModalFixture() override {
        widget.detach();
        set_moonraker_api(previous_api_);
        StandardMacros::instance().reset();
        AmsState::instance().clear_backends();
        helix::ui::UpdateQueue::instance().drain();
        mock_client.stop_temperature_simulation();
        mock_client.disconnect();
        mock_api.reset();
    }

    void set_print_state(helix::PrintJobState s) {
        lv_subject_set_int(state().get_print_state_enum_subject(), static_cast<int>(s));
        helix::ui::UpdateQueue::instance().drain();
    }

    /// Give StandardMacros a real CANCEL_PRINT to resolve, so the Cancel path
    /// reaches a dispatch rather than refusing on an empty slot.
    void configure_cancel_macro() {
        helix::PrinterDiscovery hardware;
        nlohmann::json objects = {"extruder", "gcode_macro CANCEL_PRINT"};
        hardware.parse_objects(objects);
        StandardMacros::instance().reset();
        StandardMacros::instance().init(hardware);
        REQUIRE_FALSE(StandardMacros::instance().get(StandardMacroSlot::Cancel).is_empty());
    }

    [[nodiscard]] bool gcode_sent_containing(const std::string& needle) const {
        for (const auto& script : mock_client.gcode_script_history()) {
            if (script.find(needle) != std::string::npos) {
                return true;
            }
        }
        return false;
    }

    RunoutGuidanceModal& modal() {
        return FilamentSensorWidgetTestAccess::tap_modal(widget);
    }
    void show_tap_modal(bool status_only) {
        FilamentSensorWidgetTestAccess::show_tap_modal(widget, status_only);
    }

    MoonrakerClientMock mock_client;
    std::unique_ptr<MoonrakerAPI> mock_api;
    FilamentSensorWidget widget;

  private:
    IMoonrakerAPI* previous_api_ = nullptr;
};

} // namespace

TEST_CASE_METHOD(TapModalFixture, "Paused tap modal wires the buttons its paused row shows",
                 "[filament][widget_tap][wiring]") {
    set_print_state(helix::PrintJobState::PAUSED);
    show_tap_modal(/*status_only=*/false);

    REQUIRE(modal().is_visible());
    lv_obj_t* dialog = modal().dialog();
    REQUIRE(dialog != nullptr);

    lv_obj_t* resume = lv_obj_find_by_name(dialog, "btn_resume");
    lv_obj_t* cancel = lv_obj_find_by_name(dialog, "btn_cancel_print");
    REQUIRE(resume != nullptr);
    REQUIRE(cancel != nullptr);

    // Half the claim: at print_state_enum == 2 the XML really does show this
    // row, so the buttons are reachable. Without this the wiring assertions
    // below would pass just as happily against a row nobody can press.
    CHECK_FALSE(lv_obj_has_flag(lv_obj_get_parent(resume), LV_OBJ_FLAG_HIDDEN));
    CHECK_FALSE(lv_obj_has_flag(resume, LV_OBJ_FLAG_HIDDEN));

    // The other half: reachable buttons are wired ones.
    CHECK(modal().has_resume_handler());
    CHECK(modal().has_cancel_print_handler());
}

TEST_CASE_METHOD(TapModalFixture, "One Cancel Print press dispatches the cancel macro, unconfirmed",
                 "[filament][widget_tap][wiring]") {
    // Two claims in one press. That anything is sent at all: before the fix the
    // press closed the dialog and dispatched nothing. And that ONE press is
    // enough — no confirmation step stands between the button and the printer.
    //
    // The unconfirmed half is deliberate and pinned so a divergence fails here
    // rather than surprising someone: FilamentRunoutHandler's identical Cancel
    // Print button has never confirmed either, and two dialogs that render the
    // same must not behave differently. Adding a confirmation belongs inside
    // dispatch_cancel_print(), which both surfaces call — at which point this
    // assertion changes once, on purpose, for both.
    configure_cancel_macro();
    set_print_state(helix::PrintJobState::PAUSED);
    show_tap_modal(/*status_only=*/false);

    lv_obj_t* dialog = modal().dialog();
    REQUIRE(dialog != nullptr);
    lv_obj_t* cancel = lv_obj_find_by_name(dialog, "btn_cancel_print");
    REQUIRE(cancel != nullptr);

    lv_obj_send_event(cancel, LV_EVENT_CLICKED, nullptr);
    helix::ui::UpdateQueue::instance().drain();

    CHECK(gcode_sent_containing("CANCEL_PRINT"));
}

TEST_CASE_METHOD(TapModalFixture, "Status-only reshow leaves no stale action callbacks",
                 "[filament][widget_tap][wiring]") {
    // The modal is a member and retains callbacks until they are overwritten, so
    // "the status-only branch sets nothing" only holds on the very first show.
    set_print_state(helix::PrintJobState::PAUSED);
    show_tap_modal(/*status_only=*/false);
    REQUIRE(modal().has_load_filament_handler());
    REQUIRE(modal().has_resume_handler());
    modal().hide();

    set_print_state(helix::PrintJobState::PRINTING);
    show_tap_modal(/*status_only=*/true);

    CHECK_FALSE(modal().has_load_filament_handler());
    CHECK_FALSE(modal().has_unload_filament_handler());
    CHECK_FALSE(modal().has_purge_handler());
    CHECK_FALSE(modal().has_resume_handler());
    CHECK_FALSE(modal().has_cancel_print_handler());
}

TEST_CASE_METHOD(TapModalFixture, "Detach closes an open tap modal",
                 "[filament][widget_tap][wiring]") {
    // A grid rebuild detaches the instance and invalidates every token the
    // dialog holds. Left open, the dialog's buttons would all silently no-op.
    set_print_state(helix::PrintJobState::PAUSED);
    show_tap_modal(/*status_only=*/false);
    REQUIRE(modal().is_visible());

    widget.detach();
    CHECK_FALSE(modal().is_visible());
}

TEST_CASE_METHOD(TapModalFixture, "Tile taps do not latch the advisory icon on",
                 "[filament][widget_tap][wiring]") {
    // runout_is_advisory is static and component-scoped: it outlives every show.
    // The tile sets it to 1; nothing used to set it back, so one tap swapped the
    // warning icon for the neutral one on every later runout dialog, including
    // the one that pops when a print pauses.
    //
    // Read through the class accessor rather than an XML scope lookup: this
    // fixture re-registers every component per test case, minting a fresh scope,
    // while the modal's subject registration is a once-per-process static. The
    // scope lookup would therefore answer null from the second test case on and
    // say nothing about the behaviour under test.
    lv_subject_t* advisory = RunoutGuidanceModal::advisory_subject();
    REQUIRE(advisory != nullptr);

    set_print_state(helix::PrintJobState::STANDBY);
    show_tap_modal(/*status_only=*/false);
    CHECK(lv_subject_get_int(advisory) == 1);
    modal().hide();

    // Any surface showing a REAL runout states the warning form for itself.
    RunoutGuidanceModal real_runout;
    real_runout.set_advisory(false);
    CHECK(lv_subject_get_int(advisory) == 0);
}
