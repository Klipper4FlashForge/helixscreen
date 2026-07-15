# Jog Move Coalescing Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Rapid jog-wheel taps coalesce into pending distance instead of bouncing off the busy guard with error toasts; plus two crash hardenings (MotionPanel widget-pointer nulling, toast dedupe).

**Architecture:** A pure `JogCoalescer` state machine in `MotionPanel` serializes jog RPCs (one in flight, taps accumulate algebraically, flush on ack). A new `AppMotionActivity` tracker on `PrinterState` lets the discretionary-gcode busy guard distinguish self-inflicted `idle_timeout == "Printing"` (our own jog executing) from external blocking ops (calibration) via a 2s grace window. A new `MoonrakerMotionAPI::move_relative()` sends multi-axis relative moves as one script with per-axis feedrates.

**Tech Stack:** C++17, LVGL 9.5, Catch2 (`tests/unit/`, tags in brackets), pure Makefile (`make -j`, `make test-run`).

**Spec:** `docs/devel/specs/2026-07-15-jog-move-coalescing-design.md`

## Global Constraints

- Worktree: run `scripts/setup-worktree.sh feature/jog-coalescing` from repo root; all work happens in `.worktrees/feature-jog-coalescing/`.
- TDD: every task writes its failing test FIRST, runs it red, then implements.
- Before any build: `pgrep -x cc1plus >/dev/null && echo BUSY || echo FREE` — wait if another build is running.
- spdlog only (no printf/cout). SPDX header on new files: `// SPDX-License-Identifier: GPL-3.0-or-later`.
- Threading: WebSocket callbacks NEVER touch LVGL/members directly — `lifetime_.bg_cb(tag, fn)` only. NEVER `if (tok.expired()) return;` on a bg thread (L081 — lint gate `scripts/check_l081_anti_pattern.py` blocks it at commit).
- No test-only public methods on production classes — use `friend class FooTestAccess;` (L065).
- Grace window constant: `std::chrono::seconds(2)`. XY jog feedrate 6000 mm/min, Z 600 mm/min (existing values).
- Commit messages: `feat(motion): …` / `fix(ui): …` style, no `--no-verify`, let clang-format hook fix formatting.

---

### Task 1: JogCoalescer + clamp_jog_delta (pure logic)

**Files:**
- Create: `include/jog_coalescer.h`
- Test: `tests/unit/test_jog_coalescer.cpp`
- Modify: `mk/tests.mk` only if test sources aren't glob-discovered (check `grep -n "test_.*\.cpp\|wildcard" mk/tests.mk` first — if tests are globbed, no change needed).

**Interfaces:**
- Consumes: nothing (header-only, no deps beyond `<optional>`, `<algorithm>`).
- Produces (Task 4 relies on these exact names):
  - `helix::AxisMove { double dx, dy, dz; bool any() const; }`
  - `std::optional<helix::AxisMove> JogCoalescer::on_tap(const AxisMove&)`
  - `std::optional<helix::AxisMove> JogCoalescer::on_ack()`
  - `void JogCoalescer::on_error()`, `void JogCoalescer::reset()`
  - `bool JogCoalescer::in_flight() const`
  - `double JogCoalescer::uncommitted_x() const` (same for `_y`, `_z`)
  - `double helix::clamp_jog_delta(double current, double uncommitted, double delta, double min, double max)`

- [ ] **Step 1: Write the failing test**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
// tests/unit/test_jog_coalescer.cpp
#include "jog_coalescer.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using helix::AxisMove;
using helix::JogCoalescer;

TEST_CASE("JogCoalescer: first tap sends immediately", "[jog_coalescer]") {
    JogCoalescer c;
    auto send = c.on_tap({1.0, 0.0, 0.0});
    REQUIRE(send.has_value());
    CHECK(send->dx == 1.0);
    CHECK(c.in_flight());
    CHECK(c.uncommitted_x() == 1.0); // in-flight counts as uncommitted
}

TEST_CASE("JogCoalescer: taps while in flight accumulate, ack flushes once",
          "[jog_coalescer]") {
    JogCoalescer c;
    REQUIRE(c.on_tap({1.0, 0.0, 0.0}).has_value());
    CHECK_FALSE(c.on_tap({1.0, 0.0, 0.0}).has_value());
    CHECK_FALSE(c.on_tap({1.0, 0.0, 0.0}).has_value());
    CHECK(c.uncommitted_x() == 3.0); // 1 in flight + 2 pending

    auto flush = c.on_ack();
    REQUIRE(flush.has_value());
    CHECK(flush->dx == 2.0); // both pending taps in ONE move
    CHECK(c.in_flight());

    auto done = c.on_ack();
    CHECK_FALSE(done.has_value()); // nothing pending -> idle
    CHECK_FALSE(c.in_flight());
    CHECK(c.uncommitted_x() == 0.0);
}

TEST_CASE("JogCoalescer: reversal cancels pending algebraically", "[jog_coalescer]") {
    JogCoalescer c;
    REQUIRE(c.on_tap({1.0, 0.0, 0.0}).has_value());
    c.on_tap({1.0, 0.0, 0.0});
    c.on_tap({-1.0, 0.0, 0.0});
    auto flush = c.on_ack();
    CHECK_FALSE(flush.has_value()); // +1 -1 pending nets to zero -> nothing to send
    CHECK_FALSE(c.in_flight());
}

TEST_CASE("JogCoalescer: multi-axis pending flushes as one move", "[jog_coalescer]") {
    JogCoalescer c;
    REQUIRE(c.on_tap({1.0, 0.0, 0.0}).has_value());
    c.on_tap({0.0, -2.0, 0.0});
    c.on_tap({0.0, 0.0, 0.5});
    auto flush = c.on_ack();
    REQUIRE(flush.has_value());
    CHECK(flush->dx == 0.0);
    CHECK(flush->dy == -2.0);
    CHECK(flush->dz == 0.5);
}

