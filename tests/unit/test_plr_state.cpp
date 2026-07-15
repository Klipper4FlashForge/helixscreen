// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_plr_state.cpp
 * @brief Unit tests for PrinterState::pl_env_valid / pl_recovery_file
 *
 * Snapmaker-fork Power-Loss-Recovery fields carried on virtual_sdcard.
 * Absent (harmless) on mainline Klipper; Moonraker sends explicit null for
 * subscribed fields the connected firmware doesn't populate, so parsing must
 * type-check every field rather than assume presence implies a usable value.
 */

#include "../test_helpers/printer_state_test_access.h"
#include "../ui_test_utils.h"
#include "printer_print_state.h"
#include "printer_state.h"

#include <spdlog/spdlog.h>

#include "../catch_amalgamated.hpp"

using namespace helix;
using json = nlohmann::json;

namespace {

class PlrStateTestFixture {
  public:
    PlrStateTestFixture() {
        lv_init_safe();

        if (!display_created_) {
            display_ = lv_display_create(480, 320);
            alignas(64) static lv_color_t buf[480 * 10];
            lv_display_set_buffers(display_, buf, nullptr, sizeof(buf),
                                   LV_DISPLAY_RENDER_MODE_PARTIAL);
            lv_display_set_flush_cb(display_, [](lv_display_t* disp, const lv_area_t*, uint8_t*) {
                lv_display_flush_ready(disp);
            });
            display_created_ = true;
        }

        PrinterStateTestAccess::reset(state_);
        state_.init_subjects(false);
    }

    ~PlrStateTestFixture() {
        PrinterStateTestAccess::reset(state_);
    }

  protected:
    PrinterState& state() {
        return state_;
    }

    bool pl_env_valid() {
        return state_.is_pl_env_valid();
    }

    const std::string& pl_recovery_file() {
        return state_.pl_recovery_file();
    }

  private:
    PrinterState state_;
    static lv_display_t* display_;
    static bool display_created_;
};

lv_display_t* PlrStateTestFixture::display_ = nullptr;
bool PlrStateTestFixture::display_created_ = false;

} // namespace

TEST_CASE_METHOD(PlrStateTestFixture, "PLR state: default is invalid with no recovery file",
                 "[plr][state]") {
    REQUIRE(pl_env_valid() == false);
    REQUIRE(pl_recovery_file().empty());
}

TEST_CASE_METHOD(PlrStateTestFixture, "PLR state: pl_env_valid=true + file_path parses",
                 "[plr][state]") {
    json status = {{"virtual_sdcard",
                    {{"progress", 0.0},
                     {"layer", nullptr},
                     {"layer_count", nullptr},
                     {"is_active", false},
                     {"pl_env_valid", true},
                     {"file_path", "benchy.gcode"}}}};
    state().update_from_status(status);
    REQUIRE(pl_env_valid() == true);
    REQUIRE(pl_recovery_file() == "benchy.gcode");
}

TEST_CASE_METHOD(PlrStateTestFixture, "PLR state: explicit null pl_env_valid leaves default",
                 "[plr][state]") {
    // Mainline Klipper (no power_loss_check module) subscribes the field but
    // Moonraker sends an explicit null since virtual_sdcard never sets it.
    json status = {{"virtual_sdcard",
                    {{"progress", 0.0},
                     {"is_active", false},
                     {"pl_env_valid", nullptr},
                     {"file_path", nullptr}}}};
    state().update_from_status(status);
    REQUIRE(pl_env_valid() == false);
    REQUIRE(pl_recovery_file().empty());
}

TEST_CASE_METHOD(PlrStateTestFixture, "PLR state: missing fields entirely leaves default",
                 "[plr][state]") {
    json status = {{"virtual_sdcard", {{"progress", 0.0}, {"is_active", false}}}};
    state().update_from_status(status);
    REQUIRE(pl_env_valid() == false);
    REQUIRE(pl_recovery_file().empty());
}

TEST_CASE_METHOD(PlrStateTestFixture, "PLR state: pl_env_valid flips true then back to false",
                 "[plr][state]") {
    json valid = {{"virtual_sdcard",
                   {{"progress", 0.0},
                    {"is_active", false},
                    {"pl_env_valid", true},
                    {"file_path", "recovered.gcode"}}}};
    state().update_from_status(valid);
    REQUIRE(pl_env_valid() == true);

    // Boot-time recovery consumed (e.g. SDCARD_PRINT_PL_CLEAR_ENV or a fresh
    // print start clears the env) — firmware reports false on the next update.
    json cleared = {
        {"virtual_sdcard", {{"progress", 0.0}, {"is_active", false}, {"pl_env_valid", false}}}};
    state().update_from_status(cleared);
    REQUIRE(pl_env_valid() == false);
}
