// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_afc_delegates_homing.cpp
 * @brief delegates_homing_to_printer(): true only when AFC.cfg's [AFC]
 * auto_home is loaded and set (#1265). False-until-loaded is the safety
 * posture — never skip a needed home, at worst one redundant prompt.
 */

#include "../lvgl_test_fixture.h"
#include "afc_config_manager.h"
#include "ams_backend_afc.h"
#include "ams_types.h"

#include <memory>
#include <vector>

#include "../catch_amalgamated.hpp"

using namespace helix;

namespace {
const char* CFG_WITH_AUTO_HOME = R"(
[AFC]
auto_home: True
)";

const char* CFG_WITHOUT_AUTO_HOME = R"(
[AFC]
tool_start: direct
)";
} // namespace

// Same friend-based access shape as test_afc_device_actions_config.cpp's
// AmsBackendAfcConfigHelper (declared friend at include/ams_backend_afc.h:520).
class AfcDelegatesHomingHelper : public AmsBackendAfc {
  public:
    AfcDelegatesHomingHelper() : AmsBackendAfc(nullptr, nullptr) {}

    void load_config(const char* content) {
        afc_config_ = std::make_unique<AfcConfigManager>(nullptr);
        afc_config_->load_from_string(content, "AFC/AFC.cfg");
    }
};

TEST_CASE_METHOD(LVGLTestFixture, "AFC delegates_homing_to_printer reads [AFC] auto_home",
                 "[afc][homing][1265]") {
    AfcDelegatesHomingHelper afc;

    // Config never loaded (first ~1-2s after connect, or fetch failed):
    // conservatively false — the prompt still fires.
    CHECK_FALSE(afc.delegates_homing_to_printer());

    afc.load_config(CFG_WITHOUT_AUTO_HOME);
    CHECK_FALSE(afc.delegates_homing_to_printer());

    afc.load_config(CFG_WITH_AUTO_HOME);
    CHECK(afc.delegates_homing_to_printer());
}

TEST_CASE_METHOD(LVGLTestFixture, "base default: no backend delegates homing",
                 "[capabilities][homing][1265]") {
    // Qualified call pins the BASE default, matching the
    // printer_reports_spool_ids pattern in test_ams_firmware_persistence.cpp.
    auto afc = std::make_unique<AmsBackendAfc>(nullptr, nullptr);
    CHECK_FALSE(afc->AmsBackend::delegates_homing_to_printer());
}
