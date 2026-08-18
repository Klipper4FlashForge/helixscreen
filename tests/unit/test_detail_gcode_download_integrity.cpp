// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_detail_gcode_download_integrity.cpp
 * @brief Cache-poisoning guards for the detail view's shared gcode download
 *
 * Pins the final-review finding on the tools-used cache plumbing:
 *
 * A FAILED shared download must NOT persist an authoritative-empty
 * tools-used cache entry (finish_scan(authoritative=false)) — on 2D-only
 * platforms no viewer parse ever repairs it. A SUCCESSFUL scan that finds
 * zero tools (single-extruder file) MUST still persist the empty set.
 *
 * Determinism notes:
 * - MoonrakerAPIMock resolves downloads synchronously on the calling thread
 *   (asset copy, or immediate on_error for an unknown filename) — no network.
 * - HELIX_FORCE_GCODE_MEMORY_FAIL=1 forces is_gcode_2d_streaming_safe() to
 *   fail, keeping the gcode viewer's load out of these tests: they target
 *   the download/scan plumbing, not the render path.
 * - The tools scan itself runs on the real HttpExecutor slow lane; readiness
 *    is joined with wait_until() (which drains the UpdateQueue per pass).
 */

#include "ui_callback_helpers.h"
#include "ui_print_select_detail_view.h"
#include "ui_update_queue.h"

#include "../lvgl_ui_test_fixture.h"
#include "helix-xml/src/xml/lv_xml.h"
#include "moonraker_api_mock.h"
#include "moonraker_client_mock.h"
#include "printer_state.h"
#include "tools_used_cache.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "../catch_amalgamated.hpp"

namespace {

/// No-op stand-ins for the print_file_detail.xml event callbacks (normally
/// registered by PrintSelectPanel's init_subjects — the fixture doesn't init
/// that panel, but the XML references them at create() time).
void detail_noop_cb(lv_event_t* /*e*/) {}

/// Per-test temp cache dir for HELIX_CACHE_DIR — keeps ToolsUsedCache disk
/// state and the shared gcode download out of the real user cache.
/// Saves/restores the env var so later tests in this binary are unaffected.
struct CacheDirGuard {
    std::filesystem::path dir;
    std::string prev_env_;
    bool had_prev_ = false;
    CacheDirGuard()
        : dir(std::filesystem::temp_directory_path() /
              ("detail_gcode_integrity_" + std::to_string(::getpid()))) {
        std::filesystem::create_directories(dir);
        if (const char* old = ::getenv("HELIX_CACHE_DIR")) {
            prev_env_ = old;
            had_prev_ = true;
        }
        ::setenv("HELIX_CACHE_DIR", dir.c_str(), 1);
    }
    ~CacheDirGuard() {
        if (had_prev_) {
            ::setenv("HELIX_CACHE_DIR", prev_env_.c_str(), 1);
        } else {
            ::unsetenv("HELIX_CACHE_DIR");
        }
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }
};

/// Save/set/restore one environment variable (used for
/// HELIX_FORCE_GCODE_MEMORY_FAIL — read per-call by memory_utils).
struct EnvGuard {
    std::string name_;
    std::string prev_env_;
    bool had_prev_ = false;
    EnvGuard(std::string name, std::string value) : name_(std::move(name)) {
        if (const char* old = ::getenv(name_.c_str())) {
            prev_env_ = old;
            had_prev_ = true;
        }
        ::setenv(name_.c_str(), value.c_str(), 1);
    }
    ~EnvGuard() {
        if (had_prev_) {
            ::setenv(name_.c_str(), prev_env_.c_str(), 1);
        } else {
            ::unsetenv(name_.c_str());
        }
    }
};

/// Resolve a test gcode asset the way MoonrakerAPIMock does (repo root,
/// build/, build/bin/ cwd fallbacks) so the test works from any of them.
std::string find_test_asset(const std::string& filename) {
    for (const auto& prefix : {"", "../", "../../"}) {
        std::string path = std::string(prefix) + "assets/test_gcodes/" + filename;
        if (std::filesystem::exists(path)) {
            return path;
        }
    }
    return {};
}

/// LVGL UI fixture + the mock API stack the detail view talks to (mirrors
/// MoonrakerAPIMockTestFixture in test_moonraker_api_mock.cpp).
class DetailDownloadFixture : public LVGLUITestFixture {
  public:
    DetailDownloadFixture() : client_(MoonrakerClientMock::PrinterType::VORON_24) {
        state_.init_subjects(false); // no XML bindings in tests
        api_ = std::make_unique<MoonrakerAPIMock>(client_, state_);

        register_xml_callbacks({
            {"on_print_select_detail_backdrop", detail_noop_cb},
            {"on_print_select_print_button", detail_noop_cb},
            {"on_print_select_delete_button", detail_noop_cb},
            {"on_print_detail_back_clicked", detail_noop_cb},
            {"on_toggle_sliced_colors", detail_noop_cb},
        });

        view_.init_subjects();
        REQUIRE(view_.create(test_screen()) != nullptr);
        view_.set_dependencies(api_.get(), &state_);

        ready_ = lv_xml_get_subject(nullptr, "detail_mapping_ready");
        REQUIRE(ready_ != nullptr);
    }