TEST_CASE("JogCoalescer: error drops pending and goes idle", "[jog_coalescer]") {
    JogCoalescer c;
    REQUIRE(c.on_tap({1.0, 0.0, 0.0}).has_value());
    c.on_tap({5.0, 0.0, 0.0});
    c.on_error();
    CHECK_FALSE(c.in_flight());
    CHECK(c.uncommitted_x() == 0.0);
    // Next tap sends immediately again
    CHECK(c.on_tap({1.0, 0.0, 0.0}).has_value());
}

TEST_CASE("JogCoalescer: reset clears everything", "[jog_coalescer]") {
    JogCoalescer c;
    c.on_tap({1.0, 0.0, 0.0});
    c.on_tap({2.0, 0.0, 0.0});
    c.reset();
    CHECK_FALSE(c.in_flight());
    CHECK(c.uncommitted_x() == 0.0);
}

TEST_CASE("clamp_jog_delta: clamps target to envelope", "[jog_coalescer]") {
    // current=195, nothing uncommitted, +10 would hit 205 with max 200 -> +5
    CHECK_THAT(helix::clamp_jog_delta(195.0, 0.0, 10.0, 0.0, 200.0),
               Catch::Matchers::WithinAbs(5.0, 1e-9));
    // Accounts for uncommitted travel: current=190, 5 uncommitted, +10 -> +5
    CHECK_THAT(helix::clamp_jog_delta(190.0, 5.0, 10.0, 0.0, 200.0),
               Catch::Matchers::WithinAbs(5.0, 1e-9));
    // Fully at edge -> 0
    CHECK(helix::clamp_jog_delta(200.0, 0.0, 1.0, 0.0, 200.0) == 0.0);
    // Never reverses direction even if predicted overshoots the envelope
    CHECK(helix::clamp_jog_delta(205.0, 0.0, 1.0, 0.0, 200.0) == 0.0);
    // Moves away from the edge pass through untouched
    CHECK_THAT(helix::clamp_jog_delta(200.0, 0.0, -1.0, 0.0, 200.0),
               Catch::Matchers::WithinAbs(-1.0, 1e-9));
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `make test 2>&1 | tail -5` — expect compile FAILURE (`jog_coalescer.h: No such file`).

- [ ] **Step 3: Write the implementation**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
// include/jog_coalescer.h
#pragma once

#include <algorithm>
#include <optional>

namespace helix {

/** Relative multi-axis jog delta in mm. */
struct AxisMove {
    double dx = 0.0;
    double dy = 0.0;
    double dz = 0.0;
    bool any() const {
        return dx != 0.0 || dy != 0.0 || dz != 0.0;
    }
};

/**
 * Serializes jog moves: one RPC in flight, further taps accumulate
 * algebraically into pending deltas and flush as ONE combined move when the
 * in-flight move acks. Main-thread only — callers marshal acks/errors onto
 * the UI thread before touching this.
 */
class JogCoalescer {
  public:
    /** Tap arrived. Returns the move to send NOW if idle; nullopt if it was
     *  accumulated behind the in-flight move. */
    std::optional<AxisMove> on_tap(const AxisMove& delta) {
        if (in_flight_) {
            pending_.dx += delta.dx;
            pending_.dy += delta.dy;
            pending_.dz += delta.dz;
            return std::nullopt;
        }
        in_flight_ = true;
        inflight_ = delta;
        return delta;
    }

    /** In-flight move acked. Returns the pending flush to send (stays in
     *  flight) or nullopt (now idle). */
    std::optional<AxisMove> on_ack() {
        if (pending_.any()) {
            inflight_ = pending_;
            pending_ = {};
            return inflight_;
        }
        in_flight_ = false;
        inflight_ = {};
        return std::nullopt;
    }

    /** In-flight move failed: drop pending, go idle. */
    void on_error() {
        reset();
    }

    /** Drop all state (panel deactivate, print start, UI teardown). */
    void reset() {
        in_flight_ = false;
        inflight_ = {};
        pending_ = {};
    }

    bool in_flight() const {
        return in_flight_;
    }

    /** Travel not yet reflected in the position subjects: in-flight + pending.
     *  Used to predict position for envelope clamping. */
    double uncommitted_x() const {
        return inflight_.dx + pending_.dx;
    }
    double uncommitted_y() const {
        return inflight_.dy + pending_.dy;
    }
    double uncommitted_z() const {
        return inflight_.dz + pending_.dz;
    }

  private:
    bool in_flight_ = false;
    AxisMove inflight_{};
    AxisMove pending_{};
};

/**
 * Clamp a jog delta so predicted-position + delta stays inside [min, max].
 * Returns 0 rather than a direction-reversing correction when the predicted
 * position is already at/past the edge in the tap direction.
 */
inline double clamp_jog_delta(double current, double uncommitted, double delta, double min,
                              double max) {
    const double predicted = current + uncommitted;
    const double clamped = std::clamp(predicted + delta, min, max) - predicted;
    if (clamped * delta <= 0.0) {
        return 0.0;
    }
    return clamped;
}

} // namespace helix
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `make test-run 2>&1 | tail -15` then `./build/bin/helix-tests "[jog_coalescer]"` — expect all PASS.

- [ ] **Step 5: Commit**

```bash
git add include/jog_coalescer.h tests/unit/test_jog_coalescer.cpp
git commit -m "feat(motion): add JogCoalescer state machine and clamp_jog_delta helper"
```

---

### Task 2: AppMotionActivity + busy-guard attribution

**Files:**
- Create: `include/app_motion_activity.h`
- Modify: `include/printer_state.h` (member + 2 method decls near `is_blocking_operation_active()` at :1698)
- Modify: `src/printer/printer_state.cpp:747-767` (add `is_external_blocking_operation_active()` after the existing method)
- Modify: `src/api/moonraker_motion_api.cpp:368` (guard swap)
- Modify: `src/api/moonraker_api_controls.cpp:479` (guard swap)
- Test: create `tests/unit/test_app_motion_activity.cpp`; extend `tests/unit/test_printer_state_blocking_op.cpp` and `tests/unit/test_moonraker_api_busy_guard.cpp`

**Interfaces:**
- Consumes: `PrinterCalibrationState::get_manual_probe_active_subject()`, existing `is_blocking_operation_active()`.
- Produces (Task 3 stamps these; guards call the predicate):
  - `helix::AppMotionActivity` with `void note_sent()`, `void note_done(clock::time_point now = clock::now())`, `bool recently_active(clock::time_point now = clock::now()) const`, `static constexpr std::chrono::seconds kGraceWindow{2}`
  - `helix::AppMotionActivity& PrinterState::app_motion_activity()`
  - `bool PrinterState::is_external_blocking_operation_active()`

- [ ] **Step 1: Write the failing tests**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
// tests/unit/test_app_motion_activity.cpp
#include "app_motion_activity.h"

#include <catch2/catch_test_macros.hpp>

using helix::AppMotionActivity;
using clock_t_ = AppMotionActivity::clock;

TEST_CASE("AppMotionActivity: idle by default", "[motion][busy_guard]") {
    AppMotionActivity a;
    CHECK_FALSE(a.recently_active());
}

TEST_CASE("AppMotionActivity: active while a send is outstanding", "[motion][busy_guard]") {
    AppMotionActivity a;
    a.note_sent();
    CHECK(a.recently_active());
}

TEST_CASE("AppMotionActivity: grace window after ack, then expires", "[motion][busy_guard]") {
    AppMotionActivity a;
    const auto t0 = clock_t_::now();
    a.note_sent();
    a.note_done(t0);
    CHECK(a.recently_active(t0 + std::chrono::milliseconds(500)));
    CHECK(a.recently_active(t0 + std::chrono::milliseconds(1999)));
    CHECK_FALSE(a.recently_active(t0 + std::chrono::milliseconds(2001)));
}

TEST_CASE("AppMotionActivity: overlapping sends stay active until last ack",
          "[motion][busy_guard]") {
    AppMotionActivity a;
    const auto t0 = clock_t_::now();
    a.note_sent();
    a.note_sent();
    a.note_done(t0);
    CHECK(a.recently_active(t0 + std::chrono::hours(1))); // one still outstanding
    a.note_done(t0 + std::chrono::hours(1));
    CHECK_FALSE(a.recently_active(t0 + std::chrono::hours(2)));
}

TEST_CASE("AppMotionActivity: unbalanced note_done clamps at zero", "[motion][busy_guard]") {
    AppMotionActivity a;
    const auto t0 = clock_t_::now();
    a.note_done(t0); // defensive: never sent
    CHECK_FALSE(a.recently_active(t0 + std::chrono::seconds(3)));
    a.note_sent();
    CHECK(a.recently_active(t0 + std::chrono::hours(1)));
}
```

Extend `tests/unit/test_printer_state_blocking_op.cpp` — read the existing file first and follow its fixture/TestAccess pattern exactly (it drives `idle_timeout_printing_` and print state; sections at :64-105). Add this test case using the same setup helpers the existing sections use:

```cpp
TEST_CASE_METHOD(/* same fixture as existing cases in this file */,
                 "is_external_blocking_operation_active attributes self-busy",
                 "[printer_state][busy_guard]") {
    // Arrange exactly like the existing "idle_timeout Printing + no file print
    // -> blocking" section: idle_timeout_printing = 1, print job state STANDBY.

    SECTION("external busy: no app motion -> blocked") {
        CHECK(state.is_blocking_operation_active());
        CHECK(state.is_external_blocking_operation_active());
    }

    SECTION("self busy: app motion in flight -> not blocked") {
        state.app_motion_activity().note_sent();
        CHECK(state.is_blocking_operation_active());          // raw predicate unchanged
        CHECK_FALSE(state.is_external_blocking_operation_active());
        state.app_motion_activity().note_done();
    }

    SECTION("manual probe blocks even during app motion") {
        // Set manual_probe_active = 1 via the same mechanism existing sections
        // use for calibration state subjects.
        state.app_motion_activity().note_sent();
        CHECK(state.is_external_blocking_operation_active());
        state.app_motion_activity().note_done();
    }
}
```

Extend `tests/unit/test_moonraker_api_busy_guard.cpp` — read it first; it exercises the "Printer is busy" refusal. Add one case: with the same busy arrangement that currently triggers refusal, call `state.app_motion_activity().note_sent()` first and assert the jog gcode goes through (no NOT_READY error). Follow the file's existing mock/callback pattern verbatim.

- [ ] **Step 2: Run to verify failures**

Run: `make test 2>&1 | tail -5` — expect compile FAILURE (`app_motion_activity.h` missing, methods undeclared).

- [ ] **Step 3: Implement**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
// include/app_motion_activity.h
#pragma once

#include <atomic>
#include <chrono>

namespace helix {

/**
 * Tracks app-initiated motion (jog) RPC activity so the discretionary-gcode
 * busy guard can tell self-inflicted busy (idle_timeout == "Printing" because
 * OUR jog is executing) from an external blocking op (calibration, console
 * gcode from another UI). Thread-safe: sends stamp from the main thread,
 * acks/errors from the websocket thread.
 *
 * Invariant: MoonrakerMotionAPI wraps BOTH the success and error callback of
 * every stamped send, and the request tracker guarantees one of them fires
 * (including on timeout) — so inflight_ cannot leak upward permanently.
 */
class AppMotionActivity {
  public:
    using clock = std::chrono::steady_clock;
    static constexpr std::chrono::seconds kGraceWindow{2};

    void note_sent() {
        inflight_.fetch_add(1, std::memory_order_relaxed);
    }

    void note_done(clock::time_point now = clock::now()) {
        last_done_ns_.store(now.time_since_epoch().count(), std::memory_order_relaxed);
        // Clamp at zero if a done arrives without a matching send.
        int prev = inflight_.fetch_sub(1, std::memory_order_relaxed);
        if (prev <= 0) {
            inflight_.store(0, std::memory_order_relaxed);
        }
    }

    bool recently_active(clock::time_point now = clock::now()) const {
        if (inflight_.load(std::memory_order_relaxed) > 0) {
            return true;
        }
        const auto last_ns = last_done_ns_.load(std::memory_order_relaxed);
        if (last_ns == 0) {
            return false;
        }
        const clock::time_point last{clock::duration{last_ns}};
        return (now - last) < kGraceWindow;
    }

  private:
    std::atomic<int> inflight_{0};
    std::atomic<long long> last_done_ns_{0};
};

} // namespace helix
```

`include/printer_state.h` — next to the `is_blocking_operation_active()` declaration (:1698):

```cpp
    /**
     * Like is_blocking_operation_active(), but treats busy-ness attributable
     * to the app's own recent jog activity (in flight, or acked within
     * AppMotionActivity::kGraceWindow) as NOT blocking. Manual probe remains
     * an absolute block. Guards for discretionary gcode use THIS predicate so
     * back-to-back jogs don't self-block (idle_timeout reports "Printing"
     * during any move, including our own jog — spec 2026-07-15).
     */
    bool is_external_blocking_operation_active();

    helix::AppMotionActivity& app_motion_activity() {
        return app_motion_activity_;
    }
```

plus `#include "app_motion_activity.h"` and a private member `helix::AppMotionActivity app_motion_activity_;`.

`src/printer/printer_state.cpp` — after `is_blocking_operation_active()` (:767):

```cpp
bool PrinterState::is_external_blocking_operation_active() {
    // Manual probe is an absolute block: TESTZ sessions must never accept
    // jog gcode regardless of how recently the app itself sent motion.
    if (lv_subject_get_int(calibration_state_.get_manual_probe_active_subject()) != 0) {
        return true;
    }
    if (!is_blocking_operation_active()) {
        return false;
    }
    return !app_motion_activity_.recently_active();
}
```

Guard swaps — `src/api/moonraker_motion_api.cpp:368` and `src/api/moonraker_api_controls.cpp:479`, both become:

```cpp
    if (helix::is_discretionary_gcode(gcode) && state_.is_external_blocking_operation_active()) {
```

Update the comment above each guard to mention the attribution (self-busy from our own jog passes; external ops still refuse).

- [ ] **Step 4: Run tests**

Run: `make test-run 2>&1 | tail -15`; then `./build/bin/helix-tests "[busy_guard]"` — expect PASS, including the pre-existing truth-table sections (unchanged behavior for `is_blocking_operation_active`).

- [ ] **Step 5: Commit**

```bash
git add include/app_motion_activity.h include/printer_state.h src/printer/printer_state.cpp src/api/moonraker_motion_api.cpp src/api/moonraker_api_controls.cpp tests/unit/test_app_motion_activity.cpp tests/unit/test_printer_state_blocking_op.cpp tests/unit/test_moonraker_api_busy_guard.cpp
git commit -m "feat(motion): attribute self-inflicted busy so jogs don't self-block (busy-guard attribution)"
```

---

### Task 3: MoonrakerMotionAPI::move_relative + activity stamping

**Files:**
- Modify: `include/moonraker_motion_api.h` (declare `move_relative` near `move_axis` at :86; declare `generate_relative_move_gcode` as a **public static** method so tests hit it directly)
- Modify: `src/api/moonraker_motion_api.cpp` (implement both; stamp activity in `execute_gcode`)
- Test: create `tests/unit/test_move_relative_gcode.cpp`

**Interfaces:**
- Consumes: `PrinterState::app_motion_activity()` (Task 2), existing `execute_gcode`, `is_safe_distance`, `is_safe_feedrate`, `reject_non_finite`.
- Produces (Task 4 calls this exact signature):
  - `void MoonrakerMotionAPI::move_relative(double dx, double dy, double dz, double xy_feedrate, double z_feedrate, SuccessCallback on_success, ErrorCallback on_error)`
  - `static std::string MoonrakerMotionAPI::generate_relative_move_gcode(double dx, double dy, double dz, double xy_feedrate, double z_feedrate)`

- [ ] **Step 1: Write the failing test**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
// tests/unit/test_move_relative_gcode.cpp
#include "moonraker_motion_api.h"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("generate_relative_move_gcode: XY combined on one G0 line", "[motion][gcode]") {
    CHECK(MoonrakerMotionAPI::generate_relative_move_gcode(4.0, -2.0, 0.0, 6000.0, 600.0) ==
          "G91\nG0 X4 Y-2 F6000\nG90");
}

