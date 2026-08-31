# Design brief — VFA motor compensation screen

A new calibration screen for HelixScreen. This document is self-contained: it assumes no
access to the repository and no prior knowledge of the machine.

Companion to `docs/mockups/tool-offset-calibration/DESIGN_BRIEF.md`, which established this
format. Section 11 is the engineering contract and is the only part that assumes the repo.

---

## 1. The physical thing being done

A stepper motor is not a perfectly smooth machine. Its own magnetic geometry makes the torque
it produces ripple slightly as the shaft turns — a few times per electrical cycle, forever, at
a frequency that depends only on how fast it is going. When the toolhead moves at the speed
where that ripple lines up with something the machine can flex about, the ripple gets printed
into the wall as fine vertical banding. FlashForge's own UI calls these **VFA — vertical fine
artifacts** — and the correction **VFA vertical stripe compensation**.

The fix is not to plan the moves differently. It is to **change the current going into the
motor**: add a small sinusoid to the phase current at 1×, 2× and 4× the electrical frequency,
phased so it cancels the ripple the motor produces by existing. The Creator 5 Pro's main board
is a closed-loop, current-controlled driver — it is told the winding resistance, inductance and
torque constant and synthesises the phase currents itself — so this correction lives inside the
driver's own current loop and is **active on every move**, whether or not anything is
calibrating.

**This is not input shaping, and the distinction is the single most important thing on this
screen.** Input shaping changes *how moves are planned* to avoid exciting the frame. VFA
compensation changes *the current going into the motor* to stop it producing the excitation in
the first place. Different mechanism, different firmware module, different result. The stock
FlashForge touchscreen listed the two next to each other in one menu, which is exactly how
owners came to conflate them. HelixScreen should not repeat that.

### What the calibration actually does

For each of the two motors (X and Y), for each of the three harmonics (1×, 2×, 4×), in each of
the two directions of travel, the printer:

1. **Moves the toolhead 100 mm** through a fixed centre point, at 45° to the axes, so that on a
   CoreXY exactly one motor does the work. The speed is chosen so that motor's electrical
   frequency lands on its resonance — about **114, 57 and 28.5 mm/s** for the 1×, 2× and 4×
   harmonic respectively.
2. **Listens with an accelerometer** mounted on the toolhead, and reduces the whole move to a
   single number: how much vibration there is at exactly the frequency of interest.
3. **Searches** for the amplitude and phase of injected current that makes that number
   smallest — a four-point probe to find the rough phase, a fine phase sweep, an amplitude
   sweep, then a final phase refinement. About **20 test moves per harmonic per direction**.
4. **Applies the winner to the driver immediately**, and stages it to be written to the
   printer's config file.

Twelve searches in total (2 motors × 3 harmonics × 2 directions), roughly 240 measurement
moves.

### How long it takes

Nobody has timed this on a real Creator 5 Pro. From the move geometry, the arithmetic is:

| Harmonic | Speed | 100 mm move | ~20 moves × 2 directions |
|---|---|---|---|
| 1× | 114 mm/s | ~0.9 s + overhead | ~75 s |
| 2× | 57 mm/s | ~1.8 s + overhead | ~110 s |
| 4× | 28.5 mm/s | ~3.5 s + overhead | ~180 s |

**≈ 6 minutes per motor, ≈ 12 minutes for the pair**, and the slowest third of it is at the
end. Treat **10–20 minutes** as the design range. This is by a wide margin the longest thing
HelixScreen asks a printer to do from a single button, and it is the constraint that should
shape the screen more than any other.

### Physical prerequisites

- **The bed must be clear.** The toolhead sweeps a 100 mm diagonal at Z 100, twice, hundreds of
  times. Nothing tall may be on the plate.
- **A tool must be mounted.** The accelerometer measures the *moving mass*. An empty carriage
  weighs the wrong amount and produces numbers that are wrong in a way nothing downstream can
  detect. See §11.6 — today nothing enforces this.
