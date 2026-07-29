// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui_update_queue.h"

#include "../lvgl_ui_test_fixture.h"
#include "../ui_test_utils.h"
#include "ams_backend_mock.h"
#include "ams_error_bridge.h"
#include "ams_state.h"
#include "post_op_cooldown_manager.h"
#include "recovery_modal_presenter.h"

#include "../catch_amalgamated.hpp"

#include <vector>

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

    /// Overlay the operation narration AmsBackendMock keeps private. This is
    /// what AFC's local timeout leaves behind ("<detail> (timed out)") and the
    /// only description of the fault on that path.
    void set_operation_detail(std::string detail) {
        detail_ = std::move(detail);
    }

    AmsSystemInfo get_system_info() const override {
        AmsSystemInfo info = AmsBackendMock::get_system_info();
        if (!detail_.empty()) {
            info.operation_detail = detail_;
        }
        return info;
    }

  private:
    std::optional<helix::ErrorEvent> err_;
    std::string detail_;
};

/// Captures the user-facing error toasts raised while it is in scope. Error
/// toasts are compiled out of the test build, so the hook in ui_test_utils is
/// the only way to observe one.
class ToastCapture {
  public:
    ToastCapture() {
        helix::ui::set_test_notification_error_hook(
            [this](const std::string& msg) { messages_.push_back(msg); });
    }
    ~ToastCapture() {
        helix::ui::set_test_notification_error_hook(nullptr);
    }
    ToastCapture(const ToastCapture&) = delete;
    ToastCapture& operator=(const ToastCapture&) = delete;

    [[nodiscard]] const std::vector<std::string>& messages() const {
        return messages_;
    }
    [[nodiscard]] bool empty() const {
        return messages_.empty();
    }

