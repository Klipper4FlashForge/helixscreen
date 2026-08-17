// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_host_power_availability.cpp
 * @brief Host power controls (shutdown/reboot UI) are unavailable on Android.
 *
 * On Android, helixscreen is a tablet app: "screen" reboot/shutdown cannot
 * work (no logind, systemctl, or busybox init to call), and users reported
 * the host reboot RPC failing against their printer hosts. The availability
 * rule lives in ONE predicate — helix::platform_host_power_supported() —
 * which seeds the platform_host_power_supported subject; the home-grid
 * widget registry gates the shutdown widget on that subject.
 */

#include "../lvgl_ui_test_fixture.h"
#include "lvgl/lvgl.h"
#include "panel_widget_registry.h"
#include "platform_info.h"

#include <string>

#include "../catch_amalgamated.hpp"

using namespace helix;

namespace {

/// RAII platform-override guard — every test that flips the override restores
/// it on scope exit so one failure cannot leak Android-ness into other tests.
class PlatformOverrideGuard {
  public:
    explicit PlatformOverrideGuard(int value) {
        set_platform_override(value);
    }
    ~PlatformOverrideGuard() {
        set_platform_override(-1);
    }
};

} // namespace

TEST_CASE("Host power predicate follows the platform override", "[android][platform][power]") {
    SECTION("non-Android default supports host power") {
        set_platform_override(-1);
        CHECK(platform_host_power_supported() == !is_android_platform());
        CHECK(platform_host_power_supported()); // desktop/test builds
    }
    SECTION("Android does not support host power") {
        PlatformOverrideGuard android(1);
        CHECK_FALSE(platform_host_power_supported());
    }
    SECTION("explicit non-Android override supports host power") {
        PlatformOverrideGuard desktop(0);
        CHECK(platform_host_power_supported());
    }
}

TEST_CASE_METHOD(LVGLUITestFixture,
                 "platform_host_power_supported subject is seeded from the predicate",
                 "[android][platform][power]") {
    lv_subject_t* subj = lv_xml_get_subject(nullptr, "platform_host_power_supported");
    REQUIRE(subj != nullptr);
    CHECK(lv_subject_get_int(subj) == (platform_host_power_supported() ? 1 : 0));
}

TEST_CASE("Shutdown widget is gated on host power availability", "[android][panel_widget][power]") {
    const auto* def = find_widget_def("shutdown");
    REQUIRE(def != nullptr);
    REQUIRE(def->hardware_gate_subject != nullptr);
    CHECK(std::string(def->hardware_gate_subject) == "platform_host_power_supported");
    CHECK(def->hardware_gate_hint != nullptr);
}
