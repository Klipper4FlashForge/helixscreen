# Unified Filament Consumption Tracker — Implementation Plan

> ⚠️ **Historical record (verified 2026-08-09) - not instructions. Status: SHIPPED.** The 55
> `- [ ]` boxes were never ticked and do **not** mean the work is outstanding. Evidence:
> `FilamentConsumptionTracker` (`src/printer/filament_consumption_tracker.cpp`),
> `IConsumptionSink` / `AmsSlotSink` / `ExternalSpoolSink` (`include/consumption_sink.h`,
> `src/printer/consumption_sink.cpp`).
>
> Stale paths: `src/ui/ui_ams_edit_modal.cpp` is now `src/ui/ui_ams_edit_overlay.cpp`, and
> `tests/unit/test_helpers.h` is now the `tests/test_helpers/` directory. Verify against the
> tree before following anything here.

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Extend `FilamentConsumptionTracker` to decrement `remaining_weight_g` per AMS slot during prints, for backends that don't self-report (Snapmaker, CFS, IFS, HappyHare, ACE, ToolChanger), while preserving the existing external-spool behavior.

**Architecture:** Refactor the singleton tracker into a multi-stream coordinator that owns print lifecycle and routes deltas to pluggable sinks. One `ExternalSpoolSink` (replaces existing inline logic) + one `AmsSlotSink` per registered slot. Gate tracking on unknown weight, Spoolman linkage, and a new backend virtual `tracks_consumption_natively()`. Attribute consumption per-extruder when the backend declares a tool→slot mapping; fall back to aggregate `filament_used_mm` + `current_slot` otherwise.

**Tech Stack:** C++17, LVGL observer system, spdlog, Catch2 tests (`HelixTestFixture`), Moonraker database via `FilamentSlotOverrideStore`.

**Design doc:** `docs/devel/specs/2026-04-19-filament-consumption-tracker-unified-design.md`

---

## File Map

Created:
- `include/consumption_sink.h` — `IConsumptionSink` interface + `ExternalSpoolSink` + `AmsSlotSink` declarations
- `src/printer/consumption_sink.cpp` — sink implementations
- `tests/unit/test_consumption_sink.cpp` — per-sink unit tests
- `tests/unit/test_consumption_sink_ams.cpp` — `AmsSlotSink`-focused tests

Modified:
- `include/filament_consumption_tracker.h` — multi-sink refactor + public register/unregister API
- `src/printer/filament_consumption_tracker.cpp` — delegate to sinks, multi-subject routing
- `include/ams_backend.h` — new virtuals `slot_for_extruder()`, `tracks_consumption_natively()`
- `include/ams_backend_snapmaker.h` + `src/printer/ams_backend_snapmaker.cpp` — override `slot_for_extruder()`
- `include/ams_backend_toolchanger.h` + `src/printer/ams_backend_toolchanger.cpp` — override `slot_for_extruder()`
- `include/printer_state.h` + `src/printer/printer_state.cpp` (or `print_domain.{h,cpp}`) — per-extruder `filament_used_mm` subjects
- `src/printer/ams_state.cpp` — register/unregister slot sinks on backend lifecycle
- `src/ui/ui_ams_edit_modal.cpp` — blank instead of `-1`; verify weight cap ≥5000g
- `tests/unit/test_filament_consumption_tracker.cpp` — migrate existing external-spool tests to sink architecture; add lifecycle tests

Unchanged (reference only):
- `include/ams_types.h` — `SlotInfo` struct (no schema change)
- `include/filament_slot_override.h` + `src/printer/filament_slot_override_store.cpp` — already serializes `remaining_weight_g` to Moonraker `lane_data`

---

## Phase 0: Baseline & Worktree

- [ ] **Step 1: Confirm worktree**

You should already be in `.worktrees/filament-consumption-tracker-unified` (created by new-project). Verify:

```bash
pwd
git rev-parse --show-toplevel
git status --short
```

Expected: worktree path ends in the feature name, branch is `feature/filament-consumption-tracker-unified`.

- [ ] **Step 2: Baseline build + tests**

```bash
make -j
make test-run 2>&1 | tail -40
```

Expected: Clean build, all tests pass. If baseline is broken, stop and report — don't proceed with changes on top of red.

- [ ] **Step 3: Run the existing tracker test specifically**

```bash
./build/bin/helix-tests "[filament_consumption_tracker]" 2>&1 | tail -30
```

Expected: All existing tracker tests pass. Note the count — we'll preserve that + add more.

---

## Phase 1: IConsumptionSink Interface + ExternalSpoolSink (Behavioral No-Op)

Refactor the tracker's inline external-spool logic into a reusable `ExternalSpoolSink`. Tracker still holds the sink directly (singleton sink) — multi-sink registration comes in Phase 4.

**Files:**
- Create: `include/consumption_sink.h`, `src/printer/consumption_sink.cpp`, `tests/unit/test_consumption_sink.cpp`
- Modify: `include/filament_consumption_tracker.h`, `src/printer/filament_consumption_tracker.cpp`

- [ ] **Step 1: Write failing test for `ExternalSpoolSink` snapshot + apply_delta**

`tests/unit/test_consumption_sink.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include "consumption_sink.h"

#include "ams_state.h"
#include "helix_test_fixture.h"
#include "settings_manager.h"

#include <catch2/catch_test_macros.hpp>

using helix::ExternalSpoolSink;

namespace {

struct ExternalSpoolSinkFixture : HelixTestFixture {
    ExternalSpoolSinkFixture() {
        SlotInfo info;
        info.material = "PLA";
        info.remaining_weight_g = 1000.0f;
        info.total_weight_g = 1000.0f;
        AmsState::instance().set_external_spool_info(info);
    }
};

} // namespace

TEST_CASE_METHOD(ExternalSpoolSinkFixture,
                 "ExternalSpoolSink: snapshot captures baseline when trackable",
                 "[consumption_sink][external]") {
    ExternalSpoolSink sink;
    sink.snapshot(0.0f);
    REQUIRE(sink.is_trackable());
}

TEST_CASE_METHOD(ExternalSpoolSinkFixture,
                 "ExternalSpoolSink: apply_delta decrements remaining_weight_g",
                 "[consumption_sink][external]") {
    ExternalSpoolSink sink;
    sink.snapshot(0.0f);
    // 1000 mm of 1.75mm PLA ≈ 3.0 g
    sink.apply_delta(1000.0f);
    auto info = AmsState::instance().get_external_spool_info();
    REQUIRE(info.has_value());
    REQUIRE(info->remaining_weight_g < 1000.0f);
    REQUIRE(info->remaining_weight_g > 996.0f);
}

TEST_CASE_METHOD(ExternalSpoolSinkFixture,
                 "ExternalSpoolSink: unknown weight not trackable",
                 "[consumption_sink][external]") {
    SlotInfo info;
    info.material = "PLA";
    info.remaining_weight_g = -1.0f;
    AmsState::instance().set_external_spool_info(info);

    ExternalSpoolSink sink;
    sink.snapshot(0.0f);
    REQUIRE_FALSE(sink.is_trackable());
}
```