TEST_CASE("generate_relative_move_gcode: Z gets its own feedrate line", "[motion][gcode]") {
    CHECK(MoonrakerMotionAPI::generate_relative_move_gcode(0.0, 0.0, 0.5, 6000.0, 600.0) ==
          "G91\nG0 Z0.5 F600\nG90");
}

TEST_CASE("generate_relative_move_gcode: XY and Z as two moves in one script",
          "[motion][gcode]") {
    CHECK(MoonrakerMotionAPI::generate_relative_move_gcode(1.0, 0.0, -0.5, 6000.0, 600.0) ==
          "G91\nG0 X1 F6000\nG0 Z-0.5 F600\nG90");
}

TEST_CASE("generate_relative_move_gcode: all-zero deltas produce empty script",
          "[motion][gcode]") {
    CHECK(MoonrakerMotionAPI::generate_relative_move_gcode(0.0, 0.0, 0.0, 6000.0, 600.0).empty());
}

TEST_CASE("generate_relative_move_gcode: NaN/Inf rejected", "[motion][gcode]") {
    CHECK(MoonrakerMotionAPI::generate_relative_move_gcode(
              std::numeric_limits<double>::quiet_NaN(), 0.0, 0.0, 6000.0, 600.0)
              .empty());
    CHECK(MoonrakerMotionAPI::generate_relative_move_gcode(
              0.0, std::numeric_limits<double>::infinity(), 0.0, 6000.0, 600.0)
              .empty());
}
```

Note on number formatting: the expected strings above assume `std::ostringstream` default formatting like the existing `generate_move_gcode` (`G0 X4`, not `X4.000000`). If a literal in the test disagrees with ostream output for doubles (e.g. `0.5`), fix the TEST to match actual `<<` behavior — do not add manual formatting the existing generators don't have.

- [ ] **Step 2: Run to verify failure**

Run: `make test 2>&1 | tail -5` — expect compile FAILURE (`generate_relative_move_gcode` not a member).

- [ ] **Step 3: Implement**

In `src/api/moonraker_motion_api.cpp`, next to `generate_move_gcode` (:267):

```cpp
std::string MoonrakerMotionAPI::generate_relative_move_gcode(double dx, double dy, double dz,
                                                             double xy_feedrate,
                                                             double z_feedrate) {
    const double vals[] = {dx, dy, dz, xy_feedrate, z_feedrate};
    for (double v : vals) {
        if (std::isnan(v) || std::isinf(v)) {
            spdlog::warn("[Motion API] generate_relative_move_gcode: Rejecting G-code "
                         "generation: invalid value (NaN/Inf)");
            return "";
        }
    }
    if (dx == 0.0 && dy == 0.0 && dz == 0.0) {
        return "";
    }

    std::ostringstream gcode;
    gcode << "G91";
    if (dx != 0.0 || dy != 0.0) {
        gcode << "\nG0";
        if (dx != 0.0) {
            gcode << " X" << dx;
        }
        if (dy != 0.0) {
            gcode << " Y" << dy;
        }
        if (xy_feedrate > 0) {
            gcode << " F" << xy_feedrate;
        }
    }
    if (dz != 0.0) {
        gcode << "\nG0 Z" << dz;
        if (z_feedrate > 0) {
            gcode << " F" << z_feedrate;
        }
    }
    gcode << "\nG90";
    return gcode.str();
}

