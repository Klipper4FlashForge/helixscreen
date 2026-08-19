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

Exactly one includes `Preparing`, and it has a single external consumer.

| # | Predicate | True for | Sees Preparing | Non-test call sites |
|---|---|---|---|---|
| 1 | `print_occupies_toolhead(PrintJobState)` | PRINTING, PAUSED | no | 2 |
| 2 | `status_indicates_active_print(json)` | PRINTING, PAUSED | no | 3 |
| 3 | `print_active` subject | PRINTING, PAUSED | no | 2 C++, **21 XML** |
| 4 | `is_active_print_state(PrintJobState)` | PRINTING, PAUSED | no | 3 |
| 5 | `PrintLifecycleState::is_active(PrintState)` | Printing, Paused, **Preparing** | **yes** | **1** (`ui_panel_print_status.cpp:3227`) |
| 6 | `PrintLifecycleState::want_viewer()` | everything but Idle | yes | 2 |
| 7 | `print_in_progress` subject | the preparing window, hand-maintained | **yes** | see below |
| 8 | `print_blocks_filament_op(printing, paused, self_homes)` | PRINTING; PAUSED only when self-homing | no | 4 |
| 9 | `is_blocking_operation_active()` | inverted: busy-but-not-printing | no | 2 |

Plus a fifth ad-hoc state set with no named predicate at
`spoolman_manager.cpp:99-100` (`PRINTING || COMPLETE || PAUSED`).

The one consumer of #5 is the panel's Tune/Timelapse gate
(`ui_panel_print_status.cpp:3227`, `PrintLifecycleState::is_active(state)`); the
class also uses it internally four times (`print_lifecycle_state.cpp:136`, `:144`,
`:153`, `:167`). So one site out of roughly sixty that ask "is a print happening"
asks it of the predicate that can actually answer.

Two others needed `Preparing` badly enough to hand-patch it inline rather than
reach for #5: `ui_emergency_stop.cpp:284-288` and `print_control_view.cpp:12-14`.
The second was written during this work - the correct predicate was in the header
the whole time and was not found. That is the argument for Phase 4's gate in one
sentence: a correct helper that nobody is *forced* onto gets reinvented at each
call site.

> **Correction, 2026-08-19.** An earlier draft of this plan said #5 had *zero*
> call sites and called it dead code. That was taken from a survey and not
> checked. It has one. The conclusion is unchanged but the number was wrong -
> verify a census claim before building an argument on it.

### The safety gap (verified)

21 XML bindings disable the controls that would fight a running job for the
toolhead:

| What | Count |
|---|---|
| Bed-levelling calibration - QGL, Z-Tilt, bed screws | 8 |
| Z calibration + Save Z-Offset | 4 |
| User macro buttons 1-4 | 8 |
| The home bypass tile | 1 |

> **Correction, 2026-08-19.** An earlier draft called these "jog, motion and
> extrude" controls. They are not - the jog arrows and extrude buttons carry no
> `print_active` binding at all. The conclusion is unchanged and if anything
> sharper: every one of these 21 emits G-code that homes, probes, or rewrites the
> Z offset, which is precisely what a host-side pre-print block is already doing.

All 21 use the identical form:

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

`ams_subscription_backend.cpp:320-326` deliberately **allows** a filament op on a
PAUSED print **when the backend does NOT self-home** - because then no firmware
macro can hide a `G28`, and Layer 1
(`reject_homing_during_active_print`) still refuses any the app emits itself.
`print_blocks_filament_op()` encodes the same rule: `paused && backend_self_homes`
(`filament_op_slot_resolver.h:156`).

> **Get the direction right.** An earlier draft of this plan had it inverted -
> "relaxes PAUSED when the backend self-homes". That is exactly the dangerous
> reading: it would permit filament ops in the one case where a hidden G28 can
> fire mid-print. Verified against the source 2026-08-19.

