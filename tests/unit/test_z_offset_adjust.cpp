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
