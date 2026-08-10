// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui_update_queue.h"

#include "../lvgl_ui_test_fixture.h"
#include "../test_helpers/gcode_error_router_test_access.h"
#include "../test_helpers/recovery_modal_presenter_test_access.h"
#include "../ui_test_utils.h"
#include "ams_backend_mock.h"
#include "ams_error_bridge.h"
#include "ams_state.h"
#include "app_globals.h"
#include "gcode_error_router.h"
#include "post_op_cooldown_manager.h"
#include "printer_state.h"
#include "recovery_modal_presenter.h"

#include <string>
#include <vector>

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
    for (auto action :
         {AmsAction::LOADING, AmsAction::HEATING, AmsAction::UNLOADING, AmsAction::IDLE}) {
        ams.set_action(action);
        helix::ui::UpdateQueue::instance().drain();
        process_lvgl(20);
    }

    CHECK(toasts.empty());

    ams.set_backend(nullptr);
}

// ============================================================================
// #1197: the fallback's "is this already visible" guard was modal-shaped only.
// A fault GcodeErrorRouter classified to a TOAST goes through neither
// presenter_ nor AmsPanel's dialog, so the bridge saw an empty screen and
// added a second notification for the same fault. Toasts carry no identity of
// their own — ToastManager::is_visible() is blanket and using it would let an
// unrelated toast silence a genuine fault — so the correlation runs on the
// fault text via fault_surface_correlation.
//
// These drive the REAL router through GcodeErrorRouterTestAccess rather than
// seeding the registry by hand: the recording site is half the fix.
// ============================================================================

namespace {
/// Not paused, not printing — error_classify makes an uncoded `!!` a WARNING,
/// which decide_presentation maps to PresentAs::TOAST. This is the only
/// classification that reaches the router's toast arms.
void park_printer_idle() {
    get_printer_state().update_from_status(
        nlohmann::json{{"pause_resume", {{"is_paused", false}}}});
}

/// AFC emits `!! <msg>` and AFC_logger.error() queues the byte-identical
/// string, so the router's detail and the backend's operation_detail are the
/// same string. That equality is what the correlation matches on.
constexpr const char* kAfcFault = "lane1 filament failed to trigger toolhead sensor";
} // namespace

TEST_CASE_METHOD(LVGLUITestFixture,
                 "AmsErrorBridge does not double-toast a fault the router already toasted",
                 "[error-center][ams-bridge][1197]") {
    park_printer_idle();
    REQUIRE_FALSE(get_printer_state().is_paused());

    auto& ams = AmsState::instance();
    ams.init_subjects(true);
    auto backend = std::make_unique<ErrorReportingBackend>(4); // current_error() == nullopt
    backend->set_operation_detail(kAfcFault);
    ams.set_backend(std::move(backend));

    park_action_idle(*this);

    helix::ui::RecoveryModalPresenter presenter(nullptr);
    helix::AmsErrorBridge bridge(presenter);
    bridge.start();

    ToastCapture toasts;

    // Klipper's broadcast lands first — AFC emits `!!` before it calls
    // pause_print(), so this is the real ordering on hardware.
    helix::GcodeErrorRouter router(nullptr, nullptr, presenter);
    GcodeErrorRouterTestAccess::process_line(router, std::string("!! ") + kAfcFault);

    // Then AFC's status delta raises error_state and the action edges to ERROR.
    ams.set_action(AmsAction::ERROR);
    helix::ui::UpdateQueue::instance().drain();
    process_lvgl(300); // past present_deferred_toast's 150ms timer

    REQUIRE_FALSE(presenter.is_visible()); // toast path: no modal to see
    CHECK(toasts.messages().size() == 1);  // the router's, not two
    CHECK(toasts.messages()[0].find(kAfcFault) != std::string::npos);

    ams.set_backend(nullptr);
}

TEST_CASE_METHOD(LVGLUITestFixture,
                 "AmsErrorBridge still toasts a fault the router's toast was not about",
                 "[error-center][ams-bridge][1197]") {
    // The whole point of keying on the fault text: a blanket "a toast happened
    // recently" guard would swallow this second, genuinely different fault and
    // leave the user with a stopped spinner and no explanation.
    park_printer_idle();

    auto& ams = AmsState::instance();
    ams.init_subjects(true);
    auto backend = std::make_unique<ErrorReportingBackend>(4);
    backend->set_operation_detail("Unloading lane 2 (timed out)");
    ams.set_backend(std::move(backend));

    park_action_idle(*this);

    helix::ui::RecoveryModalPresenter presenter(nullptr);
    helix::AmsErrorBridge bridge(presenter);
    bridge.start();

    ToastCapture toasts;

    helix::GcodeErrorRouter router(nullptr, nullptr, presenter);
    GcodeErrorRouterTestAccess::process_line(router, std::string("!! ") + kAfcFault);

    ams.set_action(AmsAction::ERROR);
    helix::ui::UpdateQueue::instance().drain();
    process_lvgl(300);

    REQUIRE(toasts.messages().size() == 2);
    bool saw_router = false;
    bool saw_bridge = false;
    for (const auto& m : toasts.messages()) {
        saw_router |= m.find(kAfcFault) != std::string::npos;
        saw_bridge |= m.find("Unloading lane 2 (timed out)") != std::string::npos;
    }
    CHECK(saw_router);
    CHECK(saw_bridge);

    ams.set_backend(nullptr);
}

