# Jog Move Coalescing + Busy-Guard Attribution

**Date:** 2026-07-15
**Status:** Approved
**Origin:** Discord reports (kostake, ChriL): rapid jog-wheel taps produce "jog failed: Printer is busy" toasts; command rate feels throttled. Separate unverified report of crashes from the movement screen (no bundles).

## Problem

Each jog tap sends its own `printer.gcode.script` (`G91\nG0 X…\nG90`). There is no
queue, debounce, or in-flight tracking. The discretionary-gcode busy guard
(`MoonrakerMotionAPI::execute_gcode`, `src/api/moonraker_motion_api.cpp:368`;
duplicate in `moonraker_api_controls.cpp:479`) refuses discretionary gcode while
`PrinterState::is_blocking_operation_active()` is true. That predicate keys off
Klipper `idle_timeout.state == "Printing"` — which Klipper reports for the entire
duration of **any** move, including a manual jog. Net effect: every jog blocks the
next tap until the toolhead physically stops, and each rejected tap fires an error
toast + error sound.

The guard itself is legitimate: it was added (`62cb8dd1f`) after jogs queued behind
a long calibration op and timed out at 60s (Sovol SV08, bundle 7CT79XXK). It just
can't distinguish "busy running SHAPER_CALIBRATE" from "busy finishing my own 1mm
jog".

## Design

### 1. Jog coalescer (MotionPanel)

Main-thread-only state machine owned by `MotionPanel`:

- **Idle → tap:** send immediately via `move_axis()` (unchanged UX for single taps).
  Set `jog_in_flight_ = true`.
- **In flight → tap:** do NOT call the API. Accumulate algebraically into per-axis
  pending deltas (`pending_[X/Y/Z]`). Reversal taps subtract — +1,+1,−1 on X nets
  +1mm; a full reversal cancels pending travel ("take it back" property).
- **Success ack:** if pending is nonzero, flush all pending axes as ONE script with
  per-axis feedrates preserved (`G91\nG0 X4 F6000\nG0 Z-0.5 F600\nG90`), stay in
  flight. If pending is zero, clear the flag.
- **Error ack:** drop all pending, clear flag, show ONE toast.
- **Bounds:** soft-stop envelope check runs per tap against *predicted* position
  (last sent target + pending), not the lagging position subject. Taps past the
  envelope clamp pending to the edge; "blocked at bed edge" warning fires once per
  edge-hit, not per tap.
- **Deactivate / print start:** drop pending. The in-flight move finishes naturally.
- **Threading:** move_axis callbacks arrive on the websocket thread and now touch
  coalescer state, so they route through `lifetime_.bg_cb("MotionPanel::on_jog_ack",
  …)` (MotionPanel is an OverlayBase; `lifetime_` exists). No bare `this` capture,
  no `tok.expired()` checks on the bg thread (L081).

Side effect: taps 2..N never reach the API, so the per-tap rejection toast storm
disappears from the jog path entirely.

### 2. Busy-guard attribution

The coalescer alone is insufficient: `idle_timeout.state` lingers at `"Printing"`
briefly after a move completes, so a serialized follow-up send would still bounce.

Fix: attribute the busy-ness. `MoonrakerMotionAPI` stamps a monotonic
"last app-initiated motion" time on every jog send and ack. The discretionary busy
guard allows jog gcode when `idle_timeout == "Printing"` AND the app has a jog in
flight or acked within a **2s grace window** (self-inflicted busy). `"Printing"`
with no recent app jog = external op (calibration, console gcode from another UI)
→ refuse with toast, exactly as today.

Accepted residual: a jog issued inside the grace window right as an external op
starts queues one move behind it — benign, not the 60s pileup the guard targets.

Both guard copies (`moonraker_motion_api.cpp` and `moonraker_api_controls.cpp`)
must get the same predicate; prefer hoisting the shared check.

### 3. Crash hardenings (riding along)

- `MotionPanel::on_ui_destroyed()` override nulling `jog_pad_` and any other raw
  child widget pointers, matching peer panels (bed_mesh, console, input_shaper…).
  Closes a latent UAF: `jog_ready_observer_` dereferences `jog_pad_` on every
  connection/klippy flap (`ui_panel_motion.cpp:457`), currently safe only because
  the overlay is hide-on-close.
- Toast dedupe in `ToastManager`: an identical message arriving within ~1s
  refreshes the existing toast's timer instead of creating a new widget. Generic
  defense for all rapid-fire error paths.

### Out of scope

- No changes to homing guards, print-active guards, or klippy-READY gate.
- No Klipper-side queuing (approach rejected: N taps = N RPCs, no cap, no take-back).
- The unverified Discord crash: ask reporter for a debug-bundle share code; if it
  isn't the toast storm, it's separate work.

## Testing (TDD — tests first, red before green)

1. **Coalescer unit tests** (pure logic, no LVGL): accumulate, algebraic reversal,
   flush-on-ack, error-drops-pending, predicted-position bounds clamp, deactivate
   drops pending, multi-axis flush preserves per-axis feedrates.
2. **Guard attribution truth table** (extend `test_printer_state_blocking_op.cpp`,
   `test_moonraker_api_busy_guard.cpp`): external-busy refuses; self-busy (in
   flight / within grace) passes; grace expiry refuses; real input values per L093
   (idle_timeout flips to "Printing" during our own move).
3. **Toast dedupe test**: identical message within window refreshes, different
   message stacks.
4. **Manual:** mock printer rapid-tap session; then deploy to owned hardware (Pi or
   AD5M) for feel — mock can't judge responsiveness.

## Decisions log

- Queue model: client-side coalescing (chosen over raw Klipper queuing and hybrid).
- Scope: both crash hardenings included.
- Grace window: 2s (tunable constant).
- Reversal semantics: algebraic sum.