- **The machine must be homed.**
- **Nothing needs to be hot.** No filament, no purging, no heating. This calibration is
  entirely mechanical, which makes it unusual among calibrations and worth saying on screen —
  it removes a whole class of "am I ready?" anxiety.
- **It should not be interrupted casually.** Every completed harmonic is already live on the
  driver; a stop leaves the motors in a half-tuned state that is still better than nothing but
  is not what was asked for.

### Nothing is permanent until saved

The search applies each result to the driver as it finds it, so the tuning is **live for this
boot** the moment the run ends. But it is only written to disk by a separate `SAVE_CONFIG`
step, and **`SAVE_CONFIG` restarts the printer's firmware.**

That split is deliberate and inherited: FlashForge's own app did the same thing, and the
underlying macro has the save step commented out precisely so that a macro cannot restart
firmware under somebody mid-session. **The screen owns that step.** If the user walks away
after a 15-minute run without saving, the entire run is lost on the next power cycle, silently.
Designing against that outcome is the second most important thing on this screen.

---

## 2. Who is looking at this

The owner of a FlashForge Creator 5 Pro running community firmware, standing at the machine.
They are technical enough to have replaced their printer's firmware, and they are here because
they saw fine vertical banding on a flat wall and went looking for the setting.

They will use this screen **once**, possibly ever. The compensation is a property of the motors
themselves. It does not drift with use, it is not disturbed by printing, and it is not affected
by anything the user changes. There is no maintenance cadence. This is the calibration you are
least likely to need — and the one most likely to be run out of curiosity by somebody who does
not have a VFA problem at all.

Two consequences for the design:

- **The screen must teach.** A once-ever screen cannot assume familiarity with anything, and
  cannot rely on the user having read anything first. In particular it must say, unprompted,
  that this is not input shaping.
- **The screen should be honest that most people do not need it.** A "you probably do not need
  this" that is confident rather than discouraging is better product than a start button that
  invites a 15-minute wait for no gain.

---

## 3. Hardware and platform constraints

These are hard limits, not preferences.

| | |
|---|---|
| Physical display | 800 × 480 px, capacitive touch |
| **Usable width for this screen** | **708 × 480** — it is an overlay panel that slides in from the right over the main UI |
| Input | Finger only. No stylus, no hover, no right-click, no keyboard |
| Minimum touch target | ~44 px in the dimension the finger moves |
| Default theme | Dark. A light theme exists and the design must survive it |
| Toolkit | LVGL 9.5 |
| Must also degrade to | 480 × 272 and 480 × 320 (smaller machines run the same code) |

**Available:** flexbox row/column layout, rounded rectangles, borders, opacity, an icon font,
three text sizes, per-widget style states, vertical scrolling, spinners, progress bars, simple
charts, animation of position and opacity.

**Not available:** gradients as a primary device, drop shadows, blur, arbitrary vector
illustration, custom fonts beyond the shipped set, video, free-form canvas drawing outside a
purpose-built widget.

**Design tokens — use these names, not hex values:**

- Surfaces: `card_bg`, `elevated_bg`, `border`
- Text: `text`, `text_muted`
- Meaning: `primary` (action / in progress), `success`, `warning`, `error`
- Spacing: `space_xs`, `space_sm`, `space_md`, `space_lg`

Text sizes are `text_heading`, `text_body`, `text_small`. There is no fourth size.

---

## 4. The data model

The calibration produces **six numbers per motor**, twelve in total:

| | 1× | 2× | 4× |
|---|---|---|---|
| amplitude | `td1_amp` | `td2_amp` | `td4_amp` |
| phase, forward | `td1_phase1` | `td2_phase1` | `td4_phase1` |
| phase, backward | `td1_phase2` | `td2_phase2` | `td4_phase2` |

Amplitude is a current-injection figure in the range **0.001 – 0.100**. Phase is in radians,
**0 – 2π**. Neither is meaningful to a user and neither should be the headline of the screen.

**The number that means something is the residual reduction.** Every search reports the
vibration it measured with no compensation (*bare*), the vibration it achieved with the winner,
and the improvement between them as a percentage. That percentage — *"vibration at this
harmonic reduced by 63%"* — is the only output on this screen a human can act on or feel good
about, and it should carry the results view.

