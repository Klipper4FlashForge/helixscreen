# Print state: one axis, named capabilities

Status: **Phase 0 in progress**
Branch: `fix/preparing-job-lifecycle` (Phase 0 only) → own branch for Phases 1-4
Predecessor: [2026-08-18-preparing-job-lifecycle.md](2026-08-18-preparing-job-lifecycle.md)

> **Resuming after a context break?** Read "Phase tracker" and "How to resume"
> at the bottom first. Every phase has explicit exit criteria; do not start one
> until its predecessor's criteria are met.

---

## The defect class

Code asks a **semantic** question - *"does a job own the toolhead right now?"* -
of a **raw wire value**: `helix::PrintJobState`, parsed straight from Moonraker's
`print_stats.state`. That value cannot express a job the app has committed to but
the printer has not reported yet, so every consumer keyed on it inherits the same
blind spot.

There are two types with nearly the same name, and the collision is a cause, not
a cosmetic problem:

| Type | What it is | Where |
|---|---|---|
| `helix::PrintJobState` | The wire. What `print_stats.state` said. | `include/printer_state.h` |
| `PrintState` | The derived UI lifecycle, including `Preparing`. | `include/print_lifecycle_state.h` |

Someone writing a toolhead guard reaches for the type called "print state" and
gets the wire. `print_occupies_toolhead(PrintJobState)` is a semantic question
with a wire parameter - the body is not wrong, it simply cannot see `Preparing`
and never could. **Adding a `has_preparing_job` bool parameter would patch one
call site while preserving the shape that generates the next one.** That idea was
considered and rejected.

## Evidence

Two censuses, 2026-08-19. Findings marked **(verified)** were re-checked by hand
rather than taken from the survey.

### The existing helper was never adopted

`print_occupies_toolhead` (`include/printer_state.h:113`) has **2 call sites**
(`ams_subscription_backend.cpp:317`, `ui_bypass_toggle_controller.cpp:28`), while
**~60 sites open-code `PRINTING || PAUSED` inline**. It was introduced as a single
source of truth and nothing migrated onto it. Worth remembering before we build
another one: *a helper nobody is forced onto does not become the answer.* That is
what Phase 4's gate is for.

### Nine competing definitions of "is a print active"

Exactly one includes `Preparing`, and it is dead code.

| # | Predicate | True for | Sees Preparing | Non-test call sites |
|---|---|---|---|---|
| 1 | `print_occupies_toolhead(PrintJobState)` | PRINTING, PAUSED | no | 2 |
| 2 | `status_indicates_active_print(json)` | PRINTING, PAUSED | no | 3 |
| 3 | `print_active` subject | PRINTING, PAUSED | no | 2 C++, **21 XML** |
| 4 | `is_active_print_state(PrintJobState)` | PRINTING, PAUSED | no | 3 |
| 5 | `PrintLifecycleState::is_active(PrintState)` | Printing, Paused, **Preparing** | **yes** | **0** |
| 6 | `PrintLifecycleState::want_viewer()` | everything but Idle | yes | 2 |
| 7 | `print_in_progress` subject | the preparing window, hand-maintained | **yes** | see below |
| 8 | `print_blocks_filament_op(printing, paused, self_homes)` | PRINTING; PAUSED only when self-homing | no | 4 |
| 9 | `is_blocking_operation_active()` | inverted: busy-but-not-printing | no | 2 |

Plus a fifth ad-hoc state set with no named predicate at
`spoolman_manager.cpp:99-100` (`PRINTING || COMPLETE || PAUSED`).

Two sites needed `Preparing` badly enough to hand-patch it inline rather than use
#5: `ui_emergency_stop.cpp:284-288` and `print_control_view.cpp:12-14`. The second
was written during this work - the correct predicate was in the header the whole
time and was not found.

### The safety gap (verified)

21 XML bindings disable jog, motion and extrude controls:

```
<bind_state_if_eq subject="print_active" state="disabled" ref_value="1"/>
```

| File | Lines |
|---|---|
| `ui_xml/controls_panel.xml` | 108, 336, 345, 355, 370, 489, 496, 507, 514 |
| `ui_xml/micro/controls_panel.xml` | 95, 282, 296, 415, 424, 431, 441, 447, 453 |
| `ui_xml/motion_panel.xml` | 143, 150 |
| `ui_xml/components/panel_widget_bypass.xml` | 15 |

