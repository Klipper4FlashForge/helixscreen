# Gate discretionary G-code during homing/probing

## Problem

Klipper's g-code dispatch is single-threaded. During a blocking operation (G28,
`BED_MESH_CALIBRATE`, `QUAD_GANTRY_LEVEL`, `PROBE_ACCURACY`, manual probe, long
macro) the g-code lock is held and any `printer.gcode.script` sent meanwhile just
queues until the op finishes — then HelixScreen's 60 s request timeout fires and
the user sees a stream of scary errors ("Fan control failed: The printer may be
busy"). Observed in debug bundle 7CT79XXK (Sovol SV08, Cartographer): 4 back-to-back
fan commands each timed out at 60 s while the printer was mid-calibration, plus
"Already in a manual Z probe" / "Must home x and y before calibration".

## Goal

Refuse **discretionary** g-code (fan, temp, non-homing moves, LED) at the send
boundary while the printer is executing a blocking non-print operation, giving the
user an immediate toast instead of a silent 60 s hang. **Never** block recovery
(E-stop/M112, PAUSE, RESUME, CANCEL_PRINT, FIRMWARE_RESTART), homing itself, or the
manual-probe control commands (TESTZ/ACCEPT/ABORT/SET_GCODE_OFFSET) the user needs
to finish/abort a probe. Never block discretionary commands during a real file
print — mid-print fan/temp changes are legitimate and Klipper handles them fine.

## Design

Mirror the two existing pre-send guards already in `MoonrakerAPI::execute_gcode`
(`src/api/moonraker_api_controls.cpp:426` Klippy-halted, `:451` homing-during-print).

### Signal: "blocking non-print operation in progress"

Predicate (new `PrinterState::is_blocking_operation_active() const`):

    (idle_timeout.state == "Printing" AND print_job_state NOT IN {PRINTING, PAUSED})
    OR manual_probe.is_active

- `idle_timeout.state == "Printing"` is Klipper's canonical busy flag — true for the
  entire duration of ANY blocking command issued from idle. It is **not currently
  subscribed** (`moonraker_discovery_sequence.cpp:1071` explicitly skips it). Re-add
  it with a narrowed field `["state"]`.
- Excluding real file prints (print_job_state PRINTING/PAUSED) is what keeps
  mid-print fan/temp tweaks working — those are queued and execute between moves,
  they don't block 60 s.
- `manual_probe.is_active` (already tracked) covers the interactive Z-probe case
  where idle_timeout may bounce back to Ready between TESTZ commands.

### Classifier: `is_discretionary_gcode(script)`

New pure helper (header `include/gcode_classify.h`), **default-allow**: returns true
ONLY for a known discretionary set, so nothing important is ever blocked by accident.

- Discretionary (blockable): `M106` `M107` `SET_FAN_SPEED` (fan); `M104` `M140`
  `M109` `M190` `SET_HEATER_TEMPERATURE` (temp); `G0` `G1` (non-homing moves);
  `SET_LED` (LED).
- Everything else → not discretionary (allowed): `G28`, `M112`, `EMERGENCY_STOP`,
  `PAUSE`, `RESUME`, `CANCEL_PRINT`, `FIRMWARE_RESTART`, `RESTART`, `M84`/`M18`,
  `TESTZ`, `ACCEPT`, `ABORT`, `SET_GCODE_OFFSET`, calibration commands, macros.
- Multi-line: discretionary ONLY if EVERY non-blank command line is discretionary.
  A compound/macro script (containing e.g. a G28 or TESTZ) is NOT discretionary and
  passes. Token matching mirrors `is_homing_gcode` (first whitespace-delimited token,
  case-insensitive, whole-token compare so `G10`/`M1090` don't false-match).

### Guard placement

Add a third guard block in BOTH `MoonrakerAPI::execute_gcode`
(`moonraker_api_controls.cpp:416`) and `MoonrakerMotionAPI::execute_gcode`
(`moonraker_motion_api.cpp:306`), mirroring the homing guard: if
`is_discretionary_gcode(gcode) && state_.is_blocking_operation_active()` → fire
`on_error` with `MoonrakerErrorType::NOT_READY`, message
"Printer is busy homing or leveling — try again in a moment", and return without
sending. Log at warn when `!silent`.

### Bypasses (note, low priority)

`application.cpp:1682` (spaghetti "tune" button) sends `printer.gcode.script`
directly, skipping the guard — route it through `execute_gcode` if trivial, else
leave + note. `moonraker_client.cpp:1048` `gcode_script()` is the low-level
primitive; leave it.

## Work breakdown

- **Task A** (parallel): `is_discretionary_gcode()` in `include/gcode_classify.h` +
  `tests/unit/test_gcode_classify.cpp` (test-first, full truth table). No deps.
- **Task B** (parallel): re-subscribe `idle_timeout` `["state"]`
  (`moonraker_discovery_sequence.cpp`); parse into new `idle_timeout_printing_` int
  subject in `PrinterCalibrationState::update_from_status` (alongside manual_probe,
  `printer_calibration_state.{h,cpp}`) via `INIT_SUBJECT_INT`; add
  `get_idle_timeout_printing_subject()` accessor + `PrinterState` delegation; add
  `PrinterState::is_blocking_operation_active()`; tests. No deps.
- **Task C** (after A+B, orchestrator): wire guard into both `execute_gcode`; add
  `tests/unit/test_moonraker_api_busy_guard.cpp` mirroring
  `test_moonraker_api_homing_guard.cpp` (drive the new subject directly, assert
  no-send + NOT_READY when busy, send when idle, discretionary-vs-recovery,
  never-block-during-file-print). Handle spaghetti bypass.

## Callout registry (Preston)

1. Cartographer is a user mod, not a Sovol option → do NOT use it as a detection
   signal. (DONE — dropped; build-volume heuristic shipped instead, commit 3145b735b.)
2. Fix Android printer-DB sync. (RESOLVED — no bug; gradle `copyAssets` auto-mirrors
   canonical `assets/` into the APK; the "stale" copy was a local build artifact.)
3. Disallow extra g-code operations during homing/probing — is it possible? (THIS
   PLAN: yes; discretionary-only gate, never block recovery, never block mid-print.)