So this site needs **both** axes: the lifecycle *and* a backend capability. It is
the concrete proof that `job_holds_machine()` cannot be the only predicate -
callers that must distinguish `Paused` have to keep being able to. The named
predicates are conveniences over the lifecycle, never a replacement for switching
on it.

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
| **0b** | Panel adopts the live phase so it agrees with the authority by construction | 1 + tests | **done** | `41392dfd2` |
| **1a** | `job_holds_machine` predicate + subject; the 21 XML bindings moved onto it | 21 XML | **done** | `152986987` |
| **1b** | The guards (see the corrected table below - 11 migrate, 2 stay raw) | 13 sites + 4 helper call sites + 2 observers | not started | - |
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

**0b. Make the panel ADOPT the published state instead of deriving its own.**

> **Scope corrected 2026-08-19.** This step was originally written as "delete the
> panel's private `PrintLifecycleState` and make the panel a reader". An
> investigation of what that class actually holds proved that wrong, and the
> deletion would have broken the Complete screen. Recorded here because the
> mistake is instructive: *"two state machines over the same inputs" was true of
> the enum and false of everything else in the object.*

`PrintLifecycleState` is not a duplicate state machine. It is a state machine
**plus a display-freeze latch store plus panel-local widget state**. Only the
enum is duplicated:

| Field | Status |
|---|---|
| `current_state_` | duplicated - this is the only thing `print_lifecycle` replaces |
| `elapsed_seconds_`, `remaining_seconds_`, `current_progress_`, `current_layer_`, `total_layers_` | **freeze latches, not mirrors.** Forced to their terminal values at Complete (`print_lifecycle_state.cpp:99-109`) and rejected once `outcome != NONE`. Moonraker zeroes the underlying subjects on STANDBY; these latches are why the Complete screen still reads `100% / 240/240 / 0s`. Deleting them deletes that. |
| `gcode_loaded_` | **no subject exists.** Written from four viewer-widget events (`:853`, `:1142`, `:1549`, `:1561`) no printer subject can see, and auto-cleared on the ->Idle edge. |
| `nozzle_*`, `bed_*`, `speed_percent_`, `flow_percent_` | pure unguarded mirrors - genuinely deletable |

`StateChangeResult` - seven booleans plus `old_state` - drives ~150 lines
(`:2696-2866`) including `runout_handler_->on_print_state_changed(old, new)`.

### The real defect: the two DO disagree, for the whole of PRINT_START

`PrintLifecycleState::on_job_state_changed()` hard-codes the phase
(`print_lifecycle_state.cpp:53-56`):

```cpp
PrintState new_state = derive_print_state(job_state, /*start_phase=*/0);
```

So when Moonraker reports `PRINTING` while a pre-print phase is still live - the
firmware-side case, the one `derive_print_state` exists for - the panel's copy
moves `Preparing -> Printing` while `print_lifecycle` correctly stays `Preparing`.
**They hold different states for the entire remainder of `PRINT_START`.**

### Revised scope: adopt, do not delete

The panel keeps the class, the latches, and `StateChangeResult`. What changes is
where the **enum** comes from: the panel stops deriving it and adopts the
published value. One input changes; the freeze semantics are untouched.

**Write the tests first.** There are currently **zero** panel-level tests
exercising `lifecycle_` - the 40 `[lifecycle]` cases all test the class in
isolation and would keep passing against a panel that had stopped using it. There
is no safety net at this seam, so it has to be built before the seam moves.

**Two behaviour changes to make deliberately, not by accident:**

- **Aborted preparation.** `on_start_phase_changed` returns a bare `bool` and
  produces no `StateChangeResult`, so `Preparing -> Idle` today runs none of the
  print-ended cleanup at `:2711-2751`. Adopting a published transition makes that
  a first-class edge.
- **In-`PRINT_START` display.** With the panel correctly staying `Preparing`, the
  gcode-load delay flips 500ms -> 5000ms (`:3717`) and the preprint observers keep
  owning the time display (`:1689`, `:3155`, `:3176`). That is the intended
  correction, but it is a change.