  private:
    std::vector<std::string> messages_;
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

// ============================================================================
// Last-resort fallback: a backend that raises ERROR with no ErrorEvent and no
// `!!` line (AmsBackendAfc's local action timeout) stops the spinner and shows
// the user nothing at all, which reads as success. The bridge is the one place
// that sees the ERROR edge application-wide, so it toasts — but only after a
// deferred re-check finds the screen genuinely empty of this fault.
// ============================================================================

TEST_CASE_METHOD(LVGLUITestFixture, "AmsErrorBridge toasts an ERROR nothing else surfaced",
                 "[error-center][ams-bridge]") {
    auto& ams = AmsState::instance();
    ams.init_subjects(true);
    auto backend = std::make_unique<ErrorReportingBackend>(4); // current_error() == nullopt
    auto* raw = backend.get();
    raw->set_operation_detail("Unloading lane 2 (timed out)");
    ams.set_backend(std::move(backend));

    park_action_idle(*this);

    helix::ui::RecoveryModalPresenter presenter(nullptr);
    helix::AmsErrorBridge bridge(presenter);
    bridge.start();

    ToastCapture toasts;
    ams.set_action(AmsAction::ERROR);
    helix::ui::UpdateQueue::instance().drain();
    process_lvgl(20);

    REQUIRE_FALSE(presenter.is_visible()); // nullopt: nothing presented the fault
    REQUIRE(toasts.messages().size() == 1);
    CHECK(toasts.messages()[0].find("Unloading lane 2 (timed out)") != std::string::npos);

    ams.set_backend(nullptr);
}

TEST_CASE_METHOD(LVGLUITestFixture, "AmsErrorBridge does not toast when it presented the modal",
                 "[error-center][ams-bridge]") {
    auto& ams = AmsState::instance();
    ams.init_subjects(true);
    auto backend = std::make_unique<ErrorReportingBackend>(4);
    auto* raw = backend.get();
    raw->set_operation_detail("Unloading lane 2 (timed out)");

    helix::ErrorEvent e;
    e.source = helix::ErrorSource::IFS;
    e.severity = helix::ErrorSeverity::CRITICAL;
    e.detail = "IFS unload timed out";
    e.recovery_actions = {{"Recover", "IFS_UNLOCK", "ifs::unlock", "primary"}};
    raw->set_error(e);
    ams.set_backend(std::move(backend));

    park_action_idle(*this);

    helix::ui::RecoveryModalPresenter presenter(nullptr);
    helix::AmsErrorBridge bridge(presenter);
    bridge.start();

    ToastCapture toasts;
    ams.set_action(AmsAction::ERROR);
    helix::ui::UpdateQueue::instance().drain();
    process_lvgl(20);

    REQUIRE(presenter.is_visible());
    CHECK(toasts.empty());

    ams.set_backend(nullptr);
}

// The recovery modal GcodeErrorRouter raises for a classified `!!` line lands
// in the same presenter, so a fault already described there must not collect a
// toast on top of it.
TEST_CASE_METHOD(LVGLUITestFixture, "AmsErrorBridge does not toast under a visible recovery modal",
                 "[error-center][ams-bridge]") {
    auto& ams = AmsState::instance();
    ams.init_subjects(true);
    auto backend = std::make_unique<ErrorReportingBackend>(4); // current_error() == nullopt
    auto* raw = backend.get();
    raw->set_operation_detail("Unloading lane 2 (timed out)");
    ams.set_backend(std::move(backend));

    park_action_idle(*this);

    helix::ui::RecoveryModalPresenter presenter(nullptr);
    helix::ErrorEvent e;
    e.source = helix::ErrorSource::KLIPPER;
    e.severity = helix::ErrorSeverity::CRITICAL;
    e.detail = "AFC lane 2 jam";
    e.recovery_actions = {{"Recover", "AFC_RESET", "afc::reset", "primary"}};
    presenter.present(e); // stands in for the router having already shown it
    process_lvgl(20);
    REQUIRE(presenter.is_visible());

    helix::AmsErrorBridge bridge(presenter);
    bridge.start();

    ToastCapture toasts;
    ams.set_action(AmsAction::ERROR);
    helix::ui::UpdateQueue::instance().drain();
    process_lvgl(20);

    CHECK(toasts.empty());

    presenter.dismiss();
    process_lvgl(20);
    ams.set_backend(nullptr);
}

// The deferral is what makes the check reliable, and it is also what lets the
// fault end before the check runs. An error that is already over must not be
// announced.
TEST_CASE_METHOD(LVGLUITestFixture, "AmsErrorBridge does not toast if the action left ERROR",
                 "[error-center][ams-bridge]") {
    auto& ams = AmsState::instance();
    ams.init_subjects(true);
    auto backend = std::make_unique<ErrorReportingBackend>(4);
    auto* raw = backend.get();
    raw->set_operation_detail("Unloading lane 2 (timed out)");
    ams.set_backend(std::move(backend));

    park_action_idle(*this);

    helix::ui::RecoveryModalPresenter presenter(nullptr);
    helix::AmsErrorBridge bridge(presenter);
    bridge.start();

    ToastCapture toasts;
    ams.set_action(AmsAction::ERROR);
    // One drain runs the bridge's queued observer handler, which arms the
    // fallback for the FOLLOWING tick. Clear the fault inside that window.
    helix::ui::UpdateQueue::instance().drain();
    ams.set_action(AmsAction::IDLE);
    helix::ui::UpdateQueue::instance().drain();
    process_lvgl(20);

    CHECK(toasts.empty());

    ams.set_backend(nullptr);
}

TEST_CASE_METHOD(LVGLUITestFixture, "AmsErrorBridge does not toast on non-ERROR transitions",
                 "[error-center][ams-bridge]") {
    auto& ams = AmsState::instance();
    ams.init_subjects(true);
    auto backend = std::make_unique<ErrorReportingBackend>(4);
    auto* raw = backend.get();
    raw->set_operation_detail("Unloading lane 2");
    ams.set_backend(std::move(backend));

    park_action_idle(*this);

    helix::ui::RecoveryModalPresenter presenter(nullptr);
    helix::AmsErrorBridge bridge(presenter);
    bridge.start();

    ToastCapture toasts;
    for (auto action : {AmsAction::LOADING, AmsAction::HEATING, AmsAction::UNLOADING,
                        AmsAction::IDLE}) {
        ams.set_action(action);
        helix::ui::UpdateQueue::instance().drain();
        process_lvgl(20);
    }

    CHECK(toasts.empty());

    ams.set_backend(nullptr);
}
