# Hidden Tests Tracker

**Last Updated:** 2026-08-08
**Total Hidden Tests:** 90

Hidden tests are excluded from normal runs using Catch2's `[.]` tag prefix. They exist for legitimate reasons (benchmarks, stress tests, destructive global-state cycles, tests that need the `ui_xml/` tree on disk) and should be run manually when relevant.

```bash
# Run all hidden tests
./build/bin/helix-tests "[.]"

# Run by category
./build/bin/helix-tests "[.ui_integration]"
./build/bin/helix-tests "[.xml_required]"
./build/bin/helix-tests "[.benchmark]"
./build/bin/helix-tests "[.slow]"
```

> **Run from the repo root.** Every `[.ui_integration]` and `[.xml_required]` test reads `ui_xml/` by relative path. From any other cwd they fail or skip en masse, which reads as a regression.

> **Scope:** this tracker counts `[.]`-prefixed hidden tests in compiled `tests/**/*.cpp`. Two other categories are tracked separately and are NOT in the count below: `[!mayfail]` tests (which run but are allowed to fail) and any `*.cpp.disabled` files (excluded from the build entirely).

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
| `test_action_prompt_modal_stress.cpp` | 8 | L081 crash family (#875 SIGBUS, #877 SEGV, #906 cluster): rapid show/hide, burst cadence, stacked modals, click-driven teardown, single-tick async-delete drain, `queue_update` racer | **No.** In-process race harness; out-of-process `ctl` cannot drive it |
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
./build/bin/helix-tests "[.]" --list-tests | tail -1
```

Expected: `90 test cases`.
