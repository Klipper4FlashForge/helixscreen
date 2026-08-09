// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "helix_test_fixture.h"

#include "ui_modal.h"
#include "ui_nav_manager.h"
#include "ui_test_utils.h"
#include "ui_update_queue.h"

#include "app_constants.h"
#include "async_lifetime_guard.h"
#include "config.h"
#include "display_settings_manager.h"
#include "fault_surface_correlation.h"
#include "helix-xml/src/xml/lv_xml.h"
#include "system_settings_manager.h"
#include "test_helpers/config_test_access.h"
#include "test_helpers/print_control_buttons_test_access.h"
#include "tool_state.h"

#include <cstdlib>
#include <filesystem>
#include <string>
#include <unistd.h>

namespace {
// Force SDL's dummy audio driver for the WHOLE test binary, before any code can
// open a real device. SoundManager::create_backend() unconditionally constructs
// an SDLSoundBackend in HELIX_DISPLAY_SDL builds; on a developer box with a live
// PulseAudio/ALSA server, SDL_OpenAudioDevice() spins up a callback thread that
// can stall and wedge the long-running [slow] suite — the main thread then
// blocks on a futex at a non-deterministic point (looks like a random test
// hanging). CI runners have no audio device, so SDL_OpenAudioDevice() fast-fails
// and the singleton falls back; the dummy driver makes the suite behave that way
// everywhere instead of depending on the host's audio stack. This is a static
// initializer so it runs before main() — before the first SoundManager access.
// overwrite=0 lets a developer opt back into a real driver by exporting
// SDL_AUDIODRIVER themselves.
struct ForceDummyAudioDriver {
    ForceDummyAudioDriver() {
        ::setenv("SDL_AUDIODRIVER", "dummy", /*overwrite=*/0);
    }
};
const ForceDummyAudioDriver g_force_dummy_audio_driver;

namespace fs = std::filesystem;

// Per-process sandbox for everything the test binary persists.
//
// Created before main() so no static initializer or first-touch Meyer's
// singleton can cache a real config path ahead of us. Torn down at exit.
//
// What this closes, measured on this repo before the change: a full
// `make test-run` DELETED and rewrote the developer's real
// $HOME/.helixscreen/settings.json.backup and helixscreen.env.backup on every
// run. Mechanism: test_external_spool.cpp and
// test_filament_consumption_tracker.cpp remove the fallback backups to stop
// Config::init() restoring stale data over their temp config, then init() —
// which unconditionally writes a rolling backup — recreates them from test
// defaults. The rolling-backup tiers (/var/lib/helixscreen, $HOME/.helixscreen)
// are resolved independently of HELIX_CONFIG_DIR and of the config path, so
// the only way to keep a test off them is to redirect the tiers themselves.
//
// NOT redirected here: HELIX_CONFIG_DIR. That var is not a sandbox switch —
// Config::init() treats it as an authoritative override that REPLACES the
// directory of whatever path the caller passed, keeping only the filename.
// Setting it process-wide silently rewrites every `config.init(<my temp
// path>)` in the suite to the same file (measured: 34 failing assertions in
// test_config.cpp + test_display_manager.cpp). Path-shaped isolation is done
// per-subsystem instead — see reset_config_singleton().
struct ConfigSandbox {
    std::string dir;

    ConfigSandbox() {
        std::error_code ec;
        // Prefer tmpfs. Config::save() fsyncs the temp file AND the parent
        // directory (#943 durability fix), so with the sandbox on a disk-backed
        // /tmp every save() in the suite costs two real disk syncs — measured at
        // ~10s added to a 82s `make test-run`, because save() used to be a
        // silent no-op whenever the singleton had no path. On tmpfs the fsyncs
        // are free and the suite runs at its previous speed.
        fs::path base = "/dev/shm";
        if (!fs::is_directory(base, ec)) {
            base = fs::temp_directory_path(ec);
            if (ec || base.empty()) {
                base = "/tmp";
            }
        }
        // pid keeps the parallel Catch2 shards from sharing a sandbox.
        base /= "helix-test-config-" + std::to_string(::getpid());
        fs::remove_all(base, ec);
        fs::create_directories(base / "state", ec);
        fs::create_directories(base / "backup", ec);
        dir = base.string();
        apply();
    }