void MoonrakerMotionAPI::move_relative(double dx, double dy, double dz, double xy_feedrate,
                                       double z_feedrate, SuccessCallback on_success,
                                       ErrorCallback on_error) {
    if (reject_non_finite({dx, dy, dz, xy_feedrate, z_feedrate}, "move_relative", on_error)) {
        return;
    }

    // Per-axis distance safety (same limits as move_axis).
    const struct {
        char axis;
        double dist;
    } deltas[] = {{'X', dx}, {'Y', dy}, {'Z', dz}};
    for (const auto& d : deltas) {
        if (d.dist != 0.0 && !is_safe_distance(d.dist, safety_limits_)) {
            NOTIFY_ERROR("Move distance {:.1f}mm is too large. Maximum: {:.1f}mm.",
                         std::abs(d.dist), safety_limits_.max_relative_distance_mm);
            if (on_error) {
                MoonrakerError err;
                err.type = MoonrakerErrorType::VALIDATION_ERROR;
                err.message = "Distance " + std::to_string(d.dist) +
                              "mm exceeds safety limits on axis " + std::string(1, d.axis);
                err.method = "move_relative";
                on_error(err);
            }
            return;
        }
    }
    for (double f : {xy_feedrate, z_feedrate}) {
        if (f != 0 && !is_safe_feedrate(f, safety_limits_)) {
            NOTIFY_ERROR("Speed {:.0f}mm/min is too fast. Maximum: {:.0f}mm/min.", f,
                         safety_limits_.max_feedrate_mm_min);
            if (on_error) {
                MoonrakerError err;
                err.type = MoonrakerErrorType::VALIDATION_ERROR;
                err.message = "Feedrate " + std::to_string(f) + "mm/min exceeds safety limits";
                err.method = "move_relative";
                on_error(err);
            }
            return;
        }
    }

    std::string gcode = generate_relative_move_gcode(dx, dy, dz, xy_feedrate, z_feedrate);
    if (gcode.empty()) {
        if (on_success) {
            on_success(); // nothing to do — treat as trivially complete
        }
        return;
    }
    spdlog::info("[Motion API] Relative move dx={} dy={} dz={} (G-code: {})", dx, dy, dz,
                 gcode);
    execute_gcode(gcode, on_success, on_error);
}
```

Activity stamping — in `MoonrakerMotionAPI::execute_gcode` (`moonraker_motion_api.cpp:384-395`), replace the tail of the function (after the guards) with:

```cpp
    std::string annotated = annotate_gcode(gcode);
    json params = {{"script", annotated}};

    spdlog::trace("[Motion API] Executing G-code: {}", annotated);

    // Stamp app-initiated motion activity for discretionary (jog) gcode so the
    // busy guard can attribute the resulting idle_timeout "Printing" to us.
    // Both callbacks are wrapped: the request tracker guarantees exactly one
    // fires (success, error, or timeout), keeping the inflight count balanced.
    // NOTE: `silent` was computed from the CALLER's on_error before wrapping.
    const bool stamp = helix::is_discretionary_gcode(gcode);
    PrinterState* ps = &state_;
    if (stamp) {
        ps->app_motion_activity().note_sent();
    }

    std::function<void(const json&)> success_wrapper;
    if (on_success || stamp) {
        success_wrapper = [on_success, ps, stamp](json) {
            if (stamp) {
                ps->app_motion_activity().note_done();
            }
            if (on_success) {
                on_success();
            }
        };
    }
    ErrorCallback error_wrapper = on_error;
    if (stamp) {
        error_wrapper = [on_error, ps](const MoonrakerError& err) {
            ps->app_motion_activity().note_done();
            if (on_error) {
                on_error(err);
            }
        };
    }
    client_.send_jsonrpc("printer.gcode.script", params, std::move(success_wrapper),
                         std::move(error_wrapper), timeout_ms, silent);
}
```

(These wrappers fire on the websocket thread; `AppMotionActivity` is atomics-only, so no `ui_queue_update` needed HERE. Callers' own callbacks keep their existing threading obligations.)

Header decls in `include/moonraker_motion_api.h` next to `move_axis` (:86):

```cpp
    /**
     * Relative multi-axis move as ONE gcode script: XY combined on a single G0
     * (true diagonal) at xy_feedrate, Z on its own G0 at z_feedrate. Used by
     * the jog coalescer flush path. Zero deltas -> on_success immediately.
     */
    void move_relative(double dx, double dy, double dz, double xy_feedrate, double z_feedrate,
                       SuccessCallback on_success, ErrorCallback on_error);

    /** Public static for direct unit testing (mirrors generate_move_gcode). */
    static std::string generate_relative_move_gcode(double dx, double dy, double dz,
                                                    double xy_feedrate, double z_feedrate);