- [ ] **Step 2: Run test — expect compile failure (header doesn't exist)**

```bash
make test 2>&1 | tail -20
```

Expected: fatal error, `consumption_sink.h: No such file or directory`.

- [ ] **Step 3: Create `include/consumption_sink.h`**

```cpp
// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace helix {

/// Target for per-stream filament consumption updates. Owns its own baseline
/// (snapshot_mm + snapshot_weight_g), material density, and persistence throttle.
/// Tracker calls these hooks in response to print lifecycle + filament_used subject events.
class IConsumptionSink {
  public:
    virtual ~IConsumptionSink() = default;

    /// Stable identifier for logging ("external", "ams:0:2", etc).
    [[nodiscard]] virtual std::string_view name() const = 0;

    /// True when this sink has a valid baseline + density. Tracker skips apply_delta
    /// calls when false; sink may become trackable later (e.g. user sets weight mid-print)
    /// and rebaseline() fires on the next delta.
    [[nodiscard]] virtual bool is_trackable() const = 0;

    /// Capture the current remaining_weight_g + filament_used_mm baseline. Called on
    /// print start and when a sink is registered mid-print with a valid state.
    virtual void snapshot(float filament_used_mm) = 0;

    /// Apply the cumulative filament_used_mm delta since snapshot. Sink converts mm→g
    /// and writes remaining_weight_g back to its store. Must be monotonically non-
    /// decreasing between snapshots; tracker is responsible for handling resets.
    virtual void apply_delta(float filament_used_mm) = 0;

    /// Flush any in-memory changes to persistent store. Called on print end, pause,
    /// and unregister.
    virtual void flush() = 0;

    /// Reset baseline to the currently-observed state. Used when an external writer
    /// (Spoolman poll, user edit) has updated remaining_weight_g between our ticks.
    virtual void rebaseline(float filament_used_mm) = 0;
};

/// Sink for the non-AMS external spool (single global). Writes to settings.json via
/// SettingsManager::set_external_spool_info() through AmsState.
class ExternalSpoolSink : public IConsumptionSink {
  public:
    [[nodiscard]] std::string_view name() const override;
    [[nodiscard]] bool is_trackable() const override;
    void snapshot(float filament_used_mm) override;
    void apply_delta(float filament_used_mm) override;
    void flush() override;
    void rebaseline(float filament_used_mm) override;

    /// Test hook: override persist throttle (ms). 0 = default 60s.
    void set_persist_interval_ms_for_testing(uint32_t ms) {
        persist_interval_override_ms_ = ms;
    }

  private:
    bool active_ = false;
    float snapshot_mm_ = 0.0f;
    float snapshot_weight_g_ = 0.0f;
    float density_g_cm3_ = 0.0f;
    float diameter_mm_ = 1.75f;
    float last_written_weight_g_ = 0.0f;
    uint32_t last_persist_tick_ms_ = 0;
    uint32_t persist_interval_override_ms_ = 0;

    [[nodiscard]] uint32_t persist_interval_ms() const;
};

} // namespace helix
```

- [ ] **Step 4: Create `src/printer/consumption_sink.cpp` — `ExternalSpoolSink` moved from tracker**

Port the body of `on_filament_used_changed`, `snapshot`, `persist` from `src/printer/filament_consumption_tracker.cpp` into the sink, keeping semantics identical. Key translation:

- `active_`, `snapshot_mm_`, `snapshot_weight_g_`, `density_g_cm3_`, `diameter_mm_`, `last_written_weight_g_`, `last_persist_tick_ms_` become sink members.
- `on_filament_used_changed(int mm)` body → `apply_delta(float mm)` + the external-write-detection branch → `rebaseline()` (tracker calls this based on its own delta comparison).
- `snapshot()` body → `snapshot(float mm)` using the passed mm instead of reading the subject.
- `persist()` body → `flush()`.

```cpp
// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#include "consumption_sink.h"

#include "ams_state.h"
#include "filament_database.h"
#include "lv_tick_shim.h"
#include <cmath>
#include <spdlog/spdlog.h>

namespace helix {

namespace {
constexpr uint32_t kPersistIntervalMs = 60'000;
constexpr float kDeltaWriteThresholdG = 0.05f;
constexpr float kRebaselineThresholdG = 0.5f;
} // namespace

std::string_view ExternalSpoolSink::name() const {
    return "external";
}

bool ExternalSpoolSink::is_trackable() const {
    return active_;
}

uint32_t ExternalSpoolSink::persist_interval_ms() const {
    return persist_interval_override_ms_ ? persist_interval_override_ms_ : kPersistIntervalMs;
}

void ExternalSpoolSink::snapshot(float filament_used_mm) {
    active_ = false;
    auto info_opt = AmsState::instance().get_external_spool_info();
    if (!info_opt.has_value()) {
        spdlog::debug("[ConsumptionSink:external] No external spool; skipping");
        return;
    }
    const auto& info = *info_opt;
    if (info.remaining_weight_g < 0.0f) {
        spdlog::debug("[ConsumptionSink:external] Unknown remaining weight; skipping");
        return;
    }
    auto material = filament::find_material(info.material);
    if (!material.has_value() || material->density_g_cm3 <= 0.0f) {
        spdlog::warn("[ConsumptionSink:external] No density for material '{}'; skipping",
                     info.material);
        return;
    }
    density_g_cm3_ = material->density_g_cm3;
    diameter_mm_ = 1.75f;
    snapshot_mm_ = filament_used_mm;
    snapshot_weight_g_ = info.remaining_weight_g;
    last_written_weight_g_ = info.remaining_weight_g;
    last_persist_tick_ms_ = lv_tick_get();
    active_ = true;
    spdlog::info(
        "[ConsumptionSink:external] Snapshot material={} density={} g/cm3 weight={} g mm={}",
        info.material, density_g_cm3_, snapshot_weight_g_, snapshot_mm_);
}

void ExternalSpoolSink::apply_delta(float filament_used_mm) {
    if (!active_) return;

    auto info_opt = AmsState::instance().get_external_spool_info();
    if (!info_opt.has_value()) return;
    SlotInfo info = *info_opt;

    // External-write detection: rebaseline from authoritative value.
    if (std::abs(info.remaining_weight_g - last_written_weight_g_) > kRebaselineThresholdG) {
        spdlog::info("[ConsumptionSink:external] External write detected "
                     "(was {} g, now {} g); rebaselining",
                     last_written_weight_g_, info.remaining_weight_g);
        snapshot_mm_ = filament_used_mm;
        snapshot_weight_g_ = info.remaining_weight_g;
        last_written_weight_g_ = info.remaining_weight_g;
        return;
    }

    float consumed_mm = filament_used_mm - snapshot_mm_;
    if (consumed_mm < 0.0f) {
        // Reset under us (new print). Rebase.
        snapshot_mm_ = filament_used_mm;
        snapshot_weight_g_ = info.remaining_weight_g;
        last_written_weight_g_ = info.remaining_weight_g;
        return;
    }

    float consumed_g = filament::length_to_weight_g(consumed_mm, density_g_cm3_, diameter_mm_);
    float new_remaining_g = std::max(0.0f, snapshot_weight_g_ - consumed_g);

    if (std::abs(new_remaining_g - info.remaining_weight_g) < kDeltaWriteThresholdG) return;

    info.remaining_weight_g = new_remaining_g;
    AmsState::instance().set_external_spool_info_in_memory(info);

    if (lv_tick_elaps(last_persist_tick_ms_) >= persist_interval_ms()) {
        AmsState::instance().set_external_spool_info(info);
        last_persist_tick_ms_ = lv_tick_get();
    }
    last_written_weight_g_ = new_remaining_g;
}

void ExternalSpoolSink::flush() {
    auto info_opt = AmsState::instance().get_external_spool_info();
    if (!info_opt.has_value()) return;
    AmsState::instance().set_external_spool_info(*info_opt);
    last_written_weight_g_ = info_opt->remaining_weight_g;
    last_persist_tick_ms_ = lv_tick_get();
}

void ExternalSpoolSink::rebaseline(float filament_used_mm) {
    auto info_opt = AmsState::instance().get_external_spool_info();
    if (!info_opt.has_value()) {
        active_ = false;
        return;
    }
    snapshot_mm_ = filament_used_mm;
    snapshot_weight_g_ = info_opt->remaining_weight_g;
    last_written_weight_g_ = info_opt->remaining_weight_g;
}

} // namespace helix
```

- [ ] **Step 5: Update `include/filament_consumption_tracker.h`**

Replace state members with a single `ExternalSpoolSink external_sink_`, keeping the public API (`start`, `stop`, `is_active`, `instance`) stable. Remove the now-unused member fields (snapshot_mm_, etc). The tracker delegates all weight math to the sink in this phase; multi-sink vector comes in Phase 4.

```cpp
// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "consumption_sink.h"
#include "ui_observer_guard.h"

namespace helix {

class FilamentConsumptionTracker {
  public:
    static FilamentConsumptionTracker& instance();
    void start();
    void stop();
    [[nodiscard]] bool is_active() const { return active_; }

  private:
    friend struct FilamentConsumptionTrackerTestAccess;

    FilamentConsumptionTracker() = default;
    ~FilamentConsumptionTracker() = default;
    FilamentConsumptionTracker(const FilamentConsumptionTracker&) = delete;
    FilamentConsumptionTracker& operator=(const FilamentConsumptionTracker&) = delete;

    ExternalSpoolSink external_sink_;
    bool active_ = false;

    ObserverGuard print_state_obs_;
    ObserverGuard filament_used_obs_;

    void on_print_state_changed(int job_state);
    void on_filament_used_changed(int filament_mm);
};

} // namespace helix
```

- [ ] **Step 6: Rewrite `src/printer/filament_consumption_tracker.cpp`**

```cpp
// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#include "filament_consumption_tracker.h"

#include "ams_types.h"
#include "observer_factory.h"
#include "printer_state.h"
#include <spdlog/spdlog.h>

namespace helix {

FilamentConsumptionTracker& FilamentConsumptionTracker::instance() {
    static FilamentConsumptionTracker inst;
    return inst;
}

void FilamentConsumptionTracker::start() {
    PrinterState& printer = get_printer_state();

    print_state_obs_ = helix::ui::observe_int_sync<FilamentConsumptionTracker>(
        printer.get_print_state_enum_subject(), this,
        [](FilamentConsumptionTracker* self, int state) {
            self->on_print_state_changed(state);
        });

    filament_used_obs_ = helix::ui::observe_int_sync<FilamentConsumptionTracker>(
        printer.get_print_filament_used_subject(), this,
        [](FilamentConsumptionTracker* self, int mm) {
            self->on_filament_used_changed(mm);
        });
}

void FilamentConsumptionTracker::stop() {
    print_state_obs_.reset();
    filament_used_obs_.reset();
    active_ = false;
}

void FilamentConsumptionTracker::on_print_state_changed(int job_state) {
    auto state = static_cast<PrintJobState>(job_state);
    const auto* printer_mm = get_printer_state().get_print_filament_used_subject();
    float mm = static_cast<float>(lv_subject_get_int(printer_mm));

    switch (state) {
        case PrintJobState::PRINTING:
            if (!active_) {
                external_sink_.snapshot(mm);
                active_ = true;
            }
            break;
        case PrintJobState::COMPLETE:
        case PrintJobState::CANCELLED:
        case PrintJobState::ERROR:
            if (active_) {
                external_sink_.flush();
                spdlog::info("[FilamentTracker] Print ended (state={}); flushed sinks",
                             job_state);
            }
            active_ = false;
            break;
        case PrintJobState::PAUSED:
            if (active_) external_sink_.flush();
            break;
        default:
            break;
    }
}

void FilamentConsumptionTracker::on_filament_used_changed(int filament_mm) {
    if (!active_) return;
    external_sink_.apply_delta(static_cast<float>(filament_mm));
}

} // namespace helix
```

- [ ] **Step 7: Add `test_consumption_sink.cpp` to CMakeLists/Makefile**

Open the Makefile and find the existing `test_filament_consumption_tracker.cpp` entry. Add `tests/unit/test_consumption_sink.cpp` next to it following the same pattern. If the test list is glob-based, no change needed.

```bash
grep -n "test_filament_consumption_tracker" Makefile
```

- [ ] **Step 8: Build + run sink tests**

```bash
make test-run 2>&1 | tail -40
```

Expected: sink tests pass; existing tracker tests still pass (same behavior, new indirection).

- [ ] **Step 9: Commit**

```bash
git add include/consumption_sink.h src/printer/consumption_sink.cpp \
        tests/unit/test_consumption_sink.cpp \
        include/filament_consumption_tracker.h src/printer/filament_consumption_tracker.cpp \
        Makefile
git commit -m "refactor(filament): extract ExternalSpoolSink from tracker

Tracker now delegates snapshot/delta/flush to a pluggable
IConsumptionSink. Behavioral no-op: external spool path preserved
identically. Prepares for multi-sink AMS routing."
```

---

## Phase 2: AmsBackend Capability Virtuals

Add two virtuals on the `AmsBackend` base class with sensible defaults. No overrides yet — tracker gating will consume them in Phase 5.

**Files:**
- Modify: `include/ams_backend.h`

- [ ] **Step 1: Open `include/ams_backend.h` and locate the public interface block**

The file has the full virtual interface starting around line 80. Find a logical grouping near slot-query virtuals (`get_slot_info`, `get_current_slot`).

- [ ] **Step 2: Add the two virtuals with defaulted bodies**

```cpp
    /**
     * @brief Slot index currently sourced by the given extruder. Backends that model
     * per-extruder attribution (tool-changers with one spool per tool) override to
     * return the tool→slot mapping. Default returns nullopt; callers fall back to
     * aggregate filament_used_mm + current_slot().
     * @param extruder_idx 0-based extruder index (0 = primary, 1 = extruder1, …)
     */
    [[nodiscard]] virtual std::optional<int> slot_for_extruder(int extruder_idx) const {
        (void)extruder_idx;
        return std::nullopt;
    }

    /**
     * @brief True when this backend already populates remaining_weight_g from a live
     * printer-side source. FilamentConsumptionTracker skips slots on such backends
     * to avoid double-counting.
     */
    [[nodiscard]] virtual bool tracks_consumption_natively() const {
        return false;
    }
```

Add `#include <optional>` at top of file if not already present.

- [ ] **Step 3: Build (no tests yet — defaults exercised later)**

```bash
make -j 2>&1 | tail -10
```

Expected: Clean build. No behavior change yet.

- [ ] **Step 4: Commit**

```bash
git add include/ams_backend.h
git commit -m "feat(ams-backend): add slot_for_extruder() and tracks_consumption_natively()

Opt-in capability declarations. Defaults to nullopt / false. Tracker
will consume these in a later commit."
```

---

## Phase 3: Per-Extruder `filament_used_mm` Subjects

Expose per-extruder `filament_used_mm` subjects on `PrinterState`, populated from Klipper's `extruder`, `extruder1`, `extruder2`, … objects. Subjects are dynamic (destroyed on reconnect) — observers must use `SubjectLifetime` tokens ([L077]).

**Files:**
- Modify: `include/printer_state.h`, source file that owns `print_domain_` (likely `src/printer/print_domain.{h,cpp}` or `src/printer/printer_state.cpp`)
- Test: add cases to an existing PrinterState test file or create `tests/unit/test_printer_state_extruder_filament.cpp`

- [ ] **Step 1: Locate the existing aggregate subject implementation**

```bash
grep -rn "print_filament_used" include/ src/ --include=*.h --include=*.cpp | head
```

Identify which file owns the aggregate `filament_used` subject and its backing storage (likely `print_domain_`). We'll mirror the pattern.

- [ ] **Step 2: Write failing test**

Create `tests/unit/test_printer_state_extruder_filament.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include "helix_test_fixture.h"
#include "printer_state.h"
#include <catch2/catch_test_macros.hpp>

namespace {

struct ExtruderFilamentFixture : HelixTestFixture {};

} // namespace

TEST_CASE_METHOD(ExtruderFilamentFixture,
                 "PrinterState: per-extruder filament_used subject returns non-null for idx 0-3",
                 "[printer_state][filament_used]") {
    PrinterState& state = get_printer_state();
    helix::SubjectLifetime lifetime;
    for (int i = 0; i < 4; ++i) {
        auto* subj = state.get_extruder_filament_used_subject(i, lifetime);
        REQUIRE(subj != nullptr);
    }
}

TEST_CASE_METHOD(ExtruderFilamentFixture,
                 "PrinterState: per-extruder subject reflects status update",
                 "[printer_state][filament_used]") {
    PrinterState& state = get_printer_state();
    helix::SubjectLifetime lifetime;
    auto* subj = state.get_extruder_filament_used_subject(1, lifetime);

    // Simulate a Klipper status update for extruder1.
    helix::json status;
    status["extruder1"]["filament_used"] = 123.4;
    state.handle_status_update(status);
    helix::ui::UpdateQueue::instance().drain_queue_for_testing();

    REQUIRE(lv_subject_get_int(subj) == 123);
}
```

(Adjust `handle_status_update` to whatever entry-point `PrinterState` uses to ingest Klipper status payloads — inspect existing test files for examples.)

- [ ] **Step 3: Run test — expect compile failure (method doesn't exist)**

```bash
make test 2>&1 | tail -10
```

- [ ] **Step 4: Add the accessor to `PrinterState` (or its print_domain)**

In the relevant header, add:

```cpp
    /// Per-extruder filament_used (mm), 0-based. Returns a dynamic subject — callers
    /// MUST pass a SubjectLifetime token and subscribe with observe_int_sync(..., lifetime).
    lv_subject_t* get_extruder_filament_used_subject(int extruder_idx,
                                                      helix::SubjectLifetime& lifetime);
```

In the implementation, maintain a `std::map<int, lv_subject_t>` of extruder_idx → subject (or reuse whatever pattern per-extruder temp subjects use — check `get_extruder_temp_subject` in `printer_temperature_state.cpp` for the canonical lifetime-scoped pattern).

In the Klipper status-update handler (search for where `extruder.filament_used` is currently parsed for the aggregate subject), add per-extruder parsing:

```cpp
for (int idx = 0; idx < max_extruders_; ++idx) {
    std::string key = idx == 0 ? "extruder" : "extruder" + std::to_string(idx);
    auto it = status.find(key);
    if (it == status.end() || !it->contains("filament_used")) continue;
    int mm = static_cast<int>(it->at("filament_used").get<double>());
    auto subj_it = extruder_filament_used_subjects_.find(idx);
    if (subj_it != extruder_filament_used_subjects_.end()) {
        lv_subject_set_int(subj_it->second.get(), mm);
    }
}
```

Follow the existing `get_extruder_temp_subject` pattern for lifetime plumbing — do not invent a new pattern.

- [ ] **Step 5: Run test — expect pass**

```bash
make test-run 2>&1 | tail -20
```

- [ ] **Step 6: Commit**

```bash
git add include/printer_state.h src/printer/<relevant-file>.cpp \
        tests/unit/test_printer_state_extruder_filament.cpp Makefile
git commit -m "feat(printer-state): per-extruder filament_used_mm subjects

Dynamic subjects, lifetime-token API matching get_extruder_temp_subject.
Populated from Klipper 'extruder<n>.filament_used' fields on status
update. Enables per-tool consumption attribution."
```

---

## Phase 4: AmsSlotSink + Per-Slot Registration

Implement `AmsSlotSink` (writes via `AmsBackend::set_slot_info` → `FilamentSlotOverrideStore`). Extend the tracker with a sink registry. Wire `AmsState` to register/unregister slot sinks as backends come and go.

**Files:**
- Modify: `include/consumption_sink.h`, `src/printer/consumption_sink.cpp`
- Modify: `include/filament_consumption_tracker.h`, `src/printer/filament_consumption_tracker.cpp`
- Modify: `src/printer/ams_state.cpp`
- Create: `tests/unit/test_consumption_sink_ams.cpp`

- [ ] **Step 1: Failing test for `AmsSlotSink`**

`tests/unit/test_consumption_sink_ams.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ams_backend_mock.h"
#include "ams_state.h"
#include "consumption_sink.h"
#include "helix_test_fixture.h"
#include <catch2/catch_test_macros.hpp>

using helix::AmsSlotSink;

namespace {

struct AmsSlotSinkFixture : HelixTestFixture {
    int backend_idx = 0;
    AmsBackendMock* mock = nullptr;

    AmsSlotSinkFixture() {
        auto m = std::make_unique<AmsBackendMock>();
        mock = m.get();
        backend_idx = AmsState::instance().add_backend(std::move(m));
        AmsState::instance().set_active_backend(backend_idx);

        SlotInfo info = mock->get_slot_info(0);
        info.material = "PLA";
        info.remaining_weight_g = 500.0f;
        info.total_weight_g = 1000.0f;
        info.spoolman_id = 0;
        mock->set_slot_info(0, info, /*persist=*/false);
    }
};

} // namespace

TEST_CASE_METHOD(AmsSlotSinkFixture,
                 "AmsSlotSink: snapshot when weight known + no spoolman → trackable",
                 "[consumption_sink][ams]") {
    AmsSlotSink sink(backend_idx, 0);
    sink.snapshot(0.0f);
    REQUIRE(sink.is_trackable());
}

TEST_CASE_METHOD(AmsSlotSinkFixture,
                 "AmsSlotSink: skipped when remaining_weight_g < 0",
                 "[consumption_sink][ams]") {
    SlotInfo info = mock->get_slot_info(0);
    info.remaining_weight_g = -1.0f;
    mock->set_slot_info(0, info, false);

    AmsSlotSink sink(backend_idx, 0);
    sink.snapshot(0.0f);
    REQUIRE_FALSE(sink.is_trackable());
}

TEST_CASE_METHOD(AmsSlotSinkFixture,
                 "AmsSlotSink: skipped when spoolman_id != 0",
                 "[consumption_sink][ams]") {
    SlotInfo info = mock->get_slot_info(0);
    info.spoolman_id = 42;
    mock->set_slot_info(0, info, false);

    AmsSlotSink sink(backend_idx, 0);
    sink.snapshot(0.0f);
    REQUIRE_FALSE(sink.is_trackable());
}

TEST_CASE_METHOD(AmsSlotSinkFixture,
                 "AmsSlotSink: apply_delta decrements remaining_weight_g",
                 "[consumption_sink][ams]") {
    AmsSlotSink sink(backend_idx, 0);
    sink.snapshot(0.0f);
    sink.apply_delta(1000.0f);
    SlotInfo after = mock->get_slot_info(0);
    REQUIRE(after.remaining_weight_g < 500.0f);
    REQUIRE(after.remaining_weight_g > 496.0f);
}

TEST_CASE_METHOD(AmsSlotSinkFixture,
                 "AmsSlotSink: skipped when backend declares native tracking",
                 "[consumption_sink][ams]") {
    mock->set_tracks_consumption_natively_for_testing(true);
    AmsSlotSink sink(backend_idx, 0);
    sink.snapshot(0.0f);
    REQUIRE_FALSE(sink.is_trackable());
}
```

(Add `set_tracks_consumption_natively_for_testing` on the mock — mock is test infrastructure so a setter is fine, not test-only pollution on production code per [L065].)

- [ ] **Step 2: Run tests — expect compile failure (AmsSlotSink doesn't exist)**

```bash
make test 2>&1 | tail -15
```

- [ ] **Step 3: Add `AmsSlotSink` declaration to `include/consumption_sink.h`**

```cpp
/// Sink for a single AMS slot. Writes via AmsBackend::set_slot_info() which routes
/// through FilamentSlotOverrideStore → Moonraker lane_data.
class AmsSlotSink : public IConsumptionSink {
  public:
    AmsSlotSink(int backend_index, int slot_index);

    [[nodiscard]] std::string_view name() const override;
    [[nodiscard]] bool is_trackable() const override;
    void snapshot(float filament_used_mm) override;
    void apply_delta(float filament_used_mm) override;
    void flush() override;
    void rebaseline(float filament_used_mm) override;

    [[nodiscard]] int backend_index() const { return backend_index_; }
    [[nodiscard]] int slot_index() const { return slot_index_; }

    void set_persist_interval_ms_for_testing(uint32_t ms) {
        persist_interval_override_ms_ = ms;
    }

  private:
    const int backend_index_;
    const int slot_index_;
    std::string name_;  // "ams:<backend>:<slot>"

    bool active_ = false;
    float snapshot_mm_ = 0.0f;
    float snapshot_weight_g_ = 0.0f;
    float density_g_cm3_ = 0.0f;
    float diameter_mm_ = 1.75f;
    float last_written_weight_g_ = 0.0f;
    uint32_t last_persist_tick_ms_ = 0;
    uint32_t persist_interval_override_ms_ = 0;

    [[nodiscard]] uint32_t persist_interval_ms() const;
    [[nodiscard]] std::optional<SlotInfo> current_info() const;
};
```

Forward-declare `SlotInfo` or include `ams_types.h` at top.

- [ ] **Step 4: Implement `AmsSlotSink` in `src/printer/consumption_sink.cpp`**

Mirror `ExternalSpoolSink` but read/write through the backend:

```cpp
AmsSlotSink::AmsSlotSink(int backend_index, int slot_index)
    : backend_index_(backend_index), slot_index_(slot_index),
      name_("ams:" + std::to_string(backend_index) + ":" + std::to_string(slot_index)) {}

std::string_view AmsSlotSink::name() const { return name_; }
bool AmsSlotSink::is_trackable() const { return active_; }
uint32_t AmsSlotSink::persist_interval_ms() const {
    return persist_interval_override_ms_ ? persist_interval_override_ms_ : kPersistIntervalMs;
}

std::optional<SlotInfo> AmsSlotSink::current_info() const {
    AmsBackend* backend = AmsState::instance().get_backend(backend_index_);
    if (!backend) return std::nullopt;
    return backend->get_slot_info(slot_index_);
}

void AmsSlotSink::snapshot(float filament_used_mm) {
    active_ = false;
    AmsBackend* backend = AmsState::instance().get_backend(backend_index_);
    if (!backend) return;
    if (backend->tracks_consumption_natively()) {
        spdlog::debug("[ConsumptionSink:{}] Backend tracks natively; skipping", name_);
        return;
    }
    auto info_opt = current_info();
    if (!info_opt || info_opt->remaining_weight_g < 0.0f) return;
    if (info_opt->spoolman_id != 0) {
        spdlog::debug("[ConsumptionSink:{}] Spoolman-linked; skipping", name_);
        return;
    }
    auto material = filament::find_material(info_opt->material);
    if (!material || material->density_g_cm3 <= 0.0f) {
        spdlog::warn("[ConsumptionSink:{}] No density for '{}'; skipping",
                     name_, info_opt->material);
        return;
    }
    density_g_cm3_ = material->density_g_cm3;
    diameter_mm_ = 1.75f;
    snapshot_mm_ = filament_used_mm;
    snapshot_weight_g_ = info_opt->remaining_weight_g;
    last_written_weight_g_ = info_opt->remaining_weight_g;
    last_persist_tick_ms_ = lv_tick_get();
    active_ = true;
    spdlog::info("[ConsumptionSink:{}] Snapshot weight={} g mm={}",
                 name_, snapshot_weight_g_, snapshot_mm_);
}

void AmsSlotSink::apply_delta(float filament_used_mm) {
    if (!active_) return;
    AmsBackend* backend = AmsState::instance().get_backend(backend_index_);
    if (!backend) return;
    auto info_opt = current_info();
    if (!info_opt) return;
    SlotInfo info = *info_opt;

    // Re-evaluate gating each tick (spool_id may have been linked mid-print).
    if (info.spoolman_id != 0 || backend->tracks_consumption_natively()) {
        active_ = false;
        return;
    }

    if (std::abs(info.remaining_weight_g - last_written_weight_g_) > kRebaselineThresholdG) {
        spdlog::info("[ConsumptionSink:{}] External write (was {}, now {}); rebaselining",
                     name_, last_written_weight_g_, info.remaining_weight_g);
        snapshot_mm_ = filament_used_mm;
        snapshot_weight_g_ = info.remaining_weight_g;
        last_written_weight_g_ = info.remaining_weight_g;
        return;
    }

    float consumed_mm = filament_used_mm - snapshot_mm_;
    if (consumed_mm < 0.0f) {
        snapshot_mm_ = filament_used_mm;
        snapshot_weight_g_ = info.remaining_weight_g;
        last_written_weight_g_ = info.remaining_weight_g;
        return;
    }

    float consumed_g = filament::length_to_weight_g(consumed_mm, density_g_cm3_, diameter_mm_);
    float new_remaining_g = std::max(0.0f, snapshot_weight_g_ - consumed_g);
    if (std::abs(new_remaining_g - info.remaining_weight_g) < kDeltaWriteThresholdG) return;

    info.remaining_weight_g = new_remaining_g;
    const bool persist_now = lv_tick_elaps(last_persist_tick_ms_) >= persist_interval_ms();
    backend->set_slot_info(slot_index_, info, /*persist=*/persist_now);
    if (persist_now) last_persist_tick_ms_ = lv_tick_get();
    last_written_weight_g_ = new_remaining_g;
}

void AmsSlotSink::flush() {
    AmsBackend* backend = AmsState::instance().get_backend(backend_index_);
    if (!backend) return;
    auto info_opt = current_info();
    if (!info_opt) return;
    backend->set_slot_info(slot_index_, *info_opt, /*persist=*/true);
    last_written_weight_g_ = info_opt->remaining_weight_g;
    last_persist_tick_ms_ = lv_tick_get();
}

void AmsSlotSink::rebaseline(float filament_used_mm) {
    auto info_opt = current_info();
    if (!info_opt) { active_ = false; return; }
    snapshot_mm_ = filament_used_mm;
    snapshot_weight_g_ = info_opt->remaining_weight_g;
    last_written_weight_g_ = info_opt->remaining_weight_g;
}
```

- [ ] **Step 5: Extend tracker with sink registry**

Update `include/filament_consumption_tracker.h`:

```cpp
    /// Register a sink. Tracker takes ownership. If a print is currently active, the
    /// tracker calls snapshot() on the new sink immediately using the current
    /// filament_used_mm. Returns a sink handle for later unregister.
    using SinkHandle = IConsumptionSink*;
    SinkHandle register_sink(std::unique_ptr<IConsumptionSink> sink);

    /// Unregister a sink. Flushes the sink before destruction.
    void unregister_sink(SinkHandle handle);
```

In the private section:

```cpp
    std::vector<std::unique_ptr<IConsumptionSink>> sinks_;
    IConsumptionSink* external_sink_raw_ = nullptr;  // Convenience handle; not ownership.
```

Replace the existing `ExternalSpoolSink external_sink_` member and update `start()` to register it via the new API:

```cpp
void FilamentConsumptionTracker::start() {
    // ... observers as before ...
    if (!external_sink_raw_) {
        auto ext = std::make_unique<ExternalSpoolSink>();
        external_sink_raw_ = ext.get();
        register_sink(std::move(ext));
    }
}
```

Implement:

```cpp
FilamentConsumptionTracker::SinkHandle
FilamentConsumptionTracker::register_sink(std::unique_ptr<IConsumptionSink> sink) {
    IConsumptionSink* raw = sink.get();
    sinks_.push_back(std::move(sink));
    if (active_) {
        float mm = static_cast<float>(lv_subject_get_int(
            get_printer_state().get_print_filament_used_subject()));
        raw->snapshot(mm);
    }
    return raw;
}

void FilamentConsumptionTracker::unregister_sink(SinkHandle handle) {
    auto it = std::find_if(sinks_.begin(), sinks_.end(),
        [handle](const auto& s) { return s.get() == handle; });
    if (it == sinks_.end()) return;
    (*it)->flush();
    sinks_.erase(it);
}
```

Update `on_print_state_changed` and `on_filament_used_changed` to iterate `sinks_` instead of calling `external_sink_` directly.

- [ ] **Step 6: Wire sink registration in `AmsState`**

In `src/printer/ams_state.cpp`, find `add_backend()` (~line 514) and `clear_backends()` (~line 563). After a backend is added, create one `AmsSlotSink` per slot and register with the tracker. On removal, unregister.

Approach: store a `std::map<int /*backend_idx*/, std::vector<FilamentConsumptionTracker::SinkHandle>>` on `AmsState` (or the equivalent backend-lifecycle owner).

```cpp
int AmsState::add_backend(std::unique_ptr<AmsBackend> backend) {
    int idx = /* existing logic that assigns and stores */;
    // existing work...

    AmsBackend* b = get_backend(idx);
    int slot_count = b->get_system_info().total_slots;
    auto& handles = consumption_sinks_[idx];
    handles.reserve(slot_count);
    for (int slot = 0; slot < slot_count; ++slot) {
        auto sink = std::make_unique<helix::AmsSlotSink>(idx, slot);
        handles.push_back(
            helix::FilamentConsumptionTracker::instance().register_sink(std::move(sink)));
    }
    return idx;
}

void AmsState::clear_backends() {
    for (auto& [idx, handles] : consumption_sinks_) {
        for (auto* h : handles) {
            helix::FilamentConsumptionTracker::instance().unregister_sink(h);
        }
    }
    consumption_sinks_.clear();
    // existing work...
}
```

Add the map to the `AmsState` header (`include/ams_state.h`).

- [ ] **Step 7: Build + run sink + tracker tests**

```bash
make test-run 2>&1 | tail -40
```

Expected: new AMS sink tests pass, existing external-spool tests still pass.

- [ ] **Step 8: Commit**

```bash
git add include/consumption_sink.h src/printer/consumption_sink.cpp \
        include/filament_consumption_tracker.h src/printer/filament_consumption_tracker.cpp \
        include/ams_state.h src/printer/ams_state.cpp \
        include/ams_backend_mock.h src/printer/ams_backend_mock.cpp \
        tests/unit/test_consumption_sink_ams.cpp Makefile
git commit -m "feat(filament): AmsSlotSink + per-slot registration

Each registered AMS backend gets one sink per slot, auto-registered
on add_backend() and unregistered on clear_backends(). Gates on
Spoolman linkage + native tracking + unknown weight."
```

---

## Phase 5: Per-Extruder Routing

Route each extruder's `filament_used_mm` delta to the correct sink via the backend's `slot_for_extruder()`. Keep the aggregate-subject path as fallback for backends that return `nullopt`.

**Files:**
- Modify: `include/filament_consumption_tracker.h`, `src/printer/filament_consumption_tracker.cpp`
- Test: extend `tests/unit/test_consumption_sink_ams.cpp` or add a new `test_tracker_routing.cpp`

- [ ] **Step 1: Failing test — per-extruder routing decrements only the mapped slot**

Create `tests/unit/test_tracker_routing.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ams_backend_mock.h"
#include "ams_state.h"
#include "filament_consumption_tracker.h"
#include "helix_test_fixture.h"
#include "printer_state.h"
#include <catch2/catch_test_macros.hpp>

namespace {

struct TrackerRoutingFixture : HelixTestFixture {
    int backend_idx = 0;
    AmsBackendMock* mock = nullptr;

    TrackerRoutingFixture() {
        auto m = std::make_unique<AmsBackendMock>();
        m->set_slot_count_for_testing(4);
        m->set_slot_for_extruder_for_testing([](int i) { return i; });
        mock = m.get();
        backend_idx = AmsState::instance().add_backend(std::move(m));

        for (int s = 0; s < 4; ++s) {
            SlotInfo info = mock->get_slot_info(s);
            info.material = "PLA";
            info.remaining_weight_g = 1000.0f;
            info.total_weight_g = 1000.0f;
            mock->set_slot_info(s, info, false);
        }
        helix::FilamentConsumptionTracker::instance().start();
        // Simulate print start.
        helix::FilamentConsumptionTrackerTestAccess::force_print_state(PrintJobState::PRINTING);
    }

    ~TrackerRoutingFixture() {
        helix::FilamentConsumptionTracker::instance().stop();
    }
};

} // namespace

TEST_CASE_METHOD(TrackerRoutingFixture,
                 "Per-extruder delta routes to mapped slot only",
                 "[tracker][routing]") {
    PrinterState& state = get_printer_state();
    helix::SubjectLifetime lifetime;
    auto* e1 = state.get_extruder_filament_used_subject(1, lifetime);
    REQUIRE(e1);

    // Push 1000mm on extruder1 → slot 1 should decrement, others unchanged.
    lv_subject_set_int(e1, 1000);
    helix::ui::UpdateQueue::instance().drain_queue_for_testing();

    CHECK(mock->get_slot_info(0).remaining_weight_g == 1000.0f);
    CHECK(mock->get_slot_info(1).remaining_weight_g < 1000.0f);
    CHECK(mock->get_slot_info(2).remaining_weight_g == 1000.0f);
    CHECK(mock->get_slot_info(3).remaining_weight_g == 1000.0f);
}
```

(Add `set_slot_count_for_testing` and `set_slot_for_extruder_for_testing` on `AmsBackendMock` — test infrastructure, not production.)

- [ ] **Step 2: Build — expect compile failure / routing logic missing**

```bash
make test 2>&1 | tail -15
```

- [ ] **Step 3: Update tracker to subscribe per-extruder**

In `filament_consumption_tracker.{h,cpp}`, add:

```cpp
    static constexpr int kMaxTrackedExtruders = 4;
    helix::SubjectLifetime extruder_lifetime_;
    std::array<ObserverGuard, kMaxTrackedExtruders> extruder_obs_{};
```

In `start()`, after registering the aggregate observer:

```cpp
for (int idx = 0; idx < kMaxTrackedExtruders; ++idx) {
    auto* subj = get_printer_state().get_extruder_filament_used_subject(idx, extruder_lifetime_);
    if (!subj) continue;
    extruder_obs_[idx] = helix::ui::observe_int_sync<FilamentConsumptionTracker>(
        subj, this,
        [idx](FilamentConsumptionTracker* self, int mm) {
            self->on_extruder_filament_used_changed(idx, mm);
        },
        extruder_lifetime_);
}
```

Add the handler:

```cpp
void FilamentConsumptionTracker::on_extruder_filament_used_changed(int extruder_idx, int mm) {
    if (!active_) return;
    float f_mm = static_cast<float>(mm);

    // Route per-extruder delta to the sink for the mapped slot, if any backend
    // declares a mapping.
    for (int b = 0; b < AmsState::instance().backend_count(); ++b) {
        AmsBackend* backend = AmsState::instance().get_backend(b);
        if (!backend) continue;
        auto slot = backend->slot_for_extruder(extruder_idx);
        if (!slot) continue;
        for (auto& sink : sinks_) {
            auto* ams = dynamic_cast<AmsSlotSink*>(sink.get());
            if (!ams) continue;
            if (ams->backend_index() == b && ams->slot_index() == *slot) {
                ams->apply_delta(f_mm);
            }
        }
    }
}
```

**Change aggregate handler to skip sinks claimed by per-extruder routing:**

`on_filament_used_changed` should now only drive the `ExternalSpoolSink` and any `AmsSlotSink` whose backend does NOT declare `slot_for_extruder()` (default nullopt means "single-extruder backend; use current_slot"):

```cpp
void FilamentConsumptionTracker::on_filament_used_changed(int filament_mm) {
    if (!active_) return;
    float f_mm = static_cast<float>(filament_mm);

    for (auto& sink : sinks_) {
        auto* ams = dynamic_cast<AmsSlotSink*>(sink.get());
        if (!ams) {
            sink->apply_delta(f_mm);  // ExternalSpoolSink always uses aggregate
            continue;
        }
        AmsBackend* backend = AmsState::instance().get_backend(ams->backend_index());
        if (!backend) continue;
        // Skip if ANY extruder on this backend claims a slot mapping — per-extruder
        // routing owns it.
        bool has_mapping = false;
        for (int e = 0; e < kMaxTrackedExtruders; ++e) {
            if (backend->slot_for_extruder(e)) { has_mapping = true; break; }
        }
        if (has_mapping) continue;
        // Single-extruder multi-slot: only the current_slot sink accrues.
        if (ams->slot_index() == backend->get_current_slot()) {
            ams->apply_delta(f_mm);
        }
    }
}
```

- [ ] **Step 4: Add FilamentConsumptionTrackerTestAccess friend struct**

In `tests/unit/test_helpers.h` (or create `tests/unit/filament_consumption_tracker_test_access.h`):

```cpp
#pragma once
#include "filament_consumption_tracker.h"
#include "print_job_state.h"

namespace helix {

struct FilamentConsumptionTrackerTestAccess {
    static void force_print_state(PrintJobState s) {
        FilamentConsumptionTracker::instance().on_print_state_changed(static_cast<int>(s));
    }
    static std::size_t sink_count() {
        return FilamentConsumptionTracker::instance().sinks_.size();
    }
};

} // namespace helix
```

- [ ] **Step 5: Build + run**

```bash
make test-run 2>&1 | tail -40
```

Expected: routing test + existing tests pass.

- [ ] **Step 6: Commit**

```bash
git add include/filament_consumption_tracker.h src/printer/filament_consumption_tracker.cpp \
        tests/unit/test_tracker_routing.cpp \
        tests/unit/filament_consumption_tracker_test_access.h \
        include/ams_backend_mock.h src/printer/ams_backend_mock.cpp Makefile
git commit -m "feat(filament): per-extruder consumption routing

Tracker subscribes to per-extruder filament_used_mm subjects and
routes deltas to the sink whose backend declares a tool→slot
mapping. Single-extruder multi-slot backends fall back to aggregate
stream + current_slot."
```

---

## Phase 6: Backend Overrides (Snapmaker + Toolchanger)

Wire the identity mapping on the two tool-changer backends so per-extruder routing activates.

**Files:**
- Modify: `include/ams_backend_snapmaker.h`, `src/printer/ams_backend_snapmaker.cpp`
- Modify: `include/ams_backend_toolchanger.h`, `src/printer/ams_backend_toolchanger.cpp`

- [ ] **Step 1: Add override on `AmsBackendSnapmaker`**

In `ams_backend_snapmaker.h` public section:

```cpp
    [[nodiscard]] std::optional<int> slot_for_extruder(int extruder_idx) const override;
```

In `ams_backend_snapmaker.cpp`:

```cpp
std::optional<int> AmsBackendSnapmaker::slot_for_extruder(int extruder_idx) const {
    if (extruder_idx < 0 || extruder_idx >= static_cast<int>(system_info_.total_slots)) {
        return std::nullopt;
    }
    return extruder_idx;  // U1 identity mapping: extruder N == slot N.
}
```

- [ ] **Step 2: Same for `AmsBackendToolchanger`**

```cpp
std::optional<int> AmsBackendToolchanger::slot_for_extruder(int extruder_idx) const {
    if (extruder_idx < 0 || extruder_idx >= static_cast<int>(system_info_.total_slots)) {
        return std::nullopt;
    }
    return extruder_idx;
}
```

- [ ] **Step 3: Build + run all tests**

```bash
make test-run 2>&1 | tail -20
```

Expected: all tests pass; no new tests needed (Phase 5's routing test covers the mock path; production backends match the same mapping).

- [ ] **Step 4: Manual verification on Snapmaker U1 (hardware optional, document if deferred)**

If a U1 is available (192.168.30.103): deploy and run a short print with a known starting weight, confirm `remaining_weight_g` on the active slot decreases in the log:

```bash
make snapmaker-u1-docker && SNAPMAKER_U1_HOST=192.168.30.103 make deploy-snapmaker-u1
sshpass -p snapmaker ssh root@192.168.30.103 'tail -f /tmp/helixscreen.log' | grep ConsumptionSink
```

Expected: `[ConsumptionSink:ams:0:<N>] Snapshot ... mm=...` at print start, periodic writes during print. (If no hardware available, note in the commit message that field verification is deferred.)

- [ ] **Step 5: Commit**

```bash
git add include/ams_backend_snapmaker.h src/printer/ams_backend_snapmaker.cpp \
        include/ams_backend_toolchanger.h src/printer/ams_backend_toolchanger.cpp
git commit -m "feat(ams-backend): identity slot_for_extruder on tool-changers

Snapmaker U1 and Klipper tool-changer configs have one spool per
tool — extruder N maps to slot N. Activates per-extruder consumption
tracking for these backends."
```

---

## Phase 7: UI Polish

Blank-render `-1` in the edit modal; verify the weight input accepts ≥5000g.

**Files:**
- Modify: `src/ui/ui_ams_edit_modal.cpp` (and its XML component if applicable)

- [ ] **Step 1: Locate the weight field**

```bash
grep -n "remaining_weight_g\|weight" src/ui/ui_ams_edit_modal.cpp | head
grep -rn "weight" ui_xml/components/ | grep -i "modal\|edit" | head
```

- [ ] **Step 2: Render empty on unknown**

Where the modal populates the weight field from `working_info_.remaining_weight_g`, wrap:

```cpp
if (working_info_.remaining_weight_g < 0.0f) {
    lv_textarea_set_text(weight_field_, "");  // or equivalent for roller/spinbox
} else {
    char buf[32];
    snprintf(buf, sizeof(buf), "%.0f", working_info_.remaining_weight_g);
    lv_textarea_set_text(weight_field_, buf);
}
```

(Confirm widget type — if it's a spinbox/roller, use the equivalent API to clear/set a "blank" state. If the widget has no blank state, pick a neutral default like showing `0` with a placeholder label.)

- [ ] **Step 3: Verify weight cap**

If the field uses a roller with a max value, confirm it supports ≥5000g. Search:

```bash
grep -n "max_value\|range\|3000\|2000\|1000" src/ui/ui_ams_edit_modal.cpp ui_xml/components/ams_edit*.xml
```

If capped below 5000, raise to 10000 (future-proofs for 5kg industrial spools).

- [ ] **Step 4: XML-only changes don't need a rebuild ([L031])**

If you modified XML only, restart the app. If you modified `.cpp`, rebuild:

```bash
make -j
```

- [ ] **Step 5: Manual verification**

```bash
./build/bin/helix-screen --test -vv -p ams 2>&1 | tee /tmp/test.log
```

In the mock AMS panel: open the slot edit modal. Verify:
- Slot with no weight set → weight field shows blank (not `-1`).
- Roller/input accepts at least `5000`.

- [ ] **Step 6: Commit**

```bash
git add src/ui/ui_ams_edit_modal.cpp ui_xml/components/ams_edit*.xml
git commit -m "fix(ui): blank weight field for unknown spool (not -1)

Also raises the weight cap to 10000g to accommodate 5kg industrial
spools. -1 is a sentinel, not a user-facing value."
```

---

## Phase 8: Integration Smoke + Gating Matrix

Final pass: comprehensive gating tests + end-to-end smoke.

**Files:**
- Extend: `tests/unit/test_filament_consumption_tracker.cpp` or create `test_tracker_integration.cpp`

- [ ] **Step 1: Gating matrix test**

```cpp
TEST_CASE_METHOD(TrackerRoutingFixture,
                 "Gating matrix: slot with spoolman_id != 0 not tracked",
                 "[tracker][gating]") {
    SlotInfo info = mock->get_slot_info(0);
    info.spoolman_id = 42;
    mock->set_slot_info(0, info, false);

    float before = mock->get_slot_info(0).remaining_weight_g;
    PrinterState& s = get_printer_state();
    helix::SubjectLifetime lt;
    lv_subject_set_int(s.get_extruder_filament_used_subject(0, lt), 1000);
    helix::ui::UpdateQueue::instance().drain_queue_for_testing();

    REQUIRE(mock->get_slot_info(0).remaining_weight_g == before);
}

TEST_CASE_METHOD(TrackerRoutingFixture,
                 "Gating matrix: native tracking backend not decremented",
                 "[tracker][gating]") {
    mock->set_tracks_consumption_natively_for_testing(true);
    // Re-snapshot: force new print cycle.
    helix::FilamentConsumptionTrackerTestAccess::force_print_state(PrintJobState::COMPLETE);
    helix::FilamentConsumptionTrackerTestAccess::force_print_state(PrintJobState::PRINTING);

    float before = mock->get_slot_info(0).remaining_weight_g;
    PrinterState& s = get_printer_state();
    helix::SubjectLifetime lt;
    lv_subject_set_int(s.get_extruder_filament_used_subject(0, lt), 1000);
    helix::ui::UpdateQueue::instance().drain_queue_for_testing();

    REQUIRE(mock->get_slot_info(0).remaining_weight_g == before);
}

TEST_CASE_METHOD(TrackerRoutingFixture,
                 "Mid-print edit rebaselines sink",
                 "[tracker][gating]") {
    PrinterState& s = get_printer_state();
    helix::SubjectLifetime lt;
    auto* e0 = s.get_extruder_filament_used_subject(0, lt);

    lv_subject_set_int(e0, 500);
    helix::ui::UpdateQueue::instance().drain_queue_for_testing();
    float after_first = mock->get_slot_info(0).remaining_weight_g;
    REQUIRE(after_first < 1000.0f);

    // User edits mid-print to 800g.
    SlotInfo info = mock->get_slot_info(0);
    info.remaining_weight_g = 800.0f;
    mock->set_slot_info(0, info, false);

    // Next tick should rebaseline (no decrement yet since delta check triggers rebase).
    lv_subject_set_int(e0, 600);
    helix::ui::UpdateQueue::instance().drain_queue_for_testing();
    REQUIRE(mock->get_slot_info(0).remaining_weight_g == 800.0f);

    // Further extrusion decrements from 800.
    lv_subject_set_int(e0, 1600);  // +1000mm from rebase
    helix::ui::UpdateQueue::instance().drain_queue_for_testing();
    REQUIRE(mock->get_slot_info(0).remaining_weight_g < 800.0f);
    REQUIRE(mock->get_slot_info(0).remaining_weight_g > 796.0f);
}
```

- [ ] **Step 2: Run full test suite**

```bash
make test-run 2>&1 | tail -40
```

Expected: all pass. Baseline (Phase 0) count + new tests.

- [ ] **Step 3: Run the device manually if available (Phase 6 Step 4 repeated end-to-end)**

Skip if hardware not available.

- [ ] **Step 4: Commit**

```bash
git add tests/unit/test_filament_consumption_tracker.cpp tests/unit/test_tracker_routing.cpp
git commit -m "test(filament): gating matrix + mid-print rebaseline

Covers: spoolman-linked slot skipped, native-tracking backend
skipped, mid-print weight edit rebaselines sink instead of
decrementing through the new value."
```

---

## Final Review

- [ ] **Step 1: Spec → Plan coverage check**

Re-read `docs/devel/specs/2026-04-19-filament-consumption-tracker-unified-design.md`. Each design section should map to a phase:

- §1 Architecture → Phase 1 (interface) + Phase 4 (multi-sink) + Phase 5 (routing)
- §2 Data source → Phase 3 (per-extruder subjects)
- §3 Backend capabilities → Phase 2 (virtuals) + Phase 6 (overrides)
- §4 Gating policy → Phase 4 (in-sink) + Phase 8 (tests)
- §5 Sink implementations → Phases 1, 4
- §6 Lifecycle → Phase 1 (state handler) + Phase 5 (per-extruder hookup)
- §7 Persistence → Phases 1, 4 (via existing stores)
- §8 UI → Phase 7
- §9 Testing → inline + Phase 8

No design section is unaddressed.

- [ ] **Step 2: Squash follow-ups if needed**

If adjacent commits have become too granular (e.g., Phase 6 is a one-line change), consider squashing into Phase 5's routing commit before merging. Don't rewrite public history.

- [ ] **Step 3: PR preparation**

```bash
git log --oneline feature/filament-consumption-tracker-unified..HEAD
make test-run
```

Ready for PR once all phases green.

---

## Notes for Implementers

- **L077 applies throughout**: per-extruder subjects are dynamic. Always pass `SubjectLifetime` to `get_extruder_filament_used_subject` and the observer factory. Don't cache raw `lv_subject_t*` across reconnections.
- **L065 applies**: no test-only public methods on production classes. Use `FilamentConsumptionTrackerTestAccess` friend struct for private access. For mocks, setters like `set_tracks_consumption_natively_for_testing` are fine on the mock itself.
- **L031 applies**: XML-only edits (Phase 7) don't need a rebuild.
- Throttle interval stays at 60s (matches external). Don't tune unless telemetry indicates write storms.
- Don't touch AFC's backend in this PR — `tracks_consumption_natively()` defaults to false, which is correct until field-testing confirms its data stream.
- Spoolman write-back is explicitly out of scope (spec §Non-goals). Don't add it "while you're in there."