Three outcomes are possible per search, and all three are normal:

| Outcome | Meaning | How it should read |
|---|---|---|
| A good reduction | Compensation found and applied | Success. Show the percentage |
| **Below the noise floor** | The motor's bare vibration at this harmonic was already too small to work on; the search skips it and leaves the value at zero | **Not a failure.** "Already clean — nothing to correct" |
| A poor model fit | The search fell back to a slower uniform sweep and still found something | Succeeded, took longer. Needs no user-visible distinction |

The "already clean" case is likely to be common on at least some harmonics, and a design that
renders a zero as a failure or an empty slot will make a good result look broken.

There is no per-tool dimension here. The Creator 5 Pro is a four-head tool changer, but the
motors being tuned are the two that move the gantry — this is **one machine-wide result**, not
one per tool.

---

## 5. What the user can do

1. **Read what this is** — including that it is not input shaping, and that they may not need it.
2. **Start the full calibration** — both motors, all three harmonics, both directions. One button.
3. **Watch it run**, with enough information to tell that it is progressing and roughly how long
   is left.
4. **Stop it**, and understand what stopping leaves behind.
5. **Save the result**, understanding that this restarts the printer's firmware.
6. **See the previous result**, if the machine has been calibrated before, and start over.

A confirmation is required before starting: it is 15 minutes of unattended motion and the plate
must be clear. One confirmation, not two.

There is deliberately **no** per-motor or per-harmonic start button in this list. The
underlying commands support that granularity, but exposing it invites a user to produce a
partially-calibrated machine for no benefit they can perceive. If the design wants a
"recalibrate just X" affordance it should be argued for, not assumed.

---

## 6. The states

**A — Never calibrated.** The common first arrival. Explanation, prerequisites, one start
button. This state does the teaching, and it is the state most users will see and leave without
pressing anything. It deserves the most design attention, not the least.

**B — Running.** Twelve searches over 10–20 minutes. Needs, in rough order of importance:
- Where in the whole run we are — *motor 1 of 2, harmonic 2 of 3, direction 1 of 2* — and a
  meaningful overall progress figure, not a bar that restarts.
- Time elapsed, and ideally a remaining estimate. The estimate is achievable: the phase
  structure is fixed and known in advance, so remaining work is computable rather than guessed.
- Some evidence of life between milestones. Milestones are minutes apart; a static screen for
  three minutes reads as a hang.
- A stop button that is reachable but not easy to hit by accident.
- Results filling in as they complete — this is what turns a 15-minute wait into something worth
  watching, and it is free, because each harmonic finishes with its own number.

**C — Finished, unsaved.** The most dangerous state on the screen. The tuning is live, the
numbers are on display, and everything is lost on the next power cycle unless the user acts.
The save affordance must be unmissable, and the consequence of leaving must be stated. This
state must also survive the user navigating away and coming back.

**D — Finished, saved.** Firmware has restarted. The screen must handle the disconnection and
reconnection gracefully — a modal about lost connection appearing at the moment of success would
be a bad ending to a 15-minute investment.

**E — Previously calibrated.** Values exist from an earlier run. Show them, offer to run again.
This state and state A share a layout but not a message.

**F — Cannot run.** No accelerometer, no tool mounted, machine busy, printing. Say which, and
say what to do about it. Do not show a start button that will fail.

**G — Stopped or failed part-way.** Some harmonics have real results and are live on the
driver; the rest are untouched. This is a genuinely partial state and should be shown as one —
resumable, savable, honest about which is which.

---

## 7. What exists today

Nothing. This screen does not exist in any form; the calibration is currently reachable only by
typing a command into a console. The design is not constrained by an existing layout.

Its nearest neighbour in the product is the **input shaper** screen, which measures resonance
with the same accelerometer and has a comparable start / measure / results shape. Visual
consistency with it is desirable. **Conceptual conflation with it is the thing to avoid** — if a
user can look at the two screens and not know why both exist, the design has failed at its
primary job.