```

- [ ] **Step 4: Run tests**

Run: `make test-run 2>&1 | tail -15`; `./build/bin/helix-tests "[gcode]"` and `./build/bin/helix-tests "[busy_guard]"` — PASS. Also re-run `"[motion]"` to catch regressions in ready-guard tests.

- [ ] **Step 5: Commit**

```bash
git add include/moonraker_motion_api.h src/api/moonraker_motion_api.cpp tests/unit/test_move_relative_gcode.cpp
git commit -m "feat(motion): move_relative multi-axis script + app-motion activity stamping"
```

---

### Task 4: MotionPanel integration (coalescer wiring)

**Files:**
- Modify: `include/ui_panel_motion.h` (members + method decls)
- Modify: `src/ui/ui_panel_motion.cpp` (`jog()` :620-711, `handle_z_button()` :563-574, `on_deactivate()` :304, new `send_jog_move`/`dispatch_jog`)

**Interfaces:**
- Consumes: `helix::JogCoalescer`, `helix::AxisMove`, `helix::clamp_jog_delta` (Task 1); `move_relative` (Task 3); `lifetime_.bg_cb` from `OverlayBase` (`include/async_lifetime_guard.h:287`).
- Produces: no new external interface; behavior change only.

There is no practical unit test for the panel itself (process-lifetime global singleton wired to XML); the logic lives in Task 1's tested units. Verification for this task is compile + full test suite + the manual mock-printer session in Task 7. Keep ALL new logic in the thin shapes below — no logic that isn't already covered by Task 1/3 tests.

- [ ] **Step 1: Header changes**

In `include/ui_panel_motion.h`: add `#include "jog_coalescer.h"`; in the private section next to `jog_pad_`:

```cpp
    helix::JogCoalescer jog_coalescer_;
    bool x_edge_warned_ = false; // dedupe "blocked at bed edge" warnings
    bool y_edge_warned_ = false;

    // Route a tap/flush through the coalescer and send if idle.
    void dispatch_jog(const helix::AxisMove& delta);
    // Send one relative move; ack/error callbacks re-enter the coalescer.
    void send_jog_move(const helix::AxisMove& move);
```

and in the OverlayBase-interface section:

```cpp
    void on_ui_destroyed() override; // implemented in Task 5
```

(Declare it here but implement in Task 5 — or if the linker complains, add the Task 5 body now and skip re-declaring later; either way the final tree matches Task 5.) Simplest: declare AND implement it in Task 5 only; skip it in this task.

- [ ] **Step 2: Rewrite the send path in `src/ui/ui_panel_motion.cpp`**

Replace `MotionPanel::jog()` body from the soft-stop section (:661) to the end (:711) with:

```cpp
    // Soft-stop: clamp against the PREDICTED position (current + uncommitted
    // coalescer travel) so queued taps can't walk past the envelope. Skip when
    // bounds aren't known yet (fresh connect) or the axis isn't homed.
    helix::AxisBounds bounds = get_printer_state().get_axis_bounds();
    const char* homed_axes = lv_subject_get_string(get_printer_state().get_homed_axes_subject());
    bool x_homed = homed_axes && strchr(homed_axes, 'x') != nullptr;
    bool y_homed = homed_axes && strchr(homed_axes, 'y') != nullptr;

    double ddx = static_cast<double>(dx);
    double ddy = static_cast<double>(dy);

    if (ddx != 0.0 && bounds.has_x && x_homed) {
        ddx = helix::clamp_jog_delta(current_x_, jog_coalescer_.uncommitted_x(), ddx,
                                     bounds.x_min, bounds.x_max);
        if (ddx == 0.0) {
            if (!x_edge_warned_) {
                NOTIFY_WARNING(lv_tr("X jog blocked at bed edge"));
                x_edge_warned_ = true;
            }
        } else {
            x_edge_warned_ = false;
        }
    }
    if (ddy != 0.0 && bounds.has_y && y_homed) {
        ddy = helix::clamp_jog_delta(current_y_, jog_coalescer_.uncommitted_y(), ddy,
                                     bounds.y_min, bounds.y_max);
        if (ddy == 0.0) {
            if (!y_edge_warned_) {
                NOTIFY_WARNING(lv_tr("Y jog blocked at bed edge"));
                y_edge_warned_ = true;
            }
        } else {
            y_edge_warned_ = false;
        }
    }

    if (ddx == 0.0 && ddy == 0.0) {
        return;
    }
    dispatch_jog({ddx, ddy, 0.0});
}

void MotionPanel::dispatch_jog(const helix::AxisMove& delta) {
    if (auto immediate = jog_coalescer_.on_tap(delta)) {
        send_jog_move(*immediate);
    } else {
        spdlog::debug("[{}] Jog coalesced: pending x={:+.2f} y={:+.2f} z={:+.2f}", get_name(),
                      jog_coalescer_.uncommitted_x(), jog_coalescer_.uncommitted_y(),
                      jog_coalescer_.uncommitted_z());
    }
}

void MotionPanel::send_jog_move(const helix::AxisMove& move) {
    MoonrakerAPI* api = get_moonraker_api();
    if (!api) {
        jog_coalescer_.on_error();
        return;
    }
    // XY: 6000 mm/min (100 mm/s); Z: 600 mm/min (10 mm/s) — same as before.
    constexpr double JOG_FEEDRATE = 6000.0;
    constexpr double Z_FEEDRATE = 600.0;

    api->motion().move_relative(
        move.dx, move.dy, move.dz, JOG_FEEDRATE, Z_FEEDRATE,
        lifetime_.bg_cb("MotionPanel::on_jog_ack",
                        [this]() {
                            if (auto flush = jog_coalescer_.on_ack()) {
                                send_jog_move(*flush);
                            }
                        }),
        lifetime_.bg_cb("MotionPanel::on_jog_error", [this](const MoonrakerError& err) {
            jog_coalescer_.on_error();
            NOTIFY_ERROR(lv_tr("Jog failed: {}"), clean_gcode_error(err.user_message()));
        }));
}
```