    ~DetailDownloadFixture() override {
        helix::ui::UpdateQueue::instance().drain();
    }

    // Destruction order is reverse-declaration: the view MUST be destroyed
    // before the printer state it observes (production reaches the same
    // ordering via the PrinterState singleton outliving the view).
    MoonrakerClientMock client_;
    PrinterState state_;
    std::unique_ptr<MoonrakerAPIMock> api_;
    helix::ui::PrintSelectDetailView view_;
    lv_subject_t* ready_ = nullptr;

    /// The mock fires its transfer callbacks synchronously, but each hop
    /// through ensure_gcode_downloaded / finish_scan crosses the UpdateQueue
    /// via tok.defer — pump a few passes so the whole chain has run.
    void drain_queue_chain() {
        for (int i = 0; i < 4; ++i) {
            helix::ui::UpdateQueue::instance().drain();
        }
    }

    /// Production close flow: go_back is deferred, so drain runs
    /// on_deactivate + the destroy-on-close callback before teardown.
    void pop_and_drain() {
        view_.hide();
        helix::ui::UpdateQueue::instance().drain();
    }

    bool ready() const {
        return lv_subject_get_int(ready_) == 1;
    }
};

} // namespace

// ============================================================================
// Fix 1: transient download failure must not poison the persistent cache
// ============================================================================

TEST_CASE_METHOD(DetailDownloadFixture,
                 "failed shared download does not persist a tools-used cache entry",
                 "[print_select][detail_view][gcode_cache]") {
    CacheDirGuard guard;
    constexpr size_t kSize = 7777;
    constexpr time_t kMtime = 42;

    // Unknown filename: the mock fires on_error synchronously — the same
    // transient Moonraker failure the degrade path guards against.
    view_.show("no_such_mock_file.gcode", "", "PLA", {"#FF0000"}, {}, kSize, kMtime);
    drain_queue_chain();

    // The degrade path still resolves readiness (print gate must not hang)…
    REQUIRE(ready());

    // …but the empty set is NOT an answer about this file: nothing may be
    // persisted under its (path, size, mtime) key.
    helix::ToolsUsedCache fresh;
    REQUIRE(fresh.lookup("no_such_mock_file.gcode", kSize, kMtime) == std::nullopt);

    pop_and_drain();
}

TEST_CASE_METHOD(DetailDownloadFixture,
                 "successful scan of a zero-tool file persists the empty set",
                 "[print_select][detail_view][gcode_cache]") {
    CacheDirGuard guard;
    EnvGuard mem_fail("HELIX_FORCE_GCODE_MEMORY_FAIL", "1");

    const std::string asset = find_test_asset("3DBenchy.gcode");
    REQUIRE_FALSE(asset.empty()); // tests must run from repo/build cwd
    const auto benchy_size = static_cast<size_t>(std::filesystem::file_size(asset));
    constexpr time_t kMtime = 42;

    // 3DBenchy.gcode has no tool-change lines: a successful scan of it
    // returns an empty set — the legitimate single-extruder answer.
    view_.show("3DBenchy.gcode", "", "PLA", {"#FF0000"}, {}, benchy_size, kMtime);

    // Scan runs on the real slow lane; wait_until drains the queue each pass
    // so finish_scan's deferred body publishes readiness.
    REQUIRE(wait_until([this]() { return ready(); }, 15000));

    helix::ToolsUsedCache fresh;
    const auto got = fresh.lookup("3DBenchy.gcode", benchy_size, kMtime);
    // The empty set must be a HIT, not a miss — else every open of every
    // single-extruder file re-scans, and the fix over-corrected.
    REQUIRE(got.has_value());
    REQUIRE(got->empty());

    pop_and_drain();
}
