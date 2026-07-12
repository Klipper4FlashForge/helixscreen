# Plan: InFlightGuard helper (#910) + PrintSelectFileProvider generation guard (#912)

Both follow up commit `2dac15cba` (self-heal stuck `refresh_in_flight_` after 30s).

## #910 — extract `InFlightGuard`

### Prevalence (justifies the extraction)
5 classes gate an async RPC with an in-flight bool cleared only in success/error
callbacks. Only `PrintSelectPanel` has timestamp self-heal today; the rest can
wedge forever if a response is lost.

| Class | Member | Self-heal? | In scope? |
|-------|--------|-----------|-----------|
| `PrintSelectPanel` | `refresh_in_flight_` + `refresh_started_at_` + `refresh_stuck_threshold_` | YES (30s) | **YES** — replace with guard |
| `ConsolePanel` | `fetch_in_flight_` | NO (plain bool) | **YES** — gains self-heal |
| `HistoryListPanel` | `is_loading_more_` | NO (plain bool) | **YES** — gains self-heal |
| `AmsBackendAfc` | `eject_in_flight_` | NO, but has `AMS_OPERATION_TIMEOUT_MS`, mutex-guarded | **NO** — defer (own timeout, different concurrency) |
| `AmsBackendAd5xIfs` | `json_poll_in_flight_` (atomic) | NO, but has RPC timeout, atomic-coalescing | **NO** — defer |

Migrating the 2 AMS backends risks behavior change (different concurrency shape,
already time-bounded) for little gain — documented as deliberately out of scope.

### API — `include/in_flight_guard.h` (header-only, no LVGL/network deps)
```cpp
namespace helix {
class InFlightGuard {
  public:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;
    enum class AcquireResult { Acquired, RecoveredStuck, Skipped };

    explicit InFlightGuard(std::chrono::milliseconds stuck_threshold =
                               std::chrono::milliseconds(30000));

    // Acquire semantics preserve PrintSelectPanel's refresh_should_skip() logic:
    //   force            -> Acquired (bumps started_at)
    //   !active          -> Acquired
    //   active && stuck  -> RecoveredStuck (re-acquire, bump started_at)
    //   active && !stuck -> Skipped (do not proceed)
    AcquireResult try_acquire(bool force = false, TimePoint now = Clock::now());
    void release();
    bool active() const;
    bool is_stuck(TimePoint now = Clock::now()) const;
    std::chrono::milliseconds stuck_threshold() const;

  private:
    bool in_flight_ = false;
    TimePoint started_at_{};
    std::chrono::milliseconds stuck_threshold_;
};
} // namespace helix
```
`now` is injectable so unit tests drive stuck-detection deterministically (mirrors
the existing `refresh_should_skip(now)` testability decision from #911).
`RecoveredStuck` lets each caller keep its own "flag stuck for {}ms — retrying"
warning message.

### Migration
- **PrintSelectPanel** (`ui_panel_print_select.{h,cpp}`): replace the 3 members +
  the free `refresh_should_skip()` predicate with a member `InFlightGuard
  refresh_guard_{std::chrono::milliseconds(30000)}`. At cpp:956-984 the skip check
  becomes `auto r = refresh_guard_.try_acquire(force); if (r==Skipped) return; if
  (r==RecoveredStuck) spdlog::warn(...);`. Releases at cpp:470 (ok) / cpp:673 (err)
  become `refresh_guard_.release()`. Keep `refresh_should_skip` free function ONLY
  if a test still targets it — otherwise remove and re-point its test (#911) at the
  guard.
- **ConsolePanel** (`ui_panel_console.cpp`): `fetch_in_flight_` bool → guard;
  acquire at :478, release at :510/:518. Gains 30s self-heal.
- **HistoryListPanel** (`ui_panel_history_list.cpp`): `is_loading_more_` → guard;
  acquire :445, release :457/:482.

### Tests (test-first) — `tests/unit/test_in_flight_guard.cpp` `[inflight]`
- fresh guard: `try_acquire()` → Acquired, `active()` true.
- second acquire while active & not stuck → Skipped.
- `release()` → `active()` false, next acquire → Acquired.
- stuck: acquire, advance `now` past threshold → `try_acquire(false, now+31s)` →
  RecoveredStuck, started_at re-based.
- `force=true` while active → Acquired (bumps started_at) regardless of stuck.
- `is_stuck(now)` boundary (exactly threshold = not stuck; threshold+1ms = stuck).

## #912 — per-request generation guard in `PrintSelectFileProvider`

### Problem
`refresh_files(path, existing)` registers ONE `on_files_ready_` callback with no
request id. After the 30s self-heal reissues, if the original (lost) response
eventually arrives, BOTH run the merge → extra metadata pass + grid flicker.
`nav_generation_` does NOT cover this (it bumps on directory *change*, not on a
same-dir refresh reissue).

### Approach (mirror `ThumbnailLoadContext` / `GCodeViewer::load_generation_`)
Add `std::atomic<uint32_t> refresh_generation_{0}` to `PrintSelectFileProvider`.
- `refresh_files()` entry: `uint32_t gen = ++refresh_generation_;` capture into
  both `get_directory` success/error lambdas.
- Before invoking `on_files_ready_` / `on_error_`, compare
  `if (gen != refresh_generation_.load()) return;` — a superseded (stale) response
  returns early without firing the callback.
- Provider is the right owner: a second `refresh_files` bumps the generation, so the
  first response self-invalidates. Panel-side `refresh_guard_.release()` still runs
  via the live callback only.

### Test — extend `[inflight]` or a `[fileprovider]` tag
- Drive two `refresh_files()` on the provider (mock API) where the first response is
  delivered AFTER the second; assert only the second's data reaches `on_files_ready_`
  (first observes stale generation, returns early). Use the mock client's deferred
  RPC delivery. Tag `[slow]` if it spins the mock event loop.

## Files
- NEW `include/in_flight_guard.h`, `tests/unit/test_in_flight_guard.cpp`
- `include/ui_panel_print_select.h` + `src/ui/ui_panel_print_select.cpp`
- `src/ui/ui_panel_console.cpp`
- `src/ui/ui_panel_history_list.cpp`
- `include/ui_print_select_file_provider.h` + `src/ui/ui_print_select_file_provider.cpp`
- reconcile `tests/unit/test_*` that target `refresh_should_skip` (#911)

## Order
1. `InFlightGuard` header + failing unit tests → green.
2. Migrate PrintSelectPanel (behavior-preserving); reconcile #911 test.
3. Migrate ConsolePanel + HistoryListPanel.
4. #912 generation guard + test.
5. `make -j`, `make test-run`; run `[inflight]`, `[print_select]`, full suite.
6. Smoke `--test`: navigate print-select rapidly, confirm no regression.