---

## 8. What a good design achieves

In priority order:

1. **A user who arrives confused about VFA versus input shaping leaves un-confused.**
2. **Nobody loses a 15-minute run by not saving it.**
3. **The 10–20 minute wait feels progressing rather than hung**, at a glance from across a room.
4. **A "0% — already clean" result reads as good news**, not as a failure or a blank.
5. **A user who does not need this is told so** before they spend the 15 minutes.
6. It looks like the rest of HelixScreen.

---

## 9. Fixed versus open

**Fixed — these cannot be designed around:**

- 708 × 480, dark by default, finger input, the token names in §3.
- The run is 10–20 minutes and cannot be shortened or backgrounded.
- Saving restarts the printer firmware. There is no way to persist without it.
- The results are 12 numbers that mean nothing to a user; the percentages are what mean something.
- Both motors, three harmonics, two directions. The structure is not negotiable.
- "Already clean" is a valid, common, successful outcome.

**Open — genuinely the designer's call:**

- The whole layout, at every state.
- How progress is expressed. A bar, a twelve-cell grid that fills in, a list that advances —
  all viable. The twelve searches are a natural visual unit and nothing forces a linear bar.
- Whether the raw amplitude/phase numbers are shown at all, hidden behind a detail affordance,
  or omitted entirely.
- How the not-input-shaping distinction is made. A sentence, a diagram, a comparison, placement
  relative to the input shaper entry point in the menu.
- The screen's name. **"VFA Compensation"** matches what owners saw on the stock firmware and
  what they will search for. "Motor Resonance" describes the mechanism more honestly but will
  be confused with input shaping by exactly the users at risk of confusing them. This is a real
  trade-off and the brief does not settle it.
- Whether the "you probably do not need this" message is prominent, quiet, or conditional.

**Two ideas worth exploring, offered as prompts and not as prescriptions:**

- The twelve searches form a natural 2 × 3 × 2 grid. A grid that fills in with per-cell
  reduction percentages as the run proceeds would serve as progress indicator, live result
  display and final summary in one object — and would make "already clean" a legible cell state
  rather than a missing row.
- The before/after residual is a measured pair of numbers per cell. There may be a better story
  in showing the vibration coming down than in showing a percentage.

---

## 10. Deliverable

A design for states **A, B, C, E, F, G** at 708 × 480, dark theme, plus a note on how the
layout degrades to 480 × 272. State D (saved, firmware restarting) can be described rather than
drawn.

The repo's established mockup workflow is in
`docs/mockups/tool-offset-calibration/README.md`: static XML mockups hot-reloaded over the real
panel in a `--test` instance, screenshotted at native resolution. That path is available here —
`HELIX_MOCK_PRINTER=creator5` brings up a simulated Creator 5 Pro.

---

## 11. Engineering contract

This section assumes the repository, and is the part that constrains implementation rather than
design.

### 11.1 The command surface

The calibration is entirely firmware-side and already shipping. HelixScreen sends G-code and
parses console output; there is no new Moonraker API and no MCU work.

```
STEPPER_RESONANCE_FACTORY_CALIBRATE      # the whole run — zeroes all six damp slots,
                                         # then SEARCH_CALIBRATE on stepper_x and stepper_y
SAVE_CONFIG                              # separate, required, restarts klippy
```

`STEPPER_RESONANCE_FACTORY_CALIBRATE` is a `[gcode_macro]` in FlashForge's
`printer.vibration.cfg`, over `klippy/extras/stepper_resonance_tester.py`. Results are pushed
live with `MCLIB_SET_RESONANCE_DAMP` and staged with `configfile.set` into `[mclib stepper_x]`
/ `[mclib stepper_y]`.

Finer-grained entry points exist and are **not** proposed for the UI (§5):
`STEPPER_RESONANCE_SEARCH_CALIBRATE STEPPER=stepper_x TDX=1,2,4 DIR=0|1`,
`STEPPER_RESONANCE_DAMP_DISABLE` (zeroes everything — useful for an A/B demo if one is ever
wanted).