**Ordering hazard (needs a runtime check).** `observe_int_sync` snapshots the
value at notify time but runs the handler at drain time. Two lifecycle publishes
inside one `update_from_status` batch (e.g. `printer_print_state.cpp:436` then
`:466`) would collapse `print_lifecycle_prev`, so reconstructing `state_changed`
(`:3049`) from it is not safe without checking. Verify under
`HELIX_MOCK_AUTO_PRINT=1 --sim-speed 6 -vvv`.

Unused API found on the way: `get_state()` (`ui_panel_print_status.h:227`) and
`get_progress()` (`:247`) had no callers anywhere.

**`get_state()` is now the test seam** - `tests/unit/test_print_status_lifecycle_seam.cpp`
uses it to compare the panel's belief against the published subject, which is the
only way to observe the disagreement. Keep it. `get_progress()` is still unused;
delete it, or leave it and say why.

The seam tests drive the panel through its **real observer path** - subject
writes plus a queue drain - rather than calling the private handlers, so they
exercise the ordering the refactor has to preserve. Drive points:
`update_from_status()` for the job state, `set_print_start_state()` for the
phase.

**Exit criteria**
- [x] `set_print_in_progress()` has no callers outside `PrinterPrintState`.
- [x] `print_in_progress` is true for exactly the interval
      `begin_preparing()` -> `retire_preparing()`, proven by a test per exit
      reason (Confirmed, Superseded, Failed, Cancelled, TimedOut).
- [x] A preparing job that never confirms is retired as `TimedOut` rather than
      latching the flag.
- [x] Panel-level tests exist for `lifecycle_` before the seam moves - was zero,
      now `tests/unit/test_print_status_lifecycle_seam.cpp`. **(0b)**
- [x] `PrintLifecycleState::on_job_state_changed()` no longer derives its own
      enum with a hard-coded `start_phase=0`. **(0b)**
- [x] The Complete-screen freeze still holds. **(0b)**
- [x] Full suite green (95/95 shards).

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

**1b. The guards.** Surveyed site by site 2026-08-19; the paths and line
numbers in the first draft of this table were mostly wrong and three of the
sites turned out not to be `PRINTING || PAUSED` at all. Corrected list:

| Site | Gates | Action |
|---|---|---|
| `src/api/moonraker_gcode_guards.cpp:20` | Layer-1 refusal of app-emitted homing | **KEEP RAW** - see below |
| `src/printer/ams_subscription_backend.cpp:318,321` | AMS filament-op refusal | via `print_blocks_filament_op` |
| `src/ui/ui_bypass_toggle_controller.cpp:28` | Bypass toggle | `job_holds_machine` |
| `src/printer/printer_state.cpp:968` | `is_blocking_operation_active()` | **KEEP RAW** - see below |
| `src/ui/panel_widgets/tool_switcher_widget.cpp:588` | Tool-change refusal | via `print_blocks_filament_op` |
| `src/ui/panel_widgets/tool_switcher_widget.cpp:670` | Paused confirmation modal | `lifecycle == Paused` |
| `src/printer/ams_backend_ad5x_ifs.cpp:56` | AD5X runout gate - **PAUSED-only, not a "holds machine" question** | `lifecycle == Paused` |
| `src/system/post_op_cooldown_manager.cpp:73` | Post-op nozzle cooldown | `job_holds_machine` |
| `src/ui/ui_panel_filament.cpp:2599` | Cooldown scheduling after a filament op | `job_holds_machine` |
| `src/print/filament_sensor_manager.cpp:994` | Head-sensor-empty noise suppression | `job_holds_machine` |
| `src/system/update_checker.cpp:1245,2891` | Update download + notification | `job_holds_machine` |
| `src/system/upgrade_nudge.cpp:83` | Upgrade nudge - **PRINTING-only, no PAUSED arm** | `job_holds_machine` |
| `src/application/display_manager.cpp:1021` | Display sleep inhibit | `job_holds_machine` |

Three follow-on edits the survey turned up that are not in the census:

