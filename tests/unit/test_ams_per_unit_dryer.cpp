// SPDX-License-Identifier: GPL-3.0-or-later

#include "../lvgl_test_fixture.h"
#include "../test_helpers/qidi_box_test_access.h"
#include "ams_backend_qidi.h"
#include "ams_state.h"

#include <ctime>
#include <memory>

#include "../catch_amalgamated.hpp"

using namespace helix;
using namespace helix::printer;
using json = nlohmann::json;

// Build a 2-box QIDI backend with box1 actively drying and box2 idle.
static std::unique_ptr<AmsBackendQidi> make_two_box_qidi_one_drying() {
    auto backend = std::make_unique<AmsBackendQidi>(nullptr, nullptr);
    QidiBoxTestAccess::parse_vars(*backend, json{{"box_count", 2}});
    QidiBoxTestAccess::set_clock(*backend, [] { return std::time_t{1'000'000}; });
    QidiBoxTestAccess::set_drying_timer_supported(*backend, true);
    QidiBoxTestAccess::handle_status(
        *backend,
        json{{"heater_generic heater_box1", json{{"temperature", 48.0}, {"target", 55.0}}},
             {"heater_generic heater_box2", json{{"temperature", 24.0}, {"target", 0.0}}}});
    QidiBoxTestAccess::apply_box_extras(
        *backend, json{{"box_drying_state",
                        json{{"box1", json{{"end_time", 1'000'000 + 30 * 60}}},
                             {"box2", json{{"end_time", 0}}}}}});
    return backend;
}

TEST_CASE_METHOD(LVGLTestFixture, "Per-unit dryer fan-out: only the drying box is active",
                 "[ams][dryer][multi-unit][fanout]") {
    auto& ams = AmsState::instance();
    ams.init_subjects(false);
    ams.set_backend(make_two_box_qidi_one_drying());
    ams.sync_from_backend();

    lv_subject_t* a0 = ams.get_env_ind_drying_active_subject(0);
    lv_subject_t* a1 = ams.get_env_ind_drying_active_subject(1);
    REQUIRE(a0 != nullptr);
    REQUIRE(a1 != nullptr);
    CHECK(lv_subject_get_int(a0) == 1);
    CHECK(lv_subject_get_int(a1) == 0);

    ams.deinit_subjects();
}