### 11.2 Progress and results come from `notify_gcode_response`

There is no status object to observe. The parse surface is the console, and it is stable and
deterministic. Markers, in the order they appear:

| Line | Yields |
|---|---|
| `Search calibration: N_PHASE=%d, N_AMP=%d (~%d moves per harmonic per direction, ~%d total per harmonic)` | the move budget, up front |
| `===== Search calibrating td%d for %s =====` | harmonic + which motor — **the primary progress tick** |
| `--- Direction: Forward (dir=1) ---` | direction |
| `Round 1: Quadrature phase probe` / `Round 2: Fine phase sweep` / `Round 3: Amplitude optimization` / `Round 4: Final phase refinement` | sub-phase, for the between-milestones liveness signal |
| `  Bare distortion: A=%.4f` | the baseline |
| `  Distortion too small (%.1f < %.1f), skipping` | **the "already clean" outcome** |
| `Search result: amp=%.4f, phase=%.3f rad (%.1fdeg), residual=%.4f (bare=%.4f, reduction=%.1f%%)` | **the headline percentage**, per direction |
| `td%d result: amp=%.4f, phase1=%.3f, phase2=%.3f` | the saved per-harmonic value |
| `  WARNING: …` | over-compensation retry / poor model fit — log, do not surface |

Twelve `===== Search calibrating =====` lines and twelve `Search result:` lines constitute a
complete run, which makes both an accurate progress denominator and a full results grid
available without any estimation.

The established mechanism is `subscribe_console()` registering a `notify_gcode_response`
handler wrapped in `lifetime_.bg_cb(...)` — see
`src/ui/ui_panel_calibration_tool_offset.cpp:744`. The structural precedent for a long
multi-phase op with progress and ETA is `include/pid_progress_tracker.h`; a
`VfaProgressTracker` alongside it, unit-testable against captured console text, is the right
shape. `MoonrakerAdvancedAPI` already houses `start_bed_mesh_calibrate` /
`start_pid_calibrate` / `start_mpc_calibrate` with exactly this
`(on_progress, on_complete, on_error)` signature.

### 11.3 Reading previous results (states E and G)

`printer.objects.query` on `configfile` — the staged and saved `[mclib stepper_x]` /
`[mclib stepper_y]` sections carry `td*_amp` / `td*_phase1` / `td*_phase2`, and
`configfile.save_config_pending` distinguishes state C from state D.

### 11.4 The capability gate — how "exclusive to Creator 5" gets expressed

`CLAUDE.md` forbids a vendor name in generic code, and forbids naming a subject or header after
a printer. So the screen is **not** gated on "is this a Creator 5". It is gated on the
capability, which is exclusive to the Creator 5 today as a consequence rather than as a rule:

```cpp
// include/motor_resonance_compensation.h  (+ src/printer/motor_resonance_compensation.cpp)
// Provider table keyed on a detection predicate — copy z_offset_persistence.cpp literally.
bool printer_supports_motor_resonance_cal(const PrinterDiscovery& hw);
// → hw.has_macro("STEPPER_RESONANCE_FACTORY_CALIBRATE")
```

`STEPPER_RESONANCE_FACTORY_CALIBRATE` is the right predicate: it is a macro, so it appears in
the macro list that discovery already collects, and its presence means precisely "this firmware
can do this", which is the question being asked. No other shipped printer has it. Mirror it
into a subject in `src/printer/printer_capabilities_state.cpp` next to
`printer_has_tool_offset_cal` (declare ~:50, set ~:139) as **`printer_has_motor_resonance_cal`**,
and hide the menu row with
`<bind_flag_if_eq subject="printer_has_motor_resonance_cal" flag="hidden" ref_value="0"/>` plus
a static `hidden="true"` so it never flashes before discovery lands.

Entry point: the CALIBRATION group in `ui_xml/advanced_panel.xml`, **directly below
`row_input_shaping`** — adjacency is correct here, because these two genuinely are the two
vibration screens and separating them would not stop the confusion, only hide it.

