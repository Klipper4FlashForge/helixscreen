// SPDX-License-Identifier: GPL-3.0-or-later
#include "ams_error_bridge.h"

#include "ams_state.h"
#include "observer_factory.h"
#include "post_op_cooldown_manager.h"

#include <spdlog/spdlog.h>

namespace helix {

AmsErrorBridge::AmsErrorBridge(helix::ui::RecoveryModalPresenter& presenter)
    : presenter_(presenter) {}

void AmsErrorBridge::start() {
    // One-shot: Application calls this once. Re-calling would reinstall the
    // observer while leaving prev_action_/presented_ stale — don't.
    action_observer_ = helix::ui::observe_int_sync<AmsErrorBridge>(
        AmsState::instance().get_ams_action_subject(), this,
        [](AmsErrorBridge* self, int action) { self->on_action_changed(action); });
}

void AmsErrorBridge::on_action_changed(int action) {
    const bool now_error = (action == static_cast<int>(AmsAction::ERROR));
    const bool was_error = (prev_action_ == static_cast<int>(AmsAction::ERROR));
    prev_action_ = action;
    if (now_error && !was_error) {
        // Disarm the post-op cooldown an EARLIER operation left running. Nothing
        // else cancels it on a fault, so ~120s after the error it zeroes the
        // extruder and whichever recovery the user taps next runs into a cold
        // nozzle — failing exactly the way the operation that faulted did.
        //
        // Before the early returns below, deliberately: AmsBackendAfc does not
        // override current_error(), so for AFC `ev` is always nullopt and
        // anything placed after that check never runs on the one backend whose
        // recovery dialog reaches the user through a different path.
        PostOpCooldownManager::instance().cancel();

        auto* backend = AmsState::instance().get_backend();
        if (!backend)
            return;
        auto ev = backend->current_error();
        if (ev) {
            spdlog::debug("[AmsErrorBridge] presenting error: {}", ev->detail);
            presenter_.present(*ev);
            presented_ = true;
        }
    } else if (!now_error && was_error && presented_) {
        spdlog::debug("[AmsErrorBridge] dismissing error modal (action exited ERROR)");
        presenter_.dismiss();
        presented_ = false;
    }
}

} // namespace helix
