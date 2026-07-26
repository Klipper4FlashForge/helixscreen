---
name: helix-threading
description: >
  HelixScreen threading & lifecycle safety — triggers when editing code in src/ that crosses the
  main-thread/background-thread boundary: WebSocket/libhv callbacks, src/network/ HTTP workers,
  src/bluetooth/ DBus threads, or src/printer/ background state updates. Also for UpdateQueue/queue_update
  (include/ui_update_queue.h), AsyncLifetimeGuard (include/async_lifetime_guard.h), SubjectLifetime,
  ObserverGuard (include/ui_observer_guard.h), safe_delete_deferred/safe_clean_children (include/ui_utils.h),
  StaticSubjectRegistry shutdown ordering (include/static_subject_registry.h), HttpExecutor thread pools
  (include/http_executor.h), or any code touching lv_subject_t from non-main threads.
---

# HelixScreen Threading & Lifecycle Safety

**Read `docs/devel/THREADING.md` before writing the code.** It is the single source of truth
for these rules — full patterns, code examples, enforcement details, and a symptom index.
This file exists only to surface the invariants when you touch a threading-adjacent file, and
to route you to the right section.

Threading violations in `src/network/`, `src/bluetooth/`, `src/printer/`, and lifecycle bugs in
`src/ui/` account for the majority of field crashes on K1/AD5M/CC1. Every rule below cost a
production crash to learn.

## The invariants

Each fails silently at compile time and crashes later, usually on a customer's printer.

| # | Never | Instead | Section |
|---|-------|---------|---------|
| 1 | Call `lv_*` / `lv_subject_set_*` from a background thread | `helix::ui::queue_update(...)` | §1 |
| 2 | Write bare `if (tok.expired()) return;` on a bg thread, then touch `this` | `lifetime_.bg_cb(tag, fn)` or `tok.defer(tag, fn)` | §2 |
| 3 | Delete a widget synchronously inside a queued callback | `safe_delete_deferred` / `lv_obj_delete_async` / `safe_clean_children` | §3 |
| 4 | Observe a dynamic subject without a **member** `SubjectLifetime` | Parallel member `ObserverGuard` + `SubjectLifetime`; reset lifetime first | §5 |
| 5 | `std::thread(...).detach()` for one-shot work | `HttpExecutor::fast()/slow()`, `BusThread`, or try/catch | §8 |
| 6 | `ObserverGuard::release()` in normal cleanup | `reset()` | §6 |
| 7 | Delete container children inside an input event handler | Null the pointer, let the rebuild clean | §9 |
| 8 | Skip self-registering `deinit_subjects()` in `init_subjects()` | `StaticSubjectRegistry::register_deinit(...)` | §7 |

Two of these are gated at commit time by `scripts/quality-checks.sh`:
`scripts/check_l081_anti_pattern.py` (#2) and `scripts/check_subscription_null_safety.py`.

## Two things that surprise people

**`lifetime_.defer` does NOT escape the UpdateQueue batch.** It is a thin wrapper around
`queue_update` — the callback fires in the *next* `process_pending` tick, which is still a
batch that may contain other sync deletions. The generation guard protects `this` from
use-after-free, not the event list from corruption. (§3)

**From a background thread, `tok.defer()` — never `lifetime_.defer()`.** The latter reads
`this->lifetime_`, which is the #707 TOCTOU race. `lifetime_.defer()` is only safe on the main
thread. (§2)

## Section map — `docs/devel/THREADING.md`

| Section | Covers |
|---------|--------|
| §1 LVGL is single-threaded | UpdateQueue, main-loop order, why not `lv_async_call`, backend pattern, threading model |
| §2 Async callback safety | `AsyncLifetimeGuard`, `bg_cb` vs `tok.defer`, L081 enforcement layers, release-build carve-out |
| §3 No sync widget deletion | What counts as queued, banned→replacement table, true escape routes |
| §4 `ScopedFreeze` | drain+destroy, buffer-not-drop, why `defer_critical` was removed |
| §5 Subject lifecycle | static vs dynamic, member-pairing rule, reset ordering, collections |
| §6 Observers | `observer_factory.h` factories, deferred-by-default (#82), `reset()` vs `release()` (#579) |
| §7 Shutdown | registries, ordering, self-registration, `deinit_subjects()` |
| §8 Threads and pools | no detached spawns, workload→pool table |
| §9 Input event processing | no sync deletion during `indev` dispatch |
| §10 Timers | `LvglTimerGuard` |
| §11 Testing | fixture hierarchy, cleanup order, observer immediate-fire gotcha, asan/tsan |
| §12 Symptom index | "what you're seeing" → cause → fix |