- [ ] **Step 3: Route Z buttons through the coalescer**

In `handle_z_button()` replace the `api->motion().move_axis('Z', ...)` block (:563-574) with:

```cpp
    dispatch_jog({0.0, 0.0, distance});
```

(The `MoonrakerAPI* api` null-check moves into `send_jog_move`; delete the now-unused local.)

- [ ] **Step 4: Reset on deactivate**

`MotionPanel::on_deactivate()` (:304) — before the base call:

```cpp
    // OverlayBase::on_deactivate() invalidates lifetime_, dropping any
    // in-flight ack callback — fully reset the coalescer so it can't get
    // stuck in_flight forever, and re-arm the edge warnings.
    jog_coalescer_.reset();
    x_edge_warned_ = false;
    y_edge_warned_ = false;
```

- [ ] **Step 5: Build + full test suite**

Run: `make -j 2>&1 | tail -5` (binary), then `make test-run 2>&1 | tail -15` — expect zero failures. The L081 lint gate runs at commit; the `bg_cb` pattern above is the sanctioned short-form.

- [ ] **Step 6: Commit**

```bash
git add include/ui_panel_motion.h src/ui/ui_panel_motion.cpp
git commit -m "feat(motion): coalesce rapid jog taps into pending moves (prestonbrown/helixscreen jog-rate complaints)"
```

---

### Task 5: MotionPanel::on_ui_destroyed hardening

**Files:**
- Modify: `include/ui_panel_motion.h` (declaration, if not added in Task 4)
- Modify: `src/ui/ui_panel_motion.cpp` (implementation + null guard)

**Interfaces:**
- Consumes: `OverlayBase::on_ui_destroyed()` virtual hook (`include/overlay_base.h:294`).
- Produces: nothing external.

- [ ] **Step 1: Implement the override**

In the header (OverlayBase interface section): `void on_ui_destroyed() override;`

In the cpp, next to `on_deactivate()`:

```cpp
void MotionPanel::on_ui_destroyed() {
    // Null raw child pointers so persistent observers (jog_ready_observer_
    // dereferences jog_pad_ on every connection/klippy flap) can't UAF if the
    // overlay widget tree is ever destroyed (destroy-on-close, parent screen
    // teardown). Peers do the same — see BedMeshPanel::on_ui_destroyed.
    jog_pad_ = nullptr;
    parent_screen_ = nullptr;
    jog_coalescer_.reset();
}
```

And add the null guard at the top of `update_jog_pad_enabled()` (:469):

```cpp
    if (!jog_pad_) {
        return;
    }
```

- [ ] **Step 2: Build + tests**

Run: `make -j 2>&1 | tail -3` and `make test-run 2>&1 | tail -10` — PASS.

- [ ] **Step 3: Commit**

```bash
git add include/ui_panel_motion.h src/ui/ui_panel_motion.cpp
git commit -m "fix(motion): null jog_pad_ in on_ui_destroyed; guard observer deref (latent UAF)"
```

---

### Task 6: Toast dedupe

**Files:**
- Modify: `include/ui_toast_manager.h` (ToastInstance fields, `refresh_duplicate` decl, `friend class ToastManagerTestAccess;`)
- Modify: `src/ui/ui_toast_manager.cpp` (`create_toast_internal` early-out)
- Test: create `tests/unit/test_toast_dedupe.cpp`

**Interfaces:**
- Consumes: existing `ToastList active_`, `create_toast_internal`.
- Produces: `bool ToastManager::refresh_duplicate(ToastSeverity, const char* message)` (private; test access via friend).

- [ ] **Step 1: Write the failing test**

Read `tests/helix_test_fixture.h` and one existing LVGL-based test first for fixture conventions. The test avoids widget creation entirely — it injects fake `ToastInstance` entries via the friend accessor (`dismiss_timer == nullptr` is a supported state; `refresh_duplicate` must null-check it):

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
// tests/unit/test_toast_dedupe.cpp
#include "ui_toast_manager.h"

#include <catch2/catch_test_macros.hpp>

// Friend accessor (L065): reach ToastManager privates without test-only
// methods on the production class.
class ToastManagerTestAccess {
  public:
    static void inject(ToastManager& tm, ToastSeverity sev, const char* msg, bool exiting) {
        ToastManager::ToastInstance inst;
        inst.severity = sev;
        inst.message = msg;
        inst.is_exiting = exiting;
        tm.active_.push_back(std::move(inst));
    }
    static bool refresh_duplicate(ToastManager& tm, ToastSeverity sev, const char* msg) {
        return tm.refresh_duplicate(sev, msg);
    }
    static void clear(ToastManager& tm) {
        tm.active_.clear();
    }
};

TEST_CASE("Toast dedupe: identical active toast is refreshed, not duplicated",
          "[toast][dedupe]") {
    auto& tm = ToastManager::instance();
    ToastManagerTestAccess::clear(tm);

    ToastManagerTestAccess::inject(tm, ToastSeverity::ERROR, "Jog failed: busy", false);
    CHECK(ToastManagerTestAccess::refresh_duplicate(tm, ToastSeverity::ERROR,
                                                    "Jog failed: busy"));

    ToastManagerTestAccess::clear(tm);
}

TEST_CASE("Toast dedupe: different message or severity does not match", "[toast][dedupe]") {
    auto& tm = ToastManager::instance();
    ToastManagerTestAccess::clear(tm);
    ToastManagerTestAccess::inject(tm, ToastSeverity::ERROR, "Jog failed: busy", false);

    CHECK_FALSE(ToastManagerTestAccess::refresh_duplicate(tm, ToastSeverity::WARNING,
                                                          "Jog failed: busy"));
    CHECK_FALSE(
        ToastManagerTestAccess::refresh_duplicate(tm, ToastSeverity::ERROR, "Other error"));
    ToastManagerTestAccess::clear(tm);
}