That last one matters: the home **bypass tile** binds `print_active`, and
`BypassToggleController::toggle()` guards on `print_occupies_toolhead()`. So the
bypass feature carries the blind spot in *both* its affordance and its handler -
during a host-side pre-print window the tile is enabled and the handler agrees to
run, driving filament through a toolhead that is homing or probing.

`print_active` is `PRINTING || PAUSED`. **During a host-side pre-print window
every one of these controls is enabled while the toolhead is homing and
probing.** Same hazard as the bypass toggle, 21 more sites. This is the reason
Phase 1 leads: the sweep is safety work, not tidying.

### A duplicate mechanism we created (verified)

`print_in_progress` (`printer_print_state.h:355`, `:602`, `:665`) already models
the preparing window - *"true during print preparation"*, per its own doc
comment. It is set imperatively from `PrintPreparationManager` and cleared by
hand on **ten-plus separate exit paths** (`ui_print_preparation_manager.cpp:630`,
`:639`, `:1350`, `:1358`, `:1410`, `:1419`, `:1493`, `:1559`, `:1610`, `:1669`,
and more).

The preparing-job work added `has_preparing_job()` + `preparing_epoch` without
finding it. Two mechanisms for one concept is the disease being treated. The new
one is the better shape - one owner, one exit point, `retire_preparing()` with a
reason - so the incumbent gets **derived from it**, not deleted outright (21 XML
bindings and several readers depend on a boolean subject existing).

## The design

**No new enum.** `PrintState` is already the derived model, and
`derive_print_state()` is already documented as "the single definition of this
mapping". The work is to finish that abstraction and give it a home outside a
panel - not to invent a parallel one.

Three parts:

1. **One axis.** `PrintState` becomes what everyone consults. `PrintJobState` is
   demoted to an implementation detail of `derive_print_state()` and of the sites
   that genuinely need wire semantics.

2. **Named capability predicates**, defined next to `derive_print_state()`:

   | Predicate | True for | Replaces |
   |---|---|---|
   | `job_holds_machine(lifecycle)` | Preparing, Printing, Paused | most `PRINTING \|\| PAUSED` guards |
   | `laying_material(lifecycle)` | Printing | progress/layer/ETA meaningfulness |
   | `job_ended(lifecycle)` | Complete, Cancelled, Error | terminal checks |
   | `machine_idle(lifecycle)` | Idle | calibration/firmware gates |

   The value is not brevity. `state == PRINTING || state == PAUSED` appears
   identically today for five unrelated reasons, and the call site does not say
   which. Naming the *why* is what makes the next lifecycle change safe.

3. **"Whose job" stays a separate axis.** `has_preparing_job()` answers which
   *mechanism* applies (retire vs `CANCEL_PRINT`), not what the user may do.
   Collapsing the two gives "Cancel is enabled and does the wrong thing" - learned
   the hard way in `print_control_view.cpp`.

### Do not over-collapse

`ams_subscription_backend.cpp:320` deliberately **relaxes** PAUSED for filament
ops when the backend self-homes, and `print_blocks_filament_op()` encodes that.
These are considered exceptions, not sloppiness. Callers that need to distinguish
`Paused` must still be able to; the predicates are conveniences over the
lifecycle, not a replacement for switching on it.

### Sites that must KEEP raw `PrintJobState` (24)

A blind sweep breaks these. Confirmed during the preparing-job work:

- **The 7 derivation/parse sites** - `print_lifecycle_state.cpp:33-49`,
  `printer_print_state.cpp:1345-1356`, `:278-287`, `:445-449`,
  `printer_state.cpp:57-77`, `:82-92`, `printer_print_state.cpp:71`.
- **Terminal-outcome formatting** - the 9 sites in `print_completion.cpp`
  (`:205-215`, `:260`, `:309-315`, `:329-335`, `:370-373`) and the 4 progress and
  layer freeze guards in `printer_print_state.cpp` (`:704`, `:813`, `:888`,
  `:944`).
- **Telemetry's terminal classification** - `telemetry_manager.cpp:3114-3120`,
  `:3163`. And `:3039` in particular: it resets the max pre-print phase on
  "PRINTING from non-PAUSED". On the raw state that is `STANDBY -> PRINTING`, one
  reset at the real start. On the lifecycle it becomes
  `Idle -> Preparing -> Printing`, so the reset would fire at `Preparing ->
  Printing` and **wipe the data the tracker exists to collect**.
- **Navigation's activation edge** - `print_start_navigation.cpp:27`. On a
  lifecycle including host-side `Preparing` it would open the status panel for a
  job the printer has not accepted, duplicating `PrintStartController`'s
  optimistic push.