### 11.5 Files this touches

Following the checklist that the tool offset screen established:

| | |
|---|---|
| `include/ui_panel_calibration_vfa.h` | `OverlayBase` subclass, `get_global_vfa_cal_panel()`, `init_vfa_row_handler()` |
| `src/ui/ui_panel_calibration_vfa.cpp` | singleton + `StaticPanelRegistry::register_destroy`, `UI_MANAGED_SUBJECT_*`, `register_xml_callbacks`, abort-on-deactivate |
| `ui_xml/calibration_vfa_panel.xml` | `header_bar` + scroll region + pinned footer |
| `include/vfa_progress_tracker.h` + `src/ui/vfa_progress_tracker.cpp` | console → phase/percent/ETA, unit-testable |
| `include/motor_resonance_compensation.h` + `src/printer/motor_resonance_compensation.cpp` | the capability question |
| `src/xml_registration.cpp` | register the component (~:673) |
| `src/printer/printer_capabilities_state.{h,cpp}` | the subject |
| `ui_xml/advanced_panel.xml` | the entry row |
| `src/application/subject_initializer.cpp` | call `init_vfa_row_handler()` (~:419-425) |
| `src/api/moonraker_client_mock.cpp` | replay a captured run under `HELIX_MOCK_PRINTER=creator5` |
| `docs/devel/ENVIRONMENT_VARIABLES.md` | the mock env var |

The mock deserves emphasis: a 15-minute, hardware-dependent, once-ever flow is close to
untestable by hand. Replaying a captured console transcript through the tracker at
`--sim-speed` is what makes states B, C, F and G developable at all.

### 11.6 Two findings from the firmware side, neither blocking

**`STEPPER_RESONANCE_SEARCH_CALIBRATE` has no prep wrapper.** `ff-toolchange.cfg` wraps
`SHAPER_CALIBRATE`, `TEST_RESONANCES` and `MEASURE_AXES_NOISE` in `_FF_SHAPER_PREP` (home if
needed, grab a tool if the carriage is empty, because an empty carriage measures the wrong
moving mass) at lines 195, 201 and 207. It does **not** wrap
`STEPPER_RESONANCE_SEARCH_CALIBRATE`, and `STEPPER_RESONANCE_FACTORY_CALIBRATE` calls it
directly. `docs/notes/44-vfa-calibration.md` in the firmware repo states that it does — that
claim is wrong as shipped.

So today, a VFA calibration started on an unhomed machine with an empty carriage will run to
completion and produce twelve confidently wrong numbers. Fixing this belongs in the firmware
config, not in HelixScreen; until it lands, **the screen must check homed state and mounted
tool itself** and refuse into state F. Worth doing regardless — refusing before a 15-minute run
is better UX than a wrapper silently grabbing a tool.

**The move geometry has never been checked on real hardware.** Per the firmware note, the
sweeps run X 4.6 → 75.4 and Y 184.6 → 255.4 against a `position_max` of X 310 / Y 260 — Y
clears by 4.6 mm — and the configured `x_range` / `y_range` are validated at load but never
used to clamp the move. On a tool changer with docks at X ≈ 297 this is worth watching once
before it is offered to owners. It does not change the design, but it argues for the first
release being explicit that this is new.

### 11.7 Prior art in the repo

`assets/config/presets/creator5.json:86` already carries
`"initial_resonance_compensation_run": false`, used by `src/ui/ui_wizard_input_shaper.cpp` —
the existing precedent for a "has this ever been run" flag, and the right thing to reuse for
distinguishing state A from state E.

---

## Sources

Firmware-side facts in this brief were read from the `Klipper4FlashForge` firmware tree:
`klippy/extras/stepper_resonance_tester.py` (the search, the console output, the parameters),
`config/printer.vibration.cfg` (the macro), `config/ff-toolchange.cfg` (the prep wrapper and
its gap), and `docs/notes/44-vfa-calibration.md` (the reverse-engineering of the stock app's
flow and the `mclib` driver layer).
