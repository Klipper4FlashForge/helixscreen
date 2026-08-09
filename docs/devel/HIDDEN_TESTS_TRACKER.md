# Hidden Tests Tracker

**Last Updated:** 2026-08-08
**Total Hidden Tests:** 89 (Catch2's own count — `./build/bin/helix-tests "[.]" --list-tests | tail -1`)

Hidden tests are excluded from normal runs using Catch2's `[.]` tag prefix. They exist for legitimate reasons (benchmarks, stress tests, destructive global-state cycles, tests that need the `ui_xml/` tree on disk) and should be run manually when relevant.

```bash
make test-hidden                                  # the whole hidden set, serially, from the repo root
make test-hidden HIDDEN_FILTER='[.ui_integration]'  # one slice
make test-hidden-list                             # inventory without running

# Or drive the binary directly — but only from the repo root (see below)
./build/bin/helix-tests "[.]"
./build/bin/helix-tests "[.xml_required]"
```

> **`[.]` selects the whole hidden set, not just the tests literally tagged `[.]`.** Catch2 appends a bare `.` tag to every hidden test at registration (`TestCaseInfo` ctor: `if (isHidden()) internalAppendTag("."_sr)`), and the spec parser splits a `[.foo]` pattern into a required `.` plus a required `foo`. So one pattern covers `[.ui_integration]`, `[.xml_required]`, `[.disabled]`, `[.skip]`, `[.slow]`, `[.benchmark]`, `[.memprobe]` and `[.integration]` alike.

> **Run from the repo root.** Every `[.ui_integration]` and `[.xml_required]` test reads `ui_xml/` by relative path. From any other cwd they fail or skip en masse, which reads as a regression. `make test-hidden` `cd`s to `$(CURDIR)` for exactly this reason.

> **`make test-hidden` is not wired into `make test-run` or `scripts/quality-checks.sh`.** Making the hidden set runnable is a separate decision from making it mandatory.

> **Scope:** this tracker counts `[.]`-prefixed hidden tests in compiled `tests/**/*.cpp`. Two other categories are tracked separately and are NOT in the count below: `[!mayfail]` tests (which run but are allowed to fail) and any `*.cpp.disabled` files (excluded from the build entirely).

---

## Current state of the hidden set

Measured on macOS (Darwin 24.3.0, arm64), serial, from the repo root, ~65s:

```
test cases:   89 |   41 passed |  6 failed | 42 skipped
```

The 42 "skipped" are `SKIP()` calls inside otherwise-passing cases (absent hardware, absent XML component, platform guards) — not silent failures.

The 6 failures are pre-existing and environmental. Both reproduce **in isolation**, so neither is an ordering artefact:

| Test(s) | File | Why it fails |
|---------|------|--------------|
| 5 cases: *Backend initialization state*, *Network scanning lifecycle*, *Scan callback preservation*, *WiFi edge cases*, *WiFi network information* | `test_wifi_manager.cpp` (`[.disabled]`) | macOS CoreWLAN refuses to start without Location Services permission: `[WiFiMacOS] System prerequisites check failed: Location permission not determined`. This is exactly why the file is tagged `[.disabled]`. Grant the permission or run on Linux. |
| *ams_slot: material label binds to subject* | `test_ui_ams_slot.cpp:336` (`[.skip]`) | The label renders `"--"` where the test expects `"PLA"`. These are the superseded `ams_slot` binding tests (see the row in the summary table); the assertion has drifted from the shipped widget. |

**`make test-hidden` is therefore red today, on purpose.** The filter was not narrowed to hide these and the two known-bad groups were not gated out — a hidden test that no longer passes is a finding, and burying it in an exclusion list is how it stays buried. Fix or retire them before anyone considers making this target mandatory.

---

## Summary

| Tag | Count | File(s) | Description |
|-----|------:|---------|-------------|
| `[.xml_required]` | 41 | `test_ui_panel_bindings.cpp` | Panel subject-binding assertions needing the XML tree |
| `[.ui_integration]` | 17 | 5 files (below) | Real widget tree built from `ui_xml/` |
| `[.disabled]` | 11 | `test_wifi_manager.cpp` | macOS WiFi backend needs Location permission |
| `[.]` (generic) | 9 | `test_config.cpp`, `test_moonraker_client_robustness.cpp`, `test_moonraker_api_exclude_object.cpp`, `test_nozzle_render_gallery.cpp` | Destructive global state, event-loop concurrency, BMP-writing gallery |
| `[.skip]` | 7 | `test_ui_ams_slot.cpp` | Superseded `ams_slot` binding tests |
| `[.slow]` | 2 | `test_async_callback_safety.cpp` | Stress tests, too slow for CI |
| `[.memprobe]` | 1 | `test_gcode_memory_probe.cpp` | Memory measurement probe |
| `[.integration]` | 1 | `test_moonraker_client_security.cpp` | Timeout-callback deadlock check |
| `[.benchmark]` | 1 | `test_wizard_connection.cpp` | Performance measurement |

---

## `[.ui_integration]` (17 tests, 5 files)

These build a real widget tree from `ui_xml/`. They are hidden because they depend on the working directory, **not** because they are broken — all 17 pass when run from the repo root.

| File | Tests | What it covers | Replaced by `tests/ui/`? |
|------|------:|----------------|--------------------------|
| `test_action_prompt_modal_stress.cpp` | 8 | L081 crash family (#875 SIGBUS, #877 SEGV, #906 cluster): rapid show/hide, burst cadence, stacked modals, click-driven teardown, single-tick async-delete drain, `queue_update` racer. Every case ends on a **widget census** (recursive count over the screen + `lv_layer_top()` + `lv_layer_sys()`), `dialog() == nullptr` / `is_visible() == false`, `UpdateQueue::pending_count() == 0`, and a heap-growth ceiling. The top layer is the load-bearing part: `safe_delete_deferred_raw()` reparents there before `lv_obj_delete_async()`, so a dropped async delete strands the tree **off** the screen where a screen-child assertion cannot see it | **No.** In-process race harness; out-of-process `ctl` cannot drive it |
| `test_wizard_connection_ui.cpp` | 5 | Wizard connection step XML structure: widget names, title text, flex layout | **No.** Wizard is pre-first-boot; `ctl` cannot reach it |
| `test_wizard_step_stress.cpp` | 2 | Wizard step-transition churn (bounce 2↔3, full sweep) | **No.** Same reason |
| `test_spaghetti_detection_modal.cpp` | 1 | `SpaghettiDetectionModal` resume/abort/tune callbacks + self-delete on hide; Tune deliberately does *not* hide | **No.** Sole coverage of this modal |
| `test_gcode_error_routing_e2e.cpp` | 1 | Uncoded jam while paused renders a modal holding the full untruncated message | **No.** File also holds a running `[ui_integration]` sibling test |

`tests/ui/` (pytest driving a live binary via `helix-screen ctl`, added 2026-07-25) is **not** a replacement for any of these. It is a harness-validation suite — freeze/reset/text/navigation/screenshot mechanics — plus golden screenshots for 8 base panels. It has no wizard, modal, action-prompt, or crash-race coverage.

---

## Destructive Global State (`test_config.cpp`)

| Line | Test | Tags |
|------|------|------|
| 2703 | StaticSubjectRegistry supports deinit/re-init cycles | `[.][core][registry]` |
| 2717 | StaticSubjectRegistry deinit_all runs callbacks in LIFO order | `[.][core][registry]` |

Hidden because they tear down and rebuild global subject-registry state, which is unsafe to interleave with the rest of the suite. Run in isolation.

---

## Tag Conventions

| Tag | Meaning | When to Use |
|-----|---------|-------------|
| `[.]` | Generic hidden | Crashes, instability, destructive global state, catch-all |
| `[.network]` | Requires network | Live server, hardware |
| `[.benchmark]` | Performance measurement | Timing-sensitive |
| `[.slow]` | Too slow for normal runs | >5 seconds execution |
| `[.disabled]` | Temporarily disabled | Awaiting fix or decision |
| `[.flaky]` | Intermittent failures | Race conditions, timing |
| `[.ui_integration]` | Needs `ui_xml/` on disk | Real widget tree, cwd-dependent |
| `[.xml_required]` | Needs `ui_xml/` on disk | Subject-binding assertions |

---

## Verification

```bash
make test-hidden-list                                  # or, equivalently:
./build/bin/helix-tests "[.]" --list-tests | tail -1
```

Expected: `89 matching test cases`. Catch2's count is the authority here — grepping the
sources for `[.` over-counts, because several tests carry a literal `"[.]"` inside an
unrelated string (regex fixtures in the Klipper-config parser tests, for one).
