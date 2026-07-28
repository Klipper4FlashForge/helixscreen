// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui_update_queue.h"

#include "../lvgl_ui_test_fixture.h"
#include "ams_backend_mock.h"
#include "ams_error_bridge.h"
#include "ams_state.h"
#include "post_op_cooldown_manager.h"
#include "recovery_modal_presenter.h"

#include "../catch_amalgamated.hpp"

namespace {
class ErrorReportingBackend : public AmsBackendMock {
  public:
    explicit ErrorReportingBackend(int slots) : AmsBackendMock(slots) {}
    void set_error(std::optional<helix::ErrorEvent> e) {
        err_ = std::move(e);
    }
    std::optional<helix::ErrorEvent> current_error() const override {
        return err_;
    }

  private:
    std::optional<helix::ErrorEvent> err_;
};
} // namespace

TEST_CASE_METHOD(LVGLUITestFixture, "AmsErrorBridge presents on ERROR edge, dismisses on exit",
                 "[error-center][ams-bridge]") {
    auto& ams = AmsState::instance();
    ams.init_subjects(true);
    auto backend = std::make_unique<ErrorReportingBackend>(4);
    auto* raw = backend.get();
    ams.set_backend(std::move(backend));

    helix::ErrorEvent e;
    e.source = helix::ErrorSource::IFS;
    e.severity = helix::ErrorSeverity::CRITICAL;
    e.detail = "IFS unload timed out";
    e.recovery_actions = {{"Recover", "IFS_UNLOCK", "ifs::unlock", "primary"}};
    raw->set_error(e);

    helix::ui::RecoveryModalPresenter presenter(nullptr);
    helix::AmsErrorBridge bridge(presenter);
    bridge.start();

    // Drive action → ERROR.
    ams.set_action(AmsAction::ERROR);
    helix::ui::UpdateQueue::instance().drain();
    process_lvgl(20);
    CHECK(presenter.is_visible());

    // Drive action → IDLE: bridge dismisses.
    raw->set_error(std::nullopt);
    ams.set_action(AmsAction::IDLE);
    helix::ui::UpdateQueue::instance().drain();
    process_lvgl(20);
    CHECK_FALSE(presenter.is_visible());

    ams.set_backend(nullptr);
}

TEST_CASE_METHOD(LVGLUITestFixture, "AmsErrorBridge does nothing when current_error is null",
                 "[error-center][ams-bridge]") {
    auto& ams = AmsState::instance();
    ams.init_subjects(true);
    auto backend = std::make_unique<ErrorReportingBackend>(4); // err_ defaults to nullopt
    ams.set_backend(std::move(backend));
    helix::ui::RecoveryModalPresenter presenter(nullptr);
    helix::AmsErrorBridge bridge(presenter);
    bridge.start();
    ams.set_action(AmsAction::ERROR);
    helix::ui::UpdateQueue::instance().drain();
    process_lvgl(20);
    CHECK_FALSE(presenter.is_visible());
    ams.set_backend(nullptr);
}

namespace {
/// Park the AMS action at IDLE so bridge.start()'s synchronous first tick is
/// not itself an ERROR edge (the subject is global and survives across tests).
void park_action_idle(LVGLUITestFixture& fx) {
    AmsState::instance().set_action(AmsAction::IDLE);
    helix::ui::UpdateQueue::instance().drain();
    fx.process_lvgl(10);
}

/// Arm a real post-op cooldown and confirm it is pending before the test acts.
void arm_cooldown(LVGLUITestFixture& fx) {
    auto& cd = PostOpCooldownManager::instance();
    cd.init();
    cd.cancel();
    fx.process_lvgl(10);
    cd.schedule();
    helix::ui::UpdateQueue::instance().drain();
    fx.process_lvgl(10);
}
} // namespace

// The cooldown armed by an EARLIER operation keeps counting through a fault and
// zeroes the extruder 120s later, so whichever recovery action the user taps runs
// into a cold nozzle and fails exactly like the operation that faulted. The
// ERROR edge must disarm it.
//
// Both cases below deliberately use a backend whose current_error() is nullopt —
// AFC's shape, since AmsBackendAfc does not override it. A cancel placed after
// the `backend`/`ev` early-returns in on_action_changed() passes nothing here.
TEST_CASE_METHOD(LVGLUITestFixture,
                 "AmsErrorBridge cancels a pending post-op cooldown on the ERROR edge",
                 "[error-center][ams-bridge]") {
    auto& ams = AmsState::instance();
    ams.init_subjects(true);
    ams.set_backend(std::make_unique<ErrorReportingBackend>(4)); // current_error() == nullopt

    park_action_idle(*this);
    arm_cooldown(*this);
    auto& cd = PostOpCooldownManager::instance();
    REQUIRE(cd.has_pending_timer());

    helix::ui::RecoveryModalPresenter presenter(nullptr);
    helix::AmsErrorBridge bridge(presenter);
    bridge.start();

    ams.set_action(AmsAction::ERROR);
    helix::ui::UpdateQueue::instance().drain();
    process_lvgl(20);

    CHECK_FALSE(cd.has_pending_timer());
    CHECK_FALSE(presenter.is_visible()); // nullopt: nothing to present, cancel still ran

    ams.set_backend(nullptr);
    cd.cancel();
    process_lvgl(10);
}

TEST_CASE_METHOD(LVGLUITestFixture,
                 "AmsErrorBridge cancels the cooldown even with no backend attached",
                 "[error-center][ams-bridge]") {
    auto& ams = AmsState::instance();
    ams.init_subjects(true);
    ams.set_backend(nullptr);

    park_action_idle(*this);
    arm_cooldown(*this);
    auto& cd = PostOpCooldownManager::instance();
    REQUIRE(cd.has_pending_timer());

    helix::ui::RecoveryModalPresenter presenter(nullptr);
    helix::AmsErrorBridge bridge(presenter);
    bridge.start();

    ams.set_action(AmsAction::ERROR);
    helix::ui::UpdateQueue::instance().drain();
    process_lvgl(20);

    CHECK_FALSE(cd.has_pending_timer());

    cd.cancel();
    process_lvgl(10);
}

// The cooldown is armed by a SUCCESSFUL operation completing; a transition into
// any non-ERROR action must leave it alone or the nozzle never cools after a
// normal swap.
TEST_CASE_METHOD(LVGLUITestFixture, "AmsErrorBridge leaves the cooldown alone on a non-ERROR edge",
                 "[error-center][ams-bridge]") {
    auto& ams = AmsState::instance();
    ams.init_subjects(true);
    ams.set_backend(nullptr);

    park_action_idle(*this);
    arm_cooldown(*this);
    auto& cd = PostOpCooldownManager::instance();
    REQUIRE(cd.has_pending_timer());

    helix::ui::RecoveryModalPresenter presenter(nullptr);
    helix::AmsErrorBridge bridge(presenter);
    bridge.start();

    ams.set_action(AmsAction::LOADING);
    helix::ui::UpdateQueue::instance().drain();
    process_lvgl(20);

    CHECK(cd.has_pending_timer());

    cd.cancel();
    process_lvgl(10);
}
