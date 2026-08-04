# tests/CLAUDE.md — Writing HelixScreen Tests

## Layout

| Path | Contents |
|------|----------|
| `tests/unit/` | Catch2 unit tests. **Auto-globbed** (`Makefile:904`) — drop a `.cpp` in and it builds |
| `tests/unit/application/` | App-lifecycle tests (separate glob, own include path) |
| `tests/mocks/` | Mock implementations |
| `tests/shell/` | `bats` tests — installer, lint gates, packaging |
| `tests/ui/` | Out-of-process pytest driving the real binary via `helix-screen ctl` |
| `tests/test_helpers/` | `*TestAccess` friend classes for reaching private members |

```bash
make test                      # build tests only (does NOT run them)
make test-run                  # build AND run in parallel
./build/bin/helix-tests "[tag]"  # run one tag
```

`make -j` builds **only** the app binary. Run `make test` before `./build/bin/helix-tests`
or you are testing a stale binary.

---

## Fixtures — inherit, don't hand-roll

```
HelixTestFixture          drains UpdateQueue, resets language, clears ModalStack (ctor + dtor)
└── LVGLTestFixture       + LVGL init, headless display, UpdateQueue init/shutdown
    └── LVGLUITestFixture + XML component registration
XMLTestFixture            per-instance PrinterState / MoonrakerClient / MoonrakerAPI
```

**Always derive from one of these.** A hand-rolled fixture that calls `lv_init_safe()` and
`lv_display_create()` looks sufficient and is not — it silently skips the UpdateQueue
lifecycle, and anything that defers work through `ui_queue_update()` **never runs**. There is
no error; the code under test simply does nothing and the test fails on a value that looks
like a logic bug.

That is the trap: `NavigationManager::push_overlay()` queues its entire body through
`UpdateQueue`. With an uninitialised queue, `push_overlay()` + `drain()` is a no-op pair, so a
width/lifecycle assertion fails as if the production logic were wrong.

```cpp
class MyFixture : public LVGLTestFixture {   // ← not a bare class
  public:
    MyFixture()  { /* your setup */ }
    ~MyFixture() override { helix::ui::UpdateQueue::instance().drain(); }
};
```

### Deferred work needs an explicit drain

Anything routed through `ui_queue_update()` / `tok.defer()` runs on the next queue tick, not
at call time. Drain before asserting:

```cpp
nav.push_overlay(widget);
helix::ui::UpdateQueue::instance().drain();
```

`process_lvgl(ms)` (on `LVGLTestFixture`) additionally pumps timers and animations.

### `process_lvgl()` moves *virtual* time — it is not a wall-clock wait

The test binary builds its display with a bare `lv_display_create()`. No driver, so nothing
ever calls `lv_tick_set_cb()` and `lv_tick_get()` returns exactly what `lv_tick_inc()` has
been fed. `process_lvgl()` is the thing feeding it, and it barely sleeps: 1ms of real time per
5ms step, and **zero** below `ms <= 50`, which skips the sleep entirely.

So a loop that counts nominal milliseconds toward a timeout is not waiting for anything:

```cpp
int waited = 0;
while (!done && waited < 120000) { process_lvgl(100); waited += 100; }   // ← 24s, at best
```

That burns a two-minute budget in seconds and never meaningfully yields to the thread it is
watching. The symptom is baffling rather than obvious: the test runs on past the assertion,
the fixture destructor tears down the screen, and teardown effects appear to happen mid-test.

Two correct answers, in order of preference:

1. **Join the worker, then drain.** Deterministic, no timeout, no flake under CI load.
   `ActivePrintMediaAsyncFixture::drain()` in `test_active_print_media_manager.cpp` is the
   model — `ThumbnailProcessor::wait_for_completion()` then drain, repeated, since a drained
   callback can commit more pool work.
2. **`wait_until(pred, timeout_ms)`** on `LVGLTestFixture` when there is no joinable handle.
   Real `steady_clock` deadline, real sleeps, and it advances the tick each pass so timers and
   animations still come due.

The frozen-clock trap has a mirror image, which is why `wait_until` lives on the fixture: a
wait that sleeps on the real clock *without* `lv_tick_inc()` leaves LVGL's clock stopped.
`lv_async_call` one-shots (period 0) still fire, but no timer with a real period ever comes
due, however long you wait.

---

## LVGL traps in tests

**`lv_obj_get_width()` reads the COMPUTED coord, not what you just set.**
`lv_obj_set_width()` writes a style; the computed value only refreshes on a layout pass. A
freshly created widget reports LVGL's default (160) no matter what you set. Force it:

```cpp
lv_obj_set_width(w, 900);
lv_obj_update_layout(w);          // ← without this, get_width() still says 160
CHECK(lv_obj_get_width(w) == 900);
```

Same applies to heights, content sizes, and anything measured off a flex/grid parent.

**Seed `NavigationManager` the way the app does.** `panel_stack_[0]` always holds the active
root panel in production, and overlay code reads `panel_stack_.back()` to find what is
beneath it. If you only ever `push_overlay()`, the *second* push still looks like the first:

```cpp
std::array<lv_obj_t*, UI_PANEL_COUNT> panels{};
for (auto& p : panels) p = lv_obj_create(lv_screen_active());
NavigationManager::instance().set_panels(panels.data());   // seeds panel_stack_[0]
NavigationManager::instance().set_active(PanelId::Advanced);
```

**Overlays must be registered before push.** `register_overlay_instance(widget, lifecycle)` —
or `(widget, nullptr)` for an intentional lifecycle-less overlay. Tests run with
`HELIX_STRICT_OVERLAY_CHECK`, so an unregistered push **aborts** rather than warning.

---

## What counts as a real test

A test must FAIL if the feature is removed. After writing one, **mutate the implementation and
watch it go red** — a test that passes against broken code is worse than no test.

| ❌ | ✅ |
|----|----|
| `REQUIRE(true)`, `REQUIRE(ptr != nullptr)` and nothing else | assert the value the feature computes |
| Happy path only | edge cases, error paths, both sides of a branch |
| Asserting a constant you also hardcoded in the test | derive the expectation independently |

Prefer extracting the rule as a **pure function** and testing that without LVGL — see
`include/overlay_class.h` + `tests/unit/test_overlay_width_class.cpp`. Then test the wiring
separately (`test_overlay_width_push.cpp`). Pure-logic tests are fast, total, and survive
refactors of the widget layer.

### Tag conventions

`[core]`, `[navigation]`, `[ams]`, `[threading]`, `[compile][drift]`, `[slow]`, and a bare
issue number for bug-fix tests (`[1178]`) so the whole fix runs with
`./build/bin/helix-tests "[1178]"`.

---

## Test isolation

Fixture ctor **and** dtor call `reset_all()`. Cross-test leaks through `UpdateQueue` are
ratcheted by `scripts/check_update_queue_leaks.py` — if you add a test that queues callbacks
without draining, the nightly leak gate will name it.

XML subjects still register into LVGL's **global** scope (per-test scopes are blocked by LVGL
internals). Each test refreshes them with `init_subjects(true)`.

---

## Lint gates

Gates live in `scripts/check_*.py` and run from `scripts/quality-checks.sh`. Every gate gets a
**meta-test** in `tests/shell/test_*_gate.bats` pinning both halves of its contract: the shape
it must catch, and the idioms it must stay quiet about. A gate that fires on legitimate code
gets switched off, so the silent cases matter as much as the loud ones.

Verify a new gate by running it against known-good and known-bad fixtures before wiring it in.