- `print_blocks_filament_op(bool printing, bool paused, bool self_homes)`
  (`include/filament_op_slot_resolver.h:156`) becomes
  `(PrintState lifecycle, bool self_homes)`. Four call sites, three of them
  outside the 15: `ui_ams_sidebar.cpp:1059`, `ui_ams_context_menu.cpp:218`,
  `ui_panel_filament.cpp:1876`.
- Two observers still watch `print_state_enum` and would not fire on the
  `Idle -> Preparing` edge, so the affordance would not grey even with the guard
  fixed: `tool_switcher_widget.cpp:125`, `ui_panel_filament.cpp:227`.

#### Two sites that must NOT be widened

**`moonraker_gcode_guards.cpp` - widening it breaks print start.**
`PrintPreparationManager` sends the user's configured pre-start block through
`api_->execute_gcode()` *inside* the preparing window
(`ui_print_preparation_manager.cpp:732`), and `is_homing_gcode()` matches any
line whose first token is `G28` (`include/gcode_homing.h:45`). On the K2 that
block is the forced bed mesh. Widening the guard to `job_holds_machine()` makes
it refuse the app's own pre-start G-code on every printer whose pre-start block
homes. This is the Phase 0a hazard exactly - **a guard that reads a flag its own
caller sets** - and it is the second time this refactor has produced one.

The window is not left unguarded: during a *firmware-side* `PRINT_START` the job
state is already `PRINTING`, so the guard fires. During a *host-side* block the
app is the only thing driving the toolhead, and the affordances that could send
a competing `G28` (all 21 XML bindings, the AMS ops, the bypass tile) are
disabled by the rest of Phase 1.

**`is_blocking_operation_active()` - it is inverted, and already correct.**
It returns true when `idle_timeout` says busy AND that busy-ness is *not*
explained by a print. During a host-side pre-print block `idle_timeout` reads
`Printing` (the host is running G-code) while `print_stats` reads `standby`, so
today the function correctly answers "blocked". Swapping the raw state for
`job_holds_machine()` would make it answer "not blocked" and *admit* jogs during
the bed mesh - the opposite of this phase's purpose. Leave it, and say why.

That is 25 keep-raw sites now, not 24.

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
  `PRINTING || PAUSED`. **Decided 2026-08-19: normalise it, deliberately.** Its
  own comment says *"Don't nudge mid-print - the printer is the priority, not our
  prompts"*, and a paused print is mid-print; the neighbouring guard in
  `update_checker.cpp:1246` refuses update downloads on `PRINTING || PAUSED` for
  the same reason. The narrow spelling reads as an oversight, not a design. Phase
  1 moves it to `job_holds_machine()`, which also suppresses the nudge during
  `Preparing` - a user who just committed to a print is the last person who wants
  an upgrade prompt. This makes the nudge strictly rarer, which is the safe
  direction, but it IS a behaviour change and the commit must say so.
- **Test count.** ~30 test files touch `PrintJobState`; three are pure
  predicate-definition tests that get rewritten outright
  (`test_print_start_navigation.cpp`, `test_print_active.cpp`,
  `test_print_control_view.cpp`).

## Not verified on hardware

Neither is a correctness gap - both are behaviour that has only ever been
exercised by unit tests. Recorded here because they are the difference between
"green" and "known to work on a printer", and they outlive any one session.

- **The Cancel/Pause affordance change.** The K2 hardware run predates
  `34cd93e93`, so the change that enables Cancel during a host-side pre-print and
  disables Pause during a firmware-side one has never been touched by a finger.
  Verify by reading the widget's `disabled` state flag (`ctl ls`), **not** by
  `ctl click` - that bypasses the indev layer and fires handlers on disabled
  widgets, which is how a previous "verification" of this exact path was wrong.