TEST_CASE_METHOD(LVGLUITestFixture,
                 "The router stands its toast down when the bridge's fallback got there first",
                 "[error-center][ams-bridge][1197]") {
    // Reverse ordering: a backend raises ERROR locally before Klipper's
    // broadcast arrives (AFC's stuck-action timeout shape). The bridge toasts,
    // then the same fault arrives as a `!!` — the router must not stack a
    // second transient notification on top of it.
    park_printer_idle();

    auto& ams = AmsState::instance();
    ams.init_subjects(true);
    auto backend = std::make_unique<ErrorReportingBackend>(4);
    backend->set_operation_detail(kAfcFault);
    ams.set_backend(std::move(backend));

    park_action_idle(*this);

    helix::ui::RecoveryModalPresenter presenter(nullptr);
    helix::AmsErrorBridge bridge(presenter);
    bridge.start();

    ToastCapture toasts;
    ams.set_action(AmsAction::ERROR);
    helix::ui::UpdateQueue::instance().drain();
    process_lvgl(50);
    REQUIRE(toasts.messages().size() == 1); // the fallback fired

    helix::GcodeErrorRouter router(nullptr, nullptr, presenter);
    GcodeErrorRouterTestAccess::process_line(router, std::string("!! ") + kAfcFault);
    process_lvgl(300);

    CHECK(toasts.messages().size() == 1);

    ams.set_backend(nullptr);
}

TEST_CASE_METHOD(LVGLUITestFixture,
                 "A prior fallback toast never stands the router's recovery modal down",
                 "[error-center][ams-bridge][1197]") {
    // The asymmetry that keeps the suppression honest. A modal carries the
    // recovery actions; a toast carries none. Treating "already surfaced as a
    // toast" as reason enough to skip the modal would drop the only thing the
    // user can act on. Paused + uncoded `!!` classifies CRITICAL and, since
    // #1152, carries a generic Resume action -> MODAL_WITH_RECOVER.
    get_printer_state().update_from_status(nlohmann::json{{"pause_resume", {{"is_paused", true}}}});
    REQUIRE(get_printer_state().is_paused());

    auto& ams = AmsState::instance();
    ams.init_subjects(true);
    auto backend = std::make_unique<ErrorReportingBackend>(4);
    backend->set_operation_detail(kAfcFault);
    ams.set_backend(std::move(backend));

    park_action_idle(*this);

    helix::ui::RecoveryModalPresenter presenter(nullptr);
    helix::AmsErrorBridge bridge(presenter);
    bridge.start();

    ToastCapture toasts;
    ams.set_action(AmsAction::ERROR);
    helix::ui::UpdateQueue::instance().drain();
    process_lvgl(50);
    REQUIRE(toasts.messages().size() == 1); // fallback claimed the fault

    helix::GcodeErrorRouter router(nullptr, nullptr, presenter);
    GcodeErrorRouterTestAccess::process_line(router, std::string("!! ") + kAfcFault);
    process_lvgl(300);

    CHECK(presenter.is_visible()); // modal still shown despite the prior claim

    presenter.dismiss();
    process_lvgl(20);
    ams.set_backend(nullptr);
}

// Without the ams_action_detail observer, a fault whose text changes while
// AmsAction stays ERROR never re-presents and the user keeps reading the first
// message. The bridge now re-consults current_error() on a detail change, and
// RecoveryModalPresenter::present() dedups so an unchanged fault is a no-op.
TEST_CASE_METHOD(LVGLUITestFixture,
                 "AmsErrorBridge re-presents when fault detail changes mid-ERROR",
                 "[error-center][ams-bridge]") {
    auto& ams = AmsState::instance();
    ams.init_subjects(true);
    auto backend = std::make_unique<ErrorReportingBackend>(4);
    auto* raw = backend.get();

    helix::ErrorEvent first;
    first.source = helix::ErrorSource::IFS;
    first.severity = helix::ErrorSeverity::CRITICAL;
    first.detail = "IFS unload timed out";
    first.recovery_actions = {{"Recover", "IFS_UNLOCK", "ifs::unlock", "primary"}};
    raw->set_error(first);
    raw->set_operation_detail(first.detail);
    ams.set_backend(std::move(backend));

    park_action_idle(*this);

    helix::ui::RecoveryModalPresenter presenter(nullptr);
    helix::AmsErrorBridge bridge(presenter);
    bridge.start();

    // Rising edge: present the first fault.
    ams.set_action(AmsAction::ERROR);
    helix::ui::UpdateQueue::instance().drain();
    process_lvgl(20);
    REQUIRE(presenter.is_visible());
    REQUIRE(RecoveryModalPresenterTestAccess::shown_detail(presenter) == first.detail);

    // Mid-ERROR: the backend's fault text moves on while action stays ERROR.
    helix::ErrorEvent second;
    second.source = helix::ErrorSource::IFS;
    second.severity = helix::ErrorSeverity::CRITICAL;
    second.detail = "IFS load failed at extruder";
    second.recovery_actions = {{"Recover", "IFS_UNLOCK", "ifs::unlock", "primary"}};
    raw->set_error(second);
    raw->set_operation_detail(second.detail);
    // Drive the ams_action_detail subject the way sync_from_backend() does in
    // production when the backend's operation_detail moves on — without re-
    // reading the backend's action (the mock still reports IDLE, which would
    // reset the action subject and falsely dismiss).
    ams.set_action_detail(second.detail);
    helix::ui::UpdateQueue::instance().drain();
    process_lvgl(20);

    // The stale read: without the detail observer this would still be first.detail.
    REQUIRE(presenter.is_visible()); // same ERROR episode
    REQUIRE(RecoveryModalPresenterTestAccess::shown_detail(presenter) == second.detail);

    // Falling edge still dismisses.
    raw->set_error(std::nullopt);
    ams.set_action(AmsAction::IDLE);
    helix::ui::UpdateQueue::instance().drain();
    process_lvgl(20);
    CHECK_FALSE(presenter.is_visible());

    ams.set_backend(nullptr);
}