    // Re-point the rolling-backup tiers at the sandbox. Called per test as well
    // as at construction: several tests legitimately move these refs and then
    // "restore" them by RECOMPUTING $HOME/.helixscreen rather than by putting
    // back what was there, which would otherwise silently un-sandbox every
    // later test in the shard.
    void apply() const {
        AppConstants::Update::detail::state_dir_ref() = dir + "/state";
        AppConstants::Update::detail::backup_fallback_dir_ref() = dir + "/backup";
    }

    ~ConfigSandbox() {
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
};
const ConfigSandbox g_config_sandbox;

} // namespace

namespace helix::test {

const std::string& config_sandbox_dir() {
    return g_config_sandbox.dir;
}

void reset_config_singleton() {
    g_config_sandbox.apply();

    // ToolState persists tool_spools.json into helix::get_user_config_dir(),
    // which defaults to the RELATIVE dir "config" — i.e. the repo's own
    // config/ under the test binary's CWD. It really did write there during
    // `make test-run`, which also means a later run could load another run's
    // spool assignments. set_config_dir() is the supported override.
    helix::ToolState::instance().set_config_dir(config_sandbox_dir());

    helix::Config* cfg = helix::Config::get_instance();
    if (cfg == nullptr) {
        return;
    }
    helix::ConfigTestAccess::path(*cfg) = config_sandbox_dir() + "/settings.json";
    helix::ConfigTestAccess::data(*cfg) = nlohmann::json::object();
    helix::ConfigTestAccess::active_printer_id(*cfg).clear();
    helix::ConfigTestAccess::read_only_mode(*cfg) = false;

    // Drop any file a previous test's save() left behind, so a test that
    // re-init()s the singleton at this path sees a fresh install.
    std::error_code ec;
    fs::remove(helix::ConfigTestAccess::path(*cfg), ec);
}

} // namespace helix::test

HelixTestFixture::HelixTestFixture() {
    // Tests opt into strict L081 detection: any bg-thread tok.expired() check
    // while alive aborts the run instead of just warning. Production stays
    // at warn. See include/async_lifetime_guard.h.
    helix::internal::set_strict_bg_check(true);
    // Tests also opt into strict overlay-registration detection: any
    // push_overlay() on a widget that was never registered (so on_deactivate()
    // would never fire on dismiss) aborts instead of just warning. Production
    // stays at warn. See NavigationManager::set_overlay_registration_strict.
    NavigationManager::set_overlay_registration_strict(true);
    reset_all();
}

HelixTestFixture::~HelixTestFixture() {
    reset_all();
}

void HelixTestFixture::reset_all() {
    // LVGL + UpdateQueue must be up before we touch any subject-backed state.
    // lv_init_safe() is idempotent and also re-arms the UpdateQueue if a prior
    // fixture's destructor shut it down. Safe to call from non-LVGL tests.
    lv_init_safe();

    // Drain any callbacks queued by a prior test before we touch state they read.
    helix::ui::UpdateQueue::instance().drain();

    // SystemSettingsManager language back to "en" (matches config default).
    // init_subjects() is idempotent — first call creates the subjects, later
    // calls are no-ops. Required because set_language() writes to an LVGL subject.
    //
    // Force Config singleton creation — SystemSettingsManager::init_subjects() below
    // dereferences Config::get_instance() to read defaults — and return it to a
    // clean, sandboxed state. Doing this here (rather than only in the isolation
    // listener) also covers each SECTION leaf and undoes any init(<temp dir>) a
    // derived fixture did, which is what the scattered clear_path() calls in
    // individual tests were working around.
    helix::test::reset_config_singleton();
    helix::SystemSettingsManager::instance().init_subjects();
    helix::SystemSettingsManager::instance().set_language("en");

    // Delete any tracked modal widgets and clear the modal stack.
    ModalStack::instance().clear();

    // PrintStatusWidget's DetailedFormatter used to be torn down here for the
    // reason described below, and no longer needs to be: its PrinterState
    // observers now carry that state's SubjectLifetime, so their guards learn the
    // subjects were deinited rather than walking freed observer nodes.

    // Tear down the PrintControlButtons singleton, which still does need it.
    // The controller persists across tests (it's a process
    // singleton) and observes the GLOBAL print_state_enum subject. A later test
    // calling PrinterStateTestAccess::reset(ps) — or process exit — deinits that
    // subject; lv_subject_deinit frees the observer node, leaving the
    // controller's ObserverGuard with a dangling lv_observer_t*. The next
    // destructor that walks it (the singleton's own static teardown at exit, or
    // the next fixture's reset()) calls lv_observer_remove() on freed memory →
    // SIGSEGV / heap corruption. This surfaced as nightly [slow] crashes:
    // a segfault in ~PrintControlButtons at process exit, plus mid-run
    // "malloc(): unaligned tcache chunk detected" aborts. Removing the observer
    // here — while print_state_enum is still alive — closes the window. No-op
    // when the controller was never initialized.
    helix::ui::PrintControlButtonsTestAccess::reset();

    // AmsState is a process singleton whose `ams_slot_count` gate subject is
    // registered in the global XML scope and driven >0 when an AMS test runs
    // slot discovery (AmsState::init_subjects(true) + an update). It never falls
    // back to 0 on its own, so it leaks a stale "AMS present" signal into later
    // tests — notably PanelWidgetConfig::build_default_grid(), whose "no AMS"
    // path assumes the subject is 0/absent (test_default_layout bed_temperature
    // failed only in the full single-process suite). The subject pointer is the
    // singleton's own member (stable for the process lifetime), so resetting the
    // value to 0 here is safe and restores the clean-slate default. No-op when
    // no AMS test has registered the subject yet.
    if (lv_subject_t* ams = lv_xml_get_subject(nullptr, "ams_slot_count")) {
        lv_subject_set_int(ams, 0);
    }

    // DisplaySettingsManager's animations_enabled is a process-global subject
    // that defaults to the platform value (true on desktop). A fixture-less
    // TEST_CASE that calls SettingsManager::init_subjects() (e.g. QIDI box lane
    // eject in test_ams_backend_qidi.cpp) cascades into
    // DisplaySettingsManager::init_subjects() and latches it true, which then
    // leaks into every later test. With animations ON, Modal exit animates over
    // MODAL_EXIT_DURATION_MS (150ms) and only removes the entry from the raw
    // stack_ vector when the animation completes — but modal tests typically
    // pump only ~10ms of LVGL time before asserting ModalStack::stack_empty(),
    // so the entry lingers and the assertion fails
    // (test_ams_edit_overlay_views.cpp). Forcing animations off here makes modal
    // teardown synchronous and removes a whole class of modal-timing flakiness.
    // Set the subject directly (not set_animations_enabled(), which also writes
    // Config) to avoid Config side effects.
    //
    // init_subjects() first, for the same reason SystemSettingsManager gets it
    // above: it is idempotent, and without it this force silently does nothing
    // whenever a previous test left the manager torn down. deinit_subjects()
    // withdraws the name from the XML registry, so the lookup finds nothing, the
    // force is skipped, and the NEXT fixture's init restores the platform
    // default (animations ON on desktop). Modal exits then animate over
    // MODAL_EXIT_DURATION_MS instead of completing synchronously, and any modal
    // test that pumps less than 150ms starts reading the OUTGOING dialog —
    // lv_obj_find_by_name() returns the stale subtree because it is still parented
    // to the screen (test_afc_fault_path_modal.cpp read a previous fault's text).
    helix::DisplaySettingsManager::instance().init_subjects();
    if (lv_subject_t* anim = lv_xml_get_subject(nullptr, "settings_animations_enabled")) {
        lv_subject_set_int(anim, 0);
    }

    // fault_surface_correlation entries live for 3s of wall clock, which spans
    // dozens of tests in a fast suite. A record left by an error-routing test
    // would silence AmsErrorBridge's fallback toast in an unrelated later one.
    helix::fault_surface_correlation::clear_for_test();

    // NOTE: NavigationManager has no public reset API (clear_overlay_stack is
    // private; shutdown() is a one-way teardown for app exit). Add a reset
    // here if/when test flakiness from leftover panel/overlay state surfaces.
    //
    // NOTE: theme_manager has no "reset to default" entry point either. If
    // tests start mutating the active theme, add a reset alongside that work.
}