- **CB1/Voron, the no-pre-start-block case.** The 750ms debounce is supposed to
  stop an overlay flash on a printer whose preparing window is sub-second. Never
  run there.

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
| 2026-08-19 | 0b | 91 | `41392dfd2`. Also count-neutral - 0b changes which inputs an existing derivation gets, not how many sites read the wire. Suite 95/95. **Phase 0 complete.** |
| 2026-08-19 | - | 91 | `d606bd823`. Re-merged main (10 commits, no conflicts). Suite 95/95, and the **full ungated** quality sweep passes (36 gates) - worth re-running before any push, because per-commit gates only ever run `--staged-only` and skip anything you did not stage. |
| 2026-08-19 | 1a | 91 | `152986987`. Subject + 21 XML bindings + 13 tests. Count-neutral by design: 1a adds a derived subject and moves XML, it does not remove a C++ wire read. 96/96 shards, ungated sweep green. |
| 2026-08-19 | - | 91 | `6945d4e98`. Re-merged main again (12 commits, no conflicts, translations auto-merged). Suite 95/95, ungated sweep green. Site counts re-verified unchanged: 21 bindings, 91 raw-state sites. |

## Before you touch anything: state of the branch

Branch `fix/preparing-job-lifecycle`, worktree
`.worktrees/preprint-arm-on-initiation`. Green at three layers as of
`6945d4e98`: build, suite 95/95, and the full ungated quality sweep.

**Phase 0 is not separately mergeable.** `begin_preparing`, `retire_preparing`,
`PreparingExit` and `derive_print_state` do not exist on main - they are all from
the preparing-job work earlier on this branch. Neither 0a nor 0b can be
cherry-picked. The mergeable unit is the whole branch.

**Main moves fast; re-merging is a standing step, not a one-off.** Three merges
in one day, and the last batch landed work adjacent to Phase 1. Re-merge and
re-run before assuming any site list here is current, then re-check the two
counts in the progress log - they are cheap and they are the tripwire.

**Already checked, do not redo:** `90089b714 fix(ams): the bypass subject reports
what the toggle acts on` repointed `ams_bypass_active` at the backend's
`is_bypass_active()`. That is about *which bypass value* the subject reports and
is orthogonal to the Phase 1 finding. `ui_bypass_toggle_controller.cpp:28` still
reads `print_occupies_toolhead(PrintJobState)` and still cannot see the preparing
window.

**Landing checklist.** The pre-push hook now runs the **full ungated** sweep
(`scripts/quality-checks.sh`), while every commit only ran `--staged-only`, which
skips gates that inspect nothing you staged. Run the full sweep yourself before
pushing rather than discovering it at push time. Note also that a green
pre-commit hook is **not** evidence the generated artifacts are in sync - only
`make test-run` catches a stale `theme_token_table.cpp` after a `ui_xml` change.

**Build cost.** Touching `printer_print_state.h` or `print_lifecycle_state.h`
rebuilds essentially everything; that was 40+ minutes on a contended box. Batch a
phase's changes into one build instead of iterating, and check `uptime` before
assuming a slow build is stuck - this machine runs many parallel sessions.


### Testing this area: three artifacts that impersonate bugs

Every one of these produced a failure indistinguishable from the defect being
hunted. Budget for them.

1. **`ctl click` fires handlers on DISABLED widgets.** It calls
   `lv_obj_send_event()` directly, bypassing the indev layer, so it proves the
   handler runs and says nothing about whether a finger can reach it. Check the
   widget's `disabled` state flag instead.
2. **A dead fixture's `PrinterState` leaves subject names resolving to freed
   storage**, and the damage lands in an unrelated destructor long after your own
   assertions pass. Fixed upstream by `c7cc96670`, but the shape recurs.
3. **One `UpdateQueue::drain()` is not enough.** The panel's observers are
   `observe_int_sync`; a handler running during a drain queues more work that is
   still pending when `drain()` returns, leaving the panel exactly one transition
   behind. Drain until quiescent.

**The discriminator is whether the failure moves when you change production
code.** In 0b the firmware-side assertion moved and the other two did not - that
was the signal, and it was available two wrong hypotheses before it was used.

**Note on units.** The census counts **88 distinct decision sites**; the resume
command counts **91 matching lines**. They are different measures and both are
right - a `switch` arm and a two-line condition are one site but two lines. Track
the grep number here, because that is the one a future session can reproduce in a
second without redoing the census.
