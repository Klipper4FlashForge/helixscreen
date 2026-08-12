// SPDX-License-Identifier: GPL-3.0-or-later

#include "../../include/moonraker_api.h"
#include "../../include/moonraker_client_mock.h"
#include "../../include/printer_state.h"
#include "../../include/z_offset_utils.h"
#include "../lvgl_test_fixture.h"

#include "../catch_amalgamated.hpp"

using namespace helix;

namespace {

class ZOffsetFixture : public LVGLTestFixture {
  public:
    ZOffsetFixture() : mock_client(MoonrakerClientMock::PrinterType::VORON_24) {
        state.init_subjects(false);
        state.set_klippy_state_sync(KlippyState::READY);
        mock_client.connect("ws://mock/websocket", []() {}, []() {});
        api = std::make_unique<MoonrakerAPI>(mock_client, state);
    }

    ~ZOffsetFixture() override {
        mock_client.stop_temperature_simulation();
        mock_client.disconnect();
        api.reset();
    }

    void set_homed(const char* axes) {
        lv_subject_copy_string(state.get_homed_axes_subject(), axes);
    }

    const std::string& last_sent() const {
        return mock_client.last_send_script();
    }

    MoonrakerClientMock mock_client;
    PrinterState state;
    std::unique_ptr<MoonrakerAPI> api;
};

} // namespace

TEST_CASE_METHOD(ZOffsetFixture, "adjust clamps at the safe limit", "[z_offset][adjust][mock]") {
    // Already at +1.99mm, asking for another +0.05 must stop at +2.0.
    auto r = helix::zoffset::adjust(api.get(), &state, 1.99, 0.05);

    REQUIRE(r.new_offset_mm == Catch::Approx(2.0));
    REQUIRE(r.applied_delta_mm == Catch::Approx(0.01));
}

TEST_CASE_METHOD(ZOffsetFixture, "adjust refuses a no-op move at the limit",
                 "[z_offset][adjust][mock]") {
    auto r = helix::zoffset::adjust(api.get(), &state, 2.0, 0.05);

    REQUIRE(r.sent == false);
    REQUIRE(r.clamped_to_noop == true);
    REQUIRE(last_sent().find("Z_ADJUST") == std::string::npos);
}

TEST_CASE_METHOD(ZOffsetFixture,
                 "a clamped-to-noop result is distinguishable from a null-api "
                 "result",
                 "[z_offset][adjust][mock]") {
    // Both cases share sent == false; clamped_to_noop is what tells them apart
    // (see AdjustResult in z_offset_utils.h). A caller that used float equality
    // on applied_delta_mm to infer "nothing happened" could not tell these
    // apart without it.
    auto clamped = helix::zoffset::adjust(api.get(), &state, 2.0, 0.05);
    REQUIRE(clamped.sent == false);
    REQUIRE(clamped.clamped_to_noop == true);

    auto null_api = helix::zoffset::adjust(nullptr, &state, 0.0, 0.05);
    REQUIRE(null_api.sent == false);
    REQUIRE(null_api.clamped_to_noop == false);
}

TEST_CASE_METHOD(ZOffsetFixture, "adjust rounds to the nearest micron",
                 "[z_offset][adjust][mock]") {
    // Repeated float addition would drift; the result must land on a micron.
    auto r = helix::zoffset::adjust(api.get(), &state, 0.0, 0.0123456);

    REQUIRE(r.new_offset_mm == Catch::Approx(0.012));
}

TEST_CASE_METHOD(ZOffsetFixture, "adjust omits MOVE=1 when axes are not homed",
                 "[z_offset][adjust][mock]") {
    set_homed("xy"); // Z missing — MOVE=1 would make Klipper error

    helix::zoffset::adjust(api.get(), &state, 0.0, 0.05);

    REQUIRE(last_sent().find("MOVE=1") == std::string::npos);
    REQUIRE(last_sent().find("SET_GCODE_OFFSET Z_ADJUST=0.050") != std::string::npos);
}

TEST_CASE_METHOD(ZOffsetFixture, "adjust appends MOVE=1 when all axes are homed",
                 "[z_offset][adjust][mock]") {
    set_homed("xyz");

    helix::zoffset::adjust(api.get(), &state, 0.0, 0.05);

    REQUIRE(last_sent() == "SET_GCODE_OFFSET Z_ADJUST=0.050 MOVE=1");
}

TEST_CASE_METHOD(ZOffsetFixture, "adjust accumulates the pending delta",
                 "[z_offset][adjust][mock]") {
    set_homed("xyz");

    helix::zoffset::adjust(api.get(), &state, 0.0, 0.05);
    helix::zoffset::adjust(api.get(), &state, 0.05, -0.01);

    // +50um then -10um = +40um still unsaved.
    REQUIRE(lv_subject_get_int(state.get_pending_z_offset_delta_subject()) == 40);
}

TEST_CASE_METHOD(ZOffsetFixture, "a firmware-managed save also clears the pending delta",
                 "[z_offset][adjust][mock]") {
    set_homed("xyz");
    helix::zoffset::adjust(api.get(), &state, 0.0, 0.05);
    REQUIRE(lv_subject_get_int(state.get_pending_z_offset_delta_subject()) == 50);

    bool saved = false;
    helix::zoffset::apply_and_save(
        api.get(), ZOffsetCalibrationStrategy::FIRMWARE_MANAGED, [&]() { saved = true; },
        [](const std::string&) {}, &state);

    REQUIRE(saved);
    REQUIRE(lv_subject_get_int(state.get_pending_z_offset_delta_subject()) == 0);
}

TEST_CASE_METHOD(ZOffsetFixture,
                 "a probe-calibrate save chains APPLY -> SAVE_CONFIG and clears "
                 "the pending delta",
                 "[z_offset][adjust][mock]") {
    set_homed("xyz");
    helix::zoffset::adjust(api.get(), &state, 0.0, 0.05);
    REQUIRE(lv_subject_get_int(state.get_pending_z_offset_delta_subject()) == 50);

    bool saved = false;
    std::string error;
    helix::zoffset::apply_and_save(
        api.get(), ZOffsetCalibrationStrategy::PROBE_CALIBRATE, [&]() { saved = true; },
        [&](const std::string& msg) { error = msg; }, &state);

    REQUIRE(error.empty());
    REQUIRE(saved);
    REQUIRE(lv_subject_get_int(state.get_pending_z_offset_delta_subject()) == 0);
    REQUIRE(last_sent() == "SAVE_CONFIG");
}

TEST_CASE_METHOD(LVGLTestFixture, "the z step index round-trips through Config",
                 "[z_offset][step]") {
    helix::zoffset::set_persisted_step_index(3);
    REQUIRE(helix::zoffset::persisted_step_index() == 3);

    // Out-of-range writes are rejected outright, not clamped-and-stored: the
    // previously persisted value (3) must survive untouched. Clamping on write
    // would let a future caller bug silently destroy the user's real setting;
    // the read path already defends against a corrupt on-disk value on its own.
    helix::zoffset::set_persisted_step_index(99);
    REQUIRE(helix::zoffset::persisted_step_index() == 3);

    helix::zoffset::set_persisted_step_index(-1);
    REQUIRE(helix::zoffset::persisted_step_index() == 3);
}
