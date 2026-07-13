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

## Remaining tasks

- **Task 2:** shim layer (spdlog→esp_log, hv/json→nlohmann alias) + A/B/C/D compile sweep over `src/printer/ src/system/ src/ui/`. Not started.
- **Task 3:** vertical-slice link size (PrinterState + UpdateQueue + one panel). Not started.
- **Task 4 [HW]:** RAM watermarks with the slice + CJK font viability. Not started.
- **Task 5:** final report + go/no-go. Gates revised 2026-07-13: yellow = S3 + explicit feature gates (P4 hatch removed).