- **Reconciliation** - `printer_print_state.cpp:1278` (`reconcile_preparing()`
  requires an actually-running print) and the collector arming/teardown
  predicates in `moonraker_manager.h:176-178`, `:211-213`, `:222`.

**Every one of these needs a comment saying why it is on the wire**, or the next
sweep "finishes the job" and regresses it.

---

## Phase tracker

Update this table in the same commit that completes a phase. `Commit` is the
completing SHA.

| Phase | Scope | Sites | State | Commit |
|---|---|---|---|---|
| **0a** | `print_in_progress` derived from the preparing job; watchdog for a job that never confirms | 20 setters | **done** | `289d56856` |
| **0b** | Collapse `PrintStatusPanel`'s private `PrintLifecycleState` onto the published subject | 8 | **in progress** | - |
| **1** | Safety guards + a lifecycle-derived XML subject | 15 + 21 XML | not started | - |
| **2** | Affordance + navigation | 12 + 6 | not started | - |
| **3** | Display + bookkeeping | 11 + 15 | not started | - |
| **4** | Delete the helper, add the ratcheting gate | 2 + gate | not started | - |
| **5** | Rename `PrintState` -> `PrintLifecycle` | mechanical | not started | - |

Total: **61 production edit points**, ~5 signature changes, ~30 test files.

---

### Phase 0 - make the preparing window have one owner

**On the current branch.** This is finishing the preparing-job work, not new
scope, and Phases 1-4 depend on the published subject being trustworthy.

Two jobs:

**0a. `print_in_progress` becomes derived.** `PrintPreparationManager` set it
true at two entry points and cleared it on **eighteen** exit paths; a missed path
left it stuck true and `can_start_new_print()` then refused every later print for
the rest of the session. It is now published from the preparing job -
`begin_preparing()` raises it, `retire_preparing()` lowers it whatever the exit
reason. All 20 manual calls deleted. The subject and accessor stay: readers and
XML depend on the boolean existing.

Two things found while doing it:

- **This closes a double-tap hole.** The old clear ran from a `wrapped_completion`
  that fired when the start RPC was *accepted*, which is before `print_stats`
  reports the job. In that gap `print_in_progress` was already false while the
  job state was still STANDBY, so `can_start_new_print()` returned true and a
  second tap could start another print. Deriving it holds the flag until the
  printer confirms.

- **The double-tap guard was in the wrong layer.** `start_print()` opened with
  *"reject if a print is already being started"*, reading the very flag the
  caller had just set - `PrintStartController::start_now()` arms the preparing
  job and then calls `start_print()` eleven lines later. Once the flag became
  derived, that guard rejected **every** print as a duplicate.
  `test_pre_start_timeout_gate.cpp:234` caught it. The guard is deleted: the
  controller already runs `can_start_new_print()` *before* arming, which is the
  correct place for it. Worth remembering as a general hazard of this refactor -
  **a guard that reads a flag its own caller sets is invisible until the flag
  changes owner.**

- **`PreparingExit::TimedOut` was dead.** It is handled everywhere - cooldown,
  notification, `decide_preparing_exit_action()` - but *nothing ever produced
  it*. That was survivable while the flag cleared on RPC-accept; once the flag is
  derived, a job the printer never acknowledges would latch it true forever.
  `begin_preparing()` now arms a one-shot watchdog (`PREPARING_WATCHDOG_MS`,
  1800s - above the K2's ~1140s worst case, matching `PrintStartCollector`'s own
  "definitely stuck" ceiling) and `retire_preparing()` disarms it. Cancelled in
  the destructor too, per CLAUDE.md threading rule 5. Test hook:
  `PrinterPrintStateTestAccess::fire_preparing_watchdog()`.

**0b. Collapse the panel's second state machine.** `PrintStatusPanel` owns a
private `PrintLifecycleState lifecycle_` fed by its own
`on_job_state_changed(job_state, outcome)` (`ui_panel_print_status.cpp:2683-2690`)
that never reads the published `print_lifecycle` subject. Until these two agree
by construction, no consumer is wrong to distrust the subject. The panel becomes
a *reader*.

Panel `PrintState` call sites to re-point: `ui_panel_print_status.cpp:1139-1140`,
`:1689`, `:2707-2709`, `:2827`, `:2879`, `:3155`, `:3176`, `:3717`.

