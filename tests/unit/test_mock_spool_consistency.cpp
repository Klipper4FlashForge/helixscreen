// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Guards against drift between the AMS mock backends' slot filaments and the
// mock Spoolman inventory (MoonrakerSpoolmanAPIMock::init_mock_spools). A slot
// that claims spoolman_id=N must describe the same filament as spool N, or the
// slot editor looks broken in mock mode (spec §9).

#include "ams_backend_mock.h"
#include "color_utils.h"
#include "moonraker_api_mock.h"
#include "moonraker_client_mock.h"
#include "printer_state.h"

#include <map>

#include "../catch_amalgamated.hpp"

namespace {

std::map<int, SpoolInfo> fetch_mock_spools() {
    helix::PrinterState state;
    MoonrakerClientMock client;
    MoonrakerAPIMock api(client, state);
    std::map<int, SpoolInfo> by_id;
    api.spoolman().get_spoolman_spools(
        [&](const std::vector<SpoolInfo>& spools) {
            for (const auto& s : spools) {
                by_id[s.id] = s;
            }
        },
        [](const MoonrakerError&) { FAIL("mock spool fetch must not error"); });
    return by_id;
}

void check_backend_against_spoolman(AmsBackendMock& mock, int slot_count) {
    auto spools = fetch_mock_spools();
    REQUIRE(!spools.empty());

    for (int i = 0; i < slot_count; ++i) {
        SlotInfo slot = mock.get_slot_info(i);
        if (slot.spoolman_id <= 0) {
            continue; // intentionally unlinked lane (untracked path)
        }
        INFO("slot " << i << " -> spoolman_id " << slot.spoolman_id);
        auto it = spools.find(slot.spoolman_id);
        REQUIRE(it != spools.end());
        const SpoolInfo& spool = it->second;

        CHECK(slot.material == spool.material);
        CHECK(slot.brand == spool.vendor);
        // Spoolman's filament.name is a filament name, not a colour word, so it
        // lands on spool_name. SlotInfo::color_name stays a colour label and is
        // left unset by apply_spool_to_slot — the label resolver derives one
        // from color_hex when nothing better exists.
        CHECK(slot.spool_name == spool.filament_name);

        uint32_t spool_rgb = 0;
        REQUIRE(helix::parse_hex_color(spool.color_hex.c_str(), spool_rgb));
        CHECK(slot.color_rgb == spool_rgb);

        CHECK(slot.remaining_weight_g == Catch::Approx(spool.remaining_weight_g).margin(0.5));
        CHECK(slot.total_weight_g == Catch::Approx(spool.initial_weight_g).margin(0.5));
    }
}

} // namespace

TEST_CASE("AFC mock slots match mock Spoolman spools (spec §9 drift)",
          "[mock][spoolman][ams_edit_overlay]") {
    AmsBackendMock mock(8);
    mock.set_afc_mode(true);
    check_backend_against_spoolman(mock, 8);
}

TEST_CASE("Happy Hare mock slots match mock Spoolman spools (spec §9 drift)",
          "[mock][spoolman][ams_edit_overlay]") {
    AmsBackendMock mock(8);
    check_backend_against_spoolman(mock, 8);
}

TEST_CASE("AFC mock keeps one unlinked lane for the untracked path",
          "[mock][spoolman][ams_edit_overlay]") {
    AmsBackendMock mock(4);
    mock.set_afc_mode(true);
    SlotInfo lane3 = mock.get_slot_info(3);
    CHECK(lane3.spoolman_id == 0);
    CHECK(lane3.brand == "Generic");
    CHECK(lane3.material == "PETG");
}