TEST_CASE("Toast dedupe: exiting toasts don't match", "[toast][dedupe]") {
    auto& tm = ToastManager::instance();
    ToastManagerTestAccess::clear(tm);
    ToastManagerTestAccess::inject(tm, ToastSeverity::ERROR, "Jog failed: busy", true);

    CHECK_FALSE(ToastManagerTestAccess::refresh_duplicate(tm, ToastSeverity::ERROR,
                                                          "Jog failed: busy"));
    ToastManagerTestAccess::clear(tm);
}
```

If `ToastSeverity` lives in a different header, include that too (check `include/ui_notification.h`).

- [ ] **Step 2: Run to verify failure**

Run: `make test 2>&1 | tail -5` — compile FAILURE (`refresh_duplicate`, `severity`, `message` missing).

- [ ] **Step 3: Implement**

`include/ui_toast_manager.h` — extend `ToastInstance`:

```cpp
    struct ToastInstance {
        lv_obj_t* widget = nullptr;
        lv_timer_t* dismiss_timer = nullptr;
        toast_action_callback_t action_cb = nullptr;
        void* action_user_data = nullptr;
        bool is_exiting = false;
        ToastSeverity severity = ToastSeverity::INFO;
        std::string message; // for dedupe of rapid identical toasts
    };
```

Add near `find_by_widget`:

```cpp
    /** If a non-exiting, non-action toast with identical severity+message is
     *  visible, reset its dismiss timer and return true (caller skips
     *  creating a duplicate). Rapid-fire error paths (jog spam, reconnect
     *  storms) otherwise stack N identical widgets in one queue drain. */
    bool refresh_duplicate(ToastSeverity severity, const char* message);
```

plus `friend class ToastManagerTestAccess;` in the private section, and `#include <string>` if missing.

`src/ui/ui_toast_manager.cpp`:

```cpp
bool ToastManager::refresh_duplicate(ToastSeverity severity, const char* message) {
    if (!message) {
        return false;
    }
    for (auto& t : active_) {
        if (!t.is_exiting && t.action_cb == nullptr && t.severity == severity &&
            t.message == message) {
            if (t.dismiss_timer) {
                lv_timer_reset(t.dismiss_timer);
            }
            return true;
        }
    }
    return false;
}
```

At the top of `create_toast_internal` (after its initialization/parameter checks, before any widget creation — read the function first), add:

```cpp
    // Dedupe: rapid identical toasts refresh the existing one instead of
    // stacking. Action toasts are excluded (callback/user_data may differ).
    if (!with_action && refresh_duplicate(severity, message)) {
        return;
    }
```

And where `create_toast_internal` populates the new `ToastInstance`, set the two new fields: `inst.severity = severity; inst.message = message ? message : "";` (find the exact construction site in the function body).

- [ ] **Step 4: Run tests**

Run: `make test-run 2>&1 | tail -15`; `./build/bin/helix-tests "[toast]"` — PASS.

- [ ] **Step 5: Commit**

```bash
git add include/ui_toast_manager.h src/ui/ui_toast_manager.cpp tests/unit/test_toast_dedupe.cpp
git commit -m "fix(ui): dedupe rapid identical toasts — refresh timer instead of stacking"
```

---

### Task 7: Translations, full verification, manual session

**Files:**
- Modify: translation YAMLs + `ui_xml/translations/*.xml` (generated)

- [ ] **Step 1: Translation sync (L064)**

The new user-facing string `lv_tr("Jog failed: {}")` needs keys; the removed per-axis strings ("X jog failed: {}", "Y jog failed: {}", "Z jog failed: {}") should drop out per whatever the sync tool does with stale keys (do not hand-edit).

```bash
make translation-sync && make translations
git add translations/ ui_xml/translations/
git commit -m "chore(i18n): sync translations for jog coalescing strings"
```

(Adjust the YAML directory path to wherever `make translation-sync` reports writing — check its output.)

- [ ] **Step 2: Full test suite + lint gates**

```bash
make test-run 2>&1 | tail -20          # zero failures required
./build/bin/helix-tests "[jog_coalescer]" && ./build/bin/helix-tests "[busy_guard]" \
  && ./build/bin/helix-tests "[gcode]" && ./build/bin/helix-tests "[toast]"
python3 scripts/check_l081_anti_pattern.py 2>/dev/null || true   # should report no new instances
```

- [ ] **Step 3: Manual mock-printer session (requires the user — L060)**

Launch in background with tee (NOT shell `&`):
`./build/bin/helix-screen --test -vv 2>&1 | tee /tmp/claude-1000/-home-pbrown-Code-Printing-helixscreen/98762311-0340-4f87-8876-d50fd0019b00/scratchpad/jog-test.log`

Ask the user to: open Motion panel, home XY, then rapid-tap the jog wheel 5-10× in one direction, reverse mid-stream, ride an axis to the bed edge, and rapid-tap Z. Expected: NO "Printer is busy" toasts, coalesced flush lines in the DEBUG log (`Jog coalesced: pending …`), a single edge warning at the boundary, position lands where the tap count says it should. Then read the log.

- [ ] **Step 4: Wrap up**

Merge decision via superpowers:finishing-a-development-branch (solo repo: merge to main after review, no PR ceremony).

---

## Self-review notes

- Spec coverage: coalescer (T1/T4), attribution (T2/T3), on_ui_destroyed (T5), toast dedupe (T6), translations+manual (T7). Edge-warn dedupe is in T4. Deactivate/print-start pending-drop: deactivate covered in T4; print-start is implicitly covered because a real file print makes `is_blocking_operation_active()` false (PRINTING excluded) but jog UI is unreachable mid-print via nav; no extra code.
- Type consistency: `AxisMove`/`on_tap`/`on_ack`/`uncommitted_*` names match between T1 and T4; `move_relative` signature matches between T3 and T4; `refresh_duplicate` matches T6 test and impl.
- Known judgment calls for the implementer: exact insertion points ("read the function first") are flagged inline where line numbers may drift.