**Exit criteria**
- [x] `set_print_in_progress()` has no callers outside `PrinterPrintState`.
- [x] `print_in_progress` is true for exactly the interval
      `begin_preparing()` -> `retire_preparing()`, proven by a test per exit
      reason (Confirmed, Superseded, Failed, Cancelled, TimedOut).
- [x] A preparing job that never confirms is retired as `TimedOut` rather than
      latching the flag.
- [ ] `PrintStatusPanel` holds no `PrintLifecycleState` member; its state comes
      from the `print_lifecycle` subject. **(0b - not started)**
- [ ] Full suite green (95/95 shards).

**Watch for:** a stuck-true `print_in_progress` is the failure mode being fixed,
so the test must assert the *false* edge on every exit reason, not just the happy
path.

---

### Phase 1 - safety guards, and the XML that matters

Highest value: closes the live hazard where 21 motion controls are enabled while
the toolhead moves during pre-print.

**1a. Add the lifecycle-derived subject** the XML needs. `print_active` cannot
gain `Preparing` without changing meaning for its C++ readers, so publish a new
boolean (working name `job_holds_machine`) from the lifecycle and move the 21
bindings onto it. Both subjects coexist until Phase 3 retires `print_active`.

XML sites: the 21 enumerated in "The safety gap" above. Note
`components/panel_widget_bypass.xml:15` is one of them, so Phase 1 fixes the
bypass tile's affordance and `ui_bypass_toggle_controller.cpp:28` fixes its
handler - both are needed, neither is sufficient alone.

**1b. The 15 guards:**

| Site | Gates |
|---|---|
| `moonraker_gcode_guards.cpp:20` | Layer-1 refusal of app-emitted homing |
| `ams_subscription_backend.cpp:317,320` | AMS filament-op refusal (keep the PAUSED relaxation) |
| `ui_bypass_toggle_controller.cpp:28` | Bypass toggle |
| `printer_state.cpp:965` | `is_blocking_operation_active()` |
| `tool_switcher_widget.cpp:580-581,670` | Tool change refusal + paused confirmation |
| `ams_backend_ad5x_ifs.cpp:56` | AD5X runout-recovery paths |
| `post_op_cooldown_manager.cpp:73-74` | Post-op nozzle cooldown |
| `ui_panel_filament.cpp:2599` | Cooldown scheduling after a filament op |
| `filament_sensor_manager.cpp:995-996` | Head-sensor-empty noise suppression |
| `update_checker.cpp:1246,2892` | Update download + notification |
| `upgrade_nudge.cpp:82` | Upgrade nudge (**PRINTING only today** - inconsistent with neighbours; decide deliberately) |
| `display_manager.cpp:1023` | Display sleep inhibit |

**Exit criteria**
- [ ] All 21 XML bindings reference the lifecycle-derived subject.
- [ ] Motion/jog/extrude controls verified disabled during a host-side preparing
      window - by widget **state**, via `ctl ls`, not by `ctl click`
      (`ctl click` bypasses the indev layer and fires handlers on disabled
      widgets, so it cannot prove an affordance).
- [ ] Each guard has a test that fails if it is reverted to the raw state.
- [ ] Full suite green.

---

### Phase 2 - affordance and navigation

12 affordance sites (`print_control_view.cpp` - already partly done -
`print_control_buttons.cpp:128,146`, `ui_ams_context_menu.cpp:219`,
`ui_ams_sidebar.cpp:1060`, `ui_panel_filament.cpp:1876`, `ui_panel_power.cpp:233`,
`power_device_state.cpp:156,211,236`, `ui_emergency_stop.cpp:284`,
`ui_job_queue_modal.cpp:388`, `printer_print_state.cpp:1239-1245`) and 6
navigation sites.

`ui_emergency_stop.cpp:284-288` already hand-ORs the pre-print phase - replace
that with the predicate rather than leaving a second spelling.

**Navigation needs care:** `print_start_navigation.cpp:27` is one of the 24 sites
that must stay on the wire. Only `ui_plr_offer_controller.cpp:86` and
`ui_panel_ams.cpp:268` move.

**Exit criteria**
- [ ] No hand-rolled `|| start_phase != 0` compositions remain.
- [ ] `is_active_print_state()` deleted or re-typed to `PrintState`.
- [ ] Full suite green.

---

### Phase 3 - display and bookkeeping

11 display sites and 15 bookkeeping sites, **excluding** the terminal-outcome and
freeze-guard sites listed under "must keep raw". Retire `print_active` once its
last reader is gone.

