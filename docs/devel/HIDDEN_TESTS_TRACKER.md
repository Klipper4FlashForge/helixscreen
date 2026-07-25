# Hidden Tests Tracker

**Last Updated:** 2026-07-22
**Total Hidden Tests:** 5

Hidden tests are excluded from normal runs using Catch2's `[.]` tag prefix. They exist for legitimate reasons (benchmarks, stress tests, destructive global-state cycles) and should be run manually when relevant.

```bash
# Run all hidden tests
./build/bin/helix-tests "[.]"

# Run by category
./build/bin/helix-tests "[.benchmark]"
./build/bin/helix-tests "[.slow]"
```

> **Scope:** this tracker counts `[.]`-prefixed hidden tests in compiled `tests/unit/*.cpp`. Two other categories are tracked separately and are NOT in the count below: `[!mayfail]` tests (which run but are allowed to fail) and any `*.cpp.disabled` files (excluded from the build entirely).

---

## Summary

| Tag | Count | Description |
|-----|-------|-------------|
| `[.slow]` | 2 | Stress tests, too slow for CI |
| `[.benchmark]` | 1 | Performance measurement |
| `[.]` (generic) | 2 | Destructive global-state (StaticSubjectRegistry deinit/re-init) |

---

## Inventory

### Stress Tests (2 tests, `[.slow]`)

| File | Line | Test | Tags |
|------|------|------|------|
| `test_async_callback_safety.cpp` | 727 | Stress: rapid create/destroy with pending callbacks | `[stress][async][thread][.slow]` |
| `test_async_callback_safety.cpp` | 748 | Stress: concurrent object creation and destruction | `[stress][async][thread][.slow]` |

Heavy stress tests that take too long for normal CI.

### Benchmark (1 test, `[.benchmark]`)

| File | Line | Test | Tags |
|------|------|------|------|
| `test_wizard_connection.cpp` | 342 | Wizard Connection: Performance | `[wizard][connection][performance][.benchmark]` |

Performance benchmark, excluded from fast CI runs.

### Destructive Global State (2 tests, `[.][core][registry]`)

| File | Line | Test | Tags |
|------|------|------|------|
| `test_config.cpp` | 2341 | StaticSubjectRegistry supports deinit/re-init cycles | `[.][core][registry]` |
| `test_config.cpp` | 2355 | StaticSubjectRegistry deinit_all runs callbacks in LIFO order | `[.][core][registry]` |

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

---

## Verification

```bash
grep -rn '\[\.' tests/unit/*.cpp | grep 'TEST_CASE' | grep -v '^ *//' | wc -l
```

Expected output: `5`
