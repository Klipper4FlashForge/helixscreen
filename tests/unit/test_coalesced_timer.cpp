// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_coalesced_timer.h"

#include "../lvgl_test_fixture.h"

#include <memory>

#include "../catch_amalgamated.hpp"

using namespace helix::ui;

namespace {

/// How many live LVGL timers carry @p p as their user_data. Compares pointers
/// only - never dereferences, so it is safe to ask about an object that has
/// already been destroyed.
///
/// Counted rather than tested as a boolean on purpose. lv_timer_cancel_safe()
/// neuters a timer without unlinking it or clearing its user_data, and the
/// linked-but-dead entry survives until the next lv_timer_handler pass. Earlier
/// tests therefore leave timers pointing at their own dead stack frames, and
/// those addresses get reused. Only the *delta* across a destructor is
/// meaningful; an absolute "nothing points here" is a coin flip on stack reuse.
int lvgl_timers_pointing_at(const void* p) {
    int n = 0;
    for (lv_timer_t* t = lv_timer_get_next(nullptr); t != nullptr; t = lv_timer_get_next(t)) {
        if (lv_timer_get_user_data(t) == p) {
            n++;
        }
    }
    return n;
}

} // namespace

TEST_CASE_METHOD(LVGLTestFixture, "CoalescedTimer: single schedule fires callback once",
                 "[coalesced_timer]") {
    int call_count = 0;
    CoalescedTimer timer(1);

    timer.schedule([&call_count]() { call_count++; });

    REQUIRE(timer.pending());
    process_lvgl(50);

    REQUIRE(call_count == 1);
    REQUIRE_FALSE(timer.pending());
}

TEST_CASE_METHOD(LVGLTestFixture, "CoalescedTimer: multiple rapid schedules coalesce to one call",
                 "[coalesced_timer]") {
    int call_count = 0;
    CoalescedTimer timer(10);

    // Schedule 5 times rapidly — should coalesce into a single callback
    for (int i = 0; i < 5; i++) {
        timer.schedule([&call_count]() { call_count++; });
    }

    REQUIRE(timer.pending());
    process_lvgl(50);

    REQUIRE(call_count == 1);
}

TEST_CASE_METHOD(LVGLTestFixture, "CoalescedTimer: cancel prevents callback from firing",
                 "[coalesced_timer]") {
    int call_count = 0;
    CoalescedTimer timer(10);

    timer.schedule([&call_count]() { call_count++; });
    REQUIRE(timer.pending());

    timer.cancel();
    REQUIRE_FALSE(timer.pending());

    process_lvgl(50);
    REQUIRE(call_count == 0);
}

TEST_CASE_METHOD(LVGLTestFixture, "CoalescedTimer: destructor cancels pending timer",
                 "[coalesced_timer]") {
    // A destructor that leaves the timer armed fires timer_cb() into a callback
    // whose storage is gone - a live main-thread crash source. The flag below is
    // held in a shared_ptr so it outlives the timer and can be read afterwards.
    auto fired = std::make_shared<bool>(false);
    const void* timer_addr = nullptr;
    int pointing_before = 0;

    {
        CoalescedTimer timer(20);
        timer.schedule([fired]() { *fired = true; });
        REQUIRE(timer.pending());

        timer_addr = &timer;
        pointing_before = lvgl_timers_pointing_at(timer_addr);
        REQUIRE(pointing_before >= 1); // our own armed timer, at least
    } // destructor must cancel()

    // Checked before pumping, and by pointer comparison only: cancel() nulls the
    // timer's user_data, so a destructor that skips it shows up here cleanly
    // instead of by running the leaked callback into freed memory.
    //
    // There is no timer-count check to pair with this. cancel() routes through
    // lv_timer_cancel_safe(), which neuters the timer and leaves lv_timer_handler
    // to reap it, so the timer is still linked at this point either way.
    REQUIRE(lvgl_timers_pointing_at(timer_addr) == pointing_before - 1);

    // Well past the 20ms period.
    process_lvgl(200);
    REQUIRE_FALSE(*fired);
}

TEST_CASE_METHOD(LVGLTestFixture, "CoalescedTimer: re-schedule after fire works",
                 "[coalesced_timer]") {
    int call_count = 0;
    CoalescedTimer timer(1);

    timer.schedule([&call_count]() { call_count++; });
    process_lvgl(50);
    REQUIRE(call_count == 1);

    // Schedule again after first fire
    timer.schedule([&call_count]() { call_count++; });
    REQUIRE(timer.pending());
    process_lvgl(50);
    REQUIRE(call_count == 2);
}

TEST_CASE_METHOD(LVGLTestFixture,
                 "CoalescedTimer: last callback wins when schedule called multiple times",
                 "[coalesced_timer]") {
    int value = 0;
    CoalescedTimer timer(10);

    timer.schedule([&value]() { value = 1; });
    timer.schedule([&value]() { value = 2; });
    timer.schedule([&value]() { value = 3; });

    process_lvgl(50);
    REQUIRE(value == 3);
}

TEST_CASE_METHOD(LVGLTestFixture, "CoalescedTimer: move transfers pending timer",
                 "[coalesced_timer]") {
    int call_count = 0;
    CoalescedTimer timer1(10);
    timer1.schedule([&call_count]() { call_count++; });
    REQUIRE(timer1.pending());

    CoalescedTimer timer2(std::move(timer1));
    REQUIRE_FALSE(timer1.pending());
    REQUIRE(timer2.pending());

    process_lvgl(50);
    REQUIRE(call_count == 1);
}

TEST_CASE_METHOD(LVGLTestFixture, "CoalescedTimer: default period is 1ms", "[coalesced_timer]") {
    int call_count = 0;
    CoalescedTimer timer; // default period

    timer.schedule([&call_count]() { call_count++; });
    process_lvgl(50);

    REQUIRE(call_count == 1);
}