**Exit criteria**
- [ ] `print_active` deleted, or documented as wire-semantics-on-purpose.
- [ ] Every remaining raw-state site carries a comment saying why.
- [ ] Full suite green.

---

### Phase 4 - delete the helper, add the gate

Delete `print_occupies_toolhead(PrintJobState)`.

Add `scripts/check_raw_print_job_state.py` on the established pattern: regex over
`PrintJobState::(PRINTING|PAUSED|...)`, `get_print_job_state()`, and
`lv_subject_get_int(...get_print_state_enum_subject())` - the last is the back
door a `get_print_job_state()` deprecation would miss. Per-line opt-out
`// RAW_PRINT_STATE_OK: <reason>`, file allowlist for the derivation sites,
`--max-allowed N` ratchet wired in `scripts/quality-checks.sh`.

Cost: ~250-350 lines of Python, ~20 lines in `quality-checks.sh`, ~11 bats
meta-tests on the `tests/shell/test_l081_gate.bats` pattern. **No changes needed
to the pre-commit hook or GitHub Actions** - both run `quality-checks.sh`
wholesale.

**Exit criteria**
- [ ] Gate fails on a newly introduced raw comparison outside the allowlist.
- [ ] Baseline set to the surviving count and recorded here.

---

### Phase 5 - rename

`PrintState` -> `PrintLifecycle`, mechanically, once call sites are stable. Last
because it touches everything and must not be interleaved with behaviour changes.

---

## Risks

- **Every phase changes behaviour at every site it touches, by design** - the
  sites start seeing `Preparing`. Each phase needs its own test pass; one green
  run at the end proves nothing about which phase broke what.
- **The 24 keep-raw sites are load-bearing.** Telemetry and navigation were both
  caught mid-refactor during the preparing-job work; they look identical to the
  sites that should move.
- **`upgrade_nudge.cpp:82` is PRINTING-only** while its neighbours are
  `PRINTING || PAUSED`. Decide whether that is intentional before normalising it -
  do not let a sweep silently change it.
- **Test count.** ~30 test files touch `PrintJobState`; three are pure
  predicate-definition tests that get rewritten outright
  (`test_print_start_navigation.cpp`, `test_print_active.cpp`,
  `test_print_control_view.cpp`).

## Out of scope, tracked separately

**Klippy readiness** is the same defect at larger scale - 3 parse sites, 7
disagreeing predicates, 18 raw consumers across 13 layers, and the only model
that handles "the app knows something the wire doesn't" (`expected_restart`
suppressing a transient SHUTDOWN) is trapped inside
`PrinterStatusIcon::compute_state()`. Consequence: `MoonrakerAPI::execute_gcode`
tells the user *"Klipper is halted - restart firmware to continue"* during a
`SAVE_CONFIG` restart the app itself initiated. Own branch, after this one.

Also found, not scheduled: `MoonrakerMotionAPI::execute_gcode` gates on klippy
alone with no connection term (`moonraker_motion_api.cpp:416`), 200 lines from
the gate fixed to consult both (bundle XRK8KPTF); `is_printer_ready()` is dead
code doing an RPC round trip for data already in a subject; temperature
"ready to proceed" has 6 implementations with 3 thresholds; AMS "is filament
loaded" has two definitions in one file that disagree exactly where
`ams_backend_snapmaker.cpp:1591-1598` works to distinguish them.

## How to resume

1. Read the phase tracker. Find the first phase not marked done.
2. Re-read that phase's exit criteria - they are the definition of done.
3. `git log --oneline` on this branch; the plan's `Commit` column should match.
4. If a phase is half-finished, the surviving raw-state count is the progress
   metric: `grep -rn "PrintJobState::\(PRINTING\|PAUSED\)" src/ include/ | wc -l`.
   Record it here when you stop.

**Progress metric log** (append on every stop):

| Date | Phase | Raw-state sites remaining | Note |
|---|---|---|---|
| 2026-08-19 | 0 | 91 | Plan written. Baseline for the resume command below. |
| 2026-08-19 | 0a | 91 | `289d56856`. Phase 0a touches the preparing window, not the raw-state count, so the metric is unchanged by design. Suite 95/95. |

**Note on units.** The census counts **88 distinct decision sites**; the resume
command counts **91 matching lines**. They are different measures and both are
right - a `switch` arm and a two-line condition are one site but two lines. Track
the grep number here, because that is the one a future session can reproduce in a
second without redoing the census.
