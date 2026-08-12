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
    REQUIRE(last_sent().find("Z_ADJUST") == std::string::npos);
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

// NOTE: no PROBE_CALIBRATE/ENDSTOP case here. apply_and_save chains
// Z_OFFSET_APPLY_PROBE -> SAVE_CONFIG through the mock client, and the mock's
// SAVE_CONFIG handler (moonraker_client_mock.cpp) returns 1 from gcode_script(),
// which register_print_handlers' printer.gcode.script handler treats as an RPC
// failure ("An unknown error occurred.") rather than an immediate success ack —
// unlike BED_MESH_CALIBRATE/PID_CALIBRATE, which return 0. That is a pre-existing
// mock bug unrelated to this change; driving on_success synchronously through
// that path is not possible without fixing it, which is out of scope here. The
// firmware-managed case below is guaranteed synchronous and pins the same
// wrapper independently of mock RPC behavior.
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

TEST_CASE_METHOD(LVGLTestFixture, "the z step index round-trips through Config",
                 "[z_offset][step]") {
    helix::zoffset::set_persisted_step_index(3);
    REQUIRE(helix::zoffset::persisted_step_index() == 3);

    // Out-of-range values must fall back to the default rather than index a
    // kZStepAmountsMm entry that does not exist.
    helix::zoffset::set_persisted_step_index(99);
    REQUIRE(helix::zoffset::persisted_step_index() == 2);

    helix::zoffset::set_persisted_step_index(-1);
    REQUIRE(helix::zoffset::persisted_step_index() == 2);
}
