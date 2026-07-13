# ESP32 Native Port Feasibility Audit — Results

**Plan:** `docs/devel/specs/plans/2026-06-10-esp32-native-audit.md` (Phase 0 of the ESP32 display program)
**Hardware:** BTT K-Touch (ESP32-S3R8, 8MB octal PSRAM, 16MB flash, 800×480 RGB panel — see `printer-research/BTT_K_TOUCH_HARDWARE.md`)
**Toolchain:** ESP-IDF v5.5 (release branch), `-Os`, C++ exceptions on
**Constraint (2026-07-13):** S3 is the fixed target — no P4 escape hatch. BTT wants broader appeal for existing stock. Feature gates are the expected design.

## Task 1 — helix-xml + LVGL compile/link/boot ✅ PASS (2026-07-13)

`firmware/native-audit/` compiles the repo's **LVGL 9.5 submodule (with our patches) and `lib/helix-xml` completely unmodified**, via relative paths, against ESP-IDF. Verified end-to-end on a physical K-Touch: XML component with design-token consts, styled nested widgets, and live `bind_text`/`bind_value` subject bindings rendering at 800×480, visually confirmed stable and artifact-free.

### Numbers

| Measurement | Value |
|---|---|
| App image (LVGL + helix-xml + esp_lcd + IDF runtime, -Os) | **733KB** (674KB before heap-poisoning debug config) |
| helix-xml files compiling clean | 42/42 (zero source changes) |
| LVGL files compiling clean | all (~600, internal xml/expat excluded as in Makefile) |
| `lv_xml_register_component_from_data` | 2.1ms |
| `lv_xml_create` (card + 2 labels + bar, bindings) | 10.3ms |
| Internal RAM free, full stack up | 295KB |
| PSRAM free, full stack up (incl. 768KB FB + 2×128KB draw buffers) | 7.36MB |
| Heap over 10s of 1Hz subject updates | flat (no leaks) |

### Adaptations required (all config/boundary — zero source edits)

1. `lv_conf.h` copy: `LV_COLOR_DEPTH 16` (same as embedded Linux targets); assert handler → default (app-layer header); default font → built-in Montserrat 14 (app fonts are Makefile-compiled objects).
2. `-DLV_KCONFIG_IGNORE` — helix-xml ships `lv_conf_internal.h` without the sibling `lv_conf_kconfig.h`; we configure via lv_conf.h, not Kconfig.
3. `anomaly_stub.c` — one-function stub for the `helix_lvgl_anomaly` telemetry hook our LVGL patches reference (app provides it on Linux).
4. `LV_DRAW_BUF_STRIDE_ALIGN 1` — the repo's 16 is an ARM-SIMD tuning; `esp_lcd_panel_draw_bitmap` expects packed rows. With 16, partial redraws of non-multiple-of-8px areas skew per-row (trapezoid artifacts, confirmed on device).
5. `LV_USE_OS = LV_OS_PTHREAD` kept **unchanged** and works on ESP-IDF pthreads — deliberate: this is the seam the app's std::thread usage rides on in Phase 2.

### Traps discovered (worth their weight for Phase 2)

- **`lv_init()` does NOT call `lv_xml_init()`** — helix-xml is external to LVGL; the app calls it during startup. Skipping it leaves `component_scope_ll` uninitialized, which presents as heap-corruption-shaped TLSF asserts several calls later (three different crash signatures before root-causing). Any ESP32 entry point must call `lv_xml_init()` right after `lv_init()`.
- **Registering subjects requires the "globals" scope** (created by `lv_xml_init`); `lv_xml_register_component_from_data("globals", …)` *dereferences* the existing scope and crashes if init was skipped.
- **RGB panel needs bounce buffers** (10 lines, as stock firmware and PandaTouch reference use). Direct PSRAM scanout visibly desyncs — whole-frame jumping/wrapping — whenever redraw traffic competes for PSRAM bandwidth.
- **ESP-IDF main task stack (3.5KB default) is far too small** for the XML parse path; 32KB works. Any task calling into helix-xml registration/creation needs a real stack — relevant to the plan's "HttpExecutor pools shrink to small stacks" note: *not* for XML-touching tasks.
- CH340 UART: flash/monitor at 460800 max (921600 corrupts). RTS pulse = programmatic reset; `idf.py monitor` unavailable workflow-wise, use raw pyserial capture.

### RAM trim opportunities for the port (Preston 2026-07-13: "we know the actual resolution — cut fonts, cut images, etc.")

Fixed 800×480 RGB565 and a known product surface let us cut aggressively in Phase 2:

- **Draw buffers:** 2×80-line partial buffers = 256KB PSRAM today; 2×40-line = 128KB is likely fine at this PCLK. Measure tearing/fps trade.
- **Fonts:** compile in only the sizes the 800×480 layout actually uses (the app's responsive breakpoints collapse to one profile). No runtime TTF, no unused Montserrat sizes (each compiled-in size costs flash, glyph caches cost RAM). CJK → deferred/file-backed or Latin-first v1.
- **`LV_USE_ASSERT_STYLE` off** in release (LVGL warns it costs RAM; it's a debug aid).
- **Image caches:** `LV_CACHE_DEF_SIZE` / image header cache sized to the tiny on-device asset set; no PNG decode cache for assets we pre-convert to raw RGB565 `.bin` at build time (also removes lodepng from the hot path).
- **No 32-bit anything:** all assets/canvases RGB565; no ARGB8888 intermediates (halves every pixel buffer).
- **Subjects/observers are cheap** (bytes each) — the reactive layer is not a RAM concern; caps go on *data* (file lists paginated, thumbnail streaming, JSON parsed with SAX/streaming instead of nlohmann DOM for large Moonraker payloads).
- **Heap poisoning off** in release builds (audit keeps it on for diagnostics; costs a few % of heap + CPU).

### Calibration vs. stock firmware

BTT's stock K-Touch app: 2.25MB image in 4.5MB OTA A/B slots + 7MB SPIFFS assets, built on ESP-IDF 5.1.1. Our entire UI engine at 733KB leaves generous room; the Phase 2 question is the C++ app core on top (PrinterState, subjects glue, Moonraker client), which Task 2 (compile sweep) and Task 3 (link size of the vertical slice) measure next.

## Task 2 — Shim layer + app-core compile sweep ✅ DONE (2026-07-13)

Per-file compile of all 468 `.cpp` under `src/printer/ src/system/ src/ui/` (plus
`src/application/static_subject_registry.cpp` for the vertical slice) against the
ESP-IDF v5.5 Xtensa toolchain, `-std=gnu++17`, exceptions on, RTTI off (IDF default).
Runner: `firmware/native-audit/sweep.py`; raw data: `audit_sweep_results.csv` (pass 1)
and `audit_sweep_results_pass2.csv` (pass 2). Zero unclassified rows in either pass.

**Headline: the app core is ~90% shim-portable.** 420/468 files compile with nothing
but the spdlog→esp_log and json include-path shims once ONE header seam is carved
(below). No fundamental-blocker category comes anywhere near the ~20-file gate line;
the largest is libhv's HTTP client at 8 files.

### Method

- **Flags:** the exact g++ command ESP-IDF generates for its own C++ TUs
  (from `build/compile_commands.json`: sysroot, `-mlongcalls`, `-fno-rtti`,
  `-fexceptions`), plus the app side mirrored from the Linux Makefile compile line
  — same include order, same `-include include/lvgl_pch.h`, same feature defines
  minus Linux-only ones (`HELIX_DISPLAY_SDL`, `HELIX_HAS_SYSTEMD/ALSA/SOUND/TRACKER`,
  `HELIX_HAS_LIBUSB`, `ENABLE_GLES_3D` off; `HELIX_HAS_CFS/IFS/LABEL_PRINTER=1`,
  `HELIX_ENABLE_MOCKS/SCREENSAVER` on — the configuration an ESP32 build would use,
  with portable feature logic measured rather than #ifdef'd out).
- **Bucket A nuance:** `include/lvgl_pch.h` unconditionally includes spdlog + fmt +
  `hv/json.hpp` into every TU, so a literal no-shim compile of ANY file is impossible.
  A is therefore "compiles clean AND the file itself never references spdlog/fmt/json"
  (usage scan); B = compiles and uses the shimmed surface.
- **Two passes.** Pass 1 exposed that a single header — `include/moonraker_client.h`
  (→ `hv/Event.h`, `hv/WebSocketClient.h`) — transitively blocks 149 files, because
  `observer_factory.h`/`app_globals.h` pull it into nearly everything. That wall left
  a third of the codebase uncategorized, so pass 2 carves it with skeletal audit-only
  stand-ins (`shim/hv_stub/hv/{Event.h,WebSocketClient.h}` — `hv::TimerID`, an empty
  `WebSocketClient` base with the three callback members and `setReconnect()`; nothing
  in `moonraker_client.h`'s inline code calls the base, so a declaration suffices for
  compile-only categorization).

### Results

Pass 1 (spdlog/json shims only):

| dir | A | B | C | D | total |
|---|---|---|---|---|---|
| src/application | 0 | 1 | 0 | 0 | 1 |
| src/printer | 9 | 23 | 1 | 28 | 61 |
| src/system | 9 | 69 | 6 | 19 | 103 |
| src/ui | 29 | 151 | 9 | 114 | 303 |
| **TOTAL** | **47** | **244** | **16** | **161** | **468** |

→ 149 of the 161 D rows share one blocker: `moonraker_client.h` → libhv. That seam is
already designed for — `IMoonrakerClient` (`include/i_moonraker_client.h`) exists
precisely so a stub/port can satisfy it (Task 3 does exactly that).

Pass 2 (seam carved — the real distribution underneath):

| dir | A | B | C | D | total |
|---|---|---|---|---|---|
| src/application | 0 | 1 | 0 | 0 | 1 |
| src/printer | 9 | 48 | 4 | 0 | 61 |
| src/system | 9 | 75 | 7 | 12 | 103 |
| src/ui | 30 | 248 | 24 | 1 | 303 |
| **TOTAL** | **48** | **372** | **35** | **13** | **468** |

### Bucket C detail (pass 2, 35 files — all small #ifdef / config-level)

| Blocker | Files | Fix shape |
|---|---|---|
| `typeid` with `-fno-rtti` | 23 (19 via `include/panel_widget_manager.h:40` alone) | `CONFIG_COMPILER_CXX_RTTI=y` (IDF supports it, costs flash — measure in Task 3) or replace that one header's typeid with static type tags |
| `std::min/max/clamp` arg mix | 4 | Xtensa newlib defines `int32_t` as `long`; mixed `int32_t`/`int` args break template deduction — cast-level fixes |
| `sys/statvfs.h` ×2, `ifaddrs.h`, `sys/utsname.h` | 4 | POSIX headers newlib lacks; disk-space/hostname probes — #ifdef with esp_vfs/esp_netif equivalents |
| `spdlog/sinks/base_sink.h` | 1 (+1 via `crash_error_log_sink.h`) | log-backend setup; replaced wholesale by esp_log on a port |
| POSIX signals (`SA_*`), `timegm()`, missing `<thread>` include | 3 | one-liners |

### Bucket D detail (pass 2, 13 files — genuinely Linux-bound)

| Blocker | Files | Notes |
|---|---|---|
| `hv/requests.h` (libhv HTTP client) | 8 | camera_stream, crash_reporter, debug_bundle_collector, ipp_printer, snapshot_qr_scanner, telemetry_manager, update_checker, ui_spoolman_overlay — the biggest real porting surface beyond the WS client; ESP-IDF's esp_http_client is the natural target |
| BlueZ/RFCOMM Bluetooth | 2 | bt_print_utils, makeid_bt_printer (label printing) — feature-gate off for v1 |
| `dlfcn.h`, `ucontext.h`, `hv/hlog.h` | 3 | bluetooth_loader (dlopen), crash_handler (signal-context dumps → esp coredump instead), logging_init (log backend) |

Direct libhv API leakage outside the client seam is tiny: two UI files call
`setReconnect()` on the client; everything else reaches libhv only through
`MoonrakerClient`/`MoonrakerAPI`.

### Shim/stub categorization table (one row per shim, per plan)

| Shim | What it does | Fidelity |
|---|---|---|
| `shim/spdlog_shim.h` | spdlog call surface → `helix_shim_log()` → esp_log; formats with the repo's OWN bundled fmt (`lib/spdlog/.../fmt/bundled`, header-only) — probed clean on Xtensa GCC 14, so format-string/arg type checking is semantically identical to the Linux build | Real formatting; naive fallback kept behind `HELIX_SHIM_NAIVE_FMT`, unused |
| `shim/include/spdlog/{spdlog,common}.h`, `spdlog/fmt/fmt.h` | include-path aliases onto the shim | — |
| `shim/hv_json_shim.h` + `shim/include/hv/json.hpp` | `hv/json.hpp` IS nlohmann 3.12.0 verbatim (zero libhv deps) — alias to the repo's copy, no second vendored header | Identical header |
| `shim/include/json.hpp` | bare `"json.hpp"` form (via `include/unit_conversions.h`; Linux resolves it through `-isystem lib/libhv/cpputil`) | Identical header |
| `shim/platform_stubs.{h,cpp}` | `helix_shim_log(level,msg)` → `esp_log_write` funnel (keeps esp_log macros out of app TUs); compiled in the component build | Real |
| `shim/hv_stub/hv/{Event.h,WebSocketClient.h}` | **pass-2 only, never linked**: `hv::TimerID`, skeletal `WebSocketClient` (3 callback members, `setReconnect`) to carve the moonraker seam | Declaration-only by design |

### Threats to validity

- Compile-only (`-c`, no link): missing symbols, static-init order, and section/size
  issues are invisible until Task 3.
- `-O0 -w`: warnings and optimizer-dependent diagnostics suppressed by design.
- A file that compiles is not a file that WORKS — filesystem paths, `/proc` reads,
  POSIX sockets inside function bodies compile fine against newlib+lwip headers and
  fail at runtime. The sweep measures compile viability, which is what Phase 0 gates.
- Bucket counts depend on the chosen define set (documented above); flipping
  `HELIX_HAS_LABEL_PRINTER=0` would move the 2 BT files out of the denominator.
- `hv_stub` fidelity is declaration-level; pass-2 B files still need the real
  seam satisfied (Task 3 stub or Phase 2 esp_websocket_client port) to link.

## Remaining tasks

- **Task 3:** vertical-slice link size (PrinterState + UpdateQueue + one panel). Not started. Sweep pre-work confirms `printer_state.cpp` and `static_subject_registry.cpp` compile (B) with the seam carved; `ui_panel_home.cpp` additionally needs the `panel_widget_manager.h` typeid resolved (RTTI config or carve).
- **Task 4 [HW]:** RAM watermarks with the slice + CJK font viability. Not started.
- **Task 5:** final report + go/no-go. Gates revised 2026-07-13: yellow = S3 + explicit feature gates (P4 hatch removed).
