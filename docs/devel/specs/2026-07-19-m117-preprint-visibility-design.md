# M117 Pre-Print Visibility

**Date:** 2026-07-19
**Status:** Approved, pending implementation

## Problem

Users report that M117 messages never appear in HelixScreen, while Fluidd shows them
fine. Investigation found three independent defects. None of them are in the Moonraker
subscription or parse path, which are correct.

Pre-print (heating, QGL, purge) is the longest stretch of dead time in a print and the
point where the user has least insight into what the printer is doing. It is exactly
where macro authors put M117 calls, and exactly where HelixScreen suppresses them.

### Defect 1 — delta clobber (primary)

`printer_print_state.cpp:283-296` unconditionally clears `display_message_` on the
transition to PRINTING from a non-PAUSED state.

Moonraker's `notify_status_update` carries *deltas*: `display_status.message` is re-sent
only when its value changes. An M117 issued during PRINT_START that arrives in an earlier
notification than the `state=printing` one is erased at `:294`, and Klipper never re-sends
it because the value has not changed. The message is gone for the remainder of the print.

Web UIs do not show this because they seed from a full `objects/query` snapshot and hold
current values. HelixScreen only ever sees deltas here. This is the best explanation for
the reported symptom.

### Defect 2 — pre-print suppression gate

`printer_print_state.cpp:831-843`:

```cpp
bool is_preparing = lv_subject_get_int(&print_start_phase_) != (int)PrintStartPhase::IDLE;
int new_value = (has_message && !is_preparing) ? 1 : 0;
```

This blanks M117 for the entire pre-print window. `PrintStartPhase::COMPLETE` is itself
non-IDLE, so a phase parked at COMPLETE suppresses M117 for a whole print.

The gate was deliberate: `print_start_collector.cpp:243` already pipes
`display_status.message` into `print_start_message`, so showing both would duplicate the
line. That rationale is sound but optimizes for a screen the user may not be looking at.

### Defect 3 — missing surfaces

Only four XML surfaces bind `display_message`, and three require an active print. On the
default library layout an M117 sent while idle renders nowhere.

### Contributing issue — shared subject, two writers

`print_start_collector.cpp` writes both user M117 text and hard-coded phase labels
(`"Heating Bed..."` :502, `"Leveling Gantry..."` :1064, `"Purging..."` :1080) into the
same `print_start_message` subject, and `printer_print_state.cpp:951` makes it sticky.
They clobber each other by arrival order: a macro's `M117 Leveling 3/9` can be overwritten
by the generic `"Leveling Gantry..."`. A printer sending no M117 is indistinguishable from
one that does.

## Design

### Subject ownership

Each subject gets exactly one writer:

- **`print_start_message`** — HelixScreen's derived phase label. Written only by collector
  phase transitions.
- **`display_message`** — the printer's M117. A pure mirror of `display_status.message`,
  written only at the parse site (`printer_print_state.cpp:534`) and never cleared by us.

Nothing has two writers, so nothing clobbers. During pre-print the panel's
`preparing_overlay` shows the phase label while the metadata strip shows M117 — now
genuinely different text, side by side.

### Clearing policy

`display_message` is cleared on **print end** — the transition from PRINTING to
COMPLETE / CANCELLED / ERROR. Otherwise it is a pure mirror of `display_status.message`.

**Why not "clear at new print start", the intuitive choice.** No initiation hook exists.
`reset_for_new_print()` (`printer_print_state.cpp:190`) is reached only via the *observed*
state delta: `print_stats.state == "printing"` → observer (`moonraker_manager.cpp:600-625`)
→ `collector->start()` (:629) → `set_print_start_state()`
(`print_start_collector.cpp:263`), whose body is wrapped in `queue_update`
(`printer_print_state.cpp:917`) and therefore runs a queue tick *later* than the `:294`
clear. Clearing there reproduces Defect 1 with a wider race window.

There is no initiation-side alternative. Local print paths
(`PrintPreparationManager::start_print()`, `api_->job().start_print()`) never touch the
phase or the reset, so even a user-tapped print arrives only through the delta. Prints
started externally (Fluidd/Mainsail, queue, resume) have no local hook at all. Other
candidate keys fail too: `print_stats.filename` emits no delta on a same-file reprint
(relied on at `:175-178`), and no `job_id` exists on the status channel.

Print *end* is safe because the hazard is specific to print start, when PRINT_START is
emitting M117s that a clear would destroy and Klipper would never re-send. At print end
there is no such traffic. An `M117 Print complete` from an END_PRINT macro arrives after
the state transition and survives normally.

This yields the intended guarantee — last print's text never bleeds into the next print —
without the delta hazard. Idle screens go blank after a job finishes; an M117 sent while
idle thereafter displays normally.

### C++ changes

| Change | Site |
|---|---|
| Delete M117 pass-through (`update_message_only`) | `print_start_collector.cpp:243-253` |
| Delete `is_preparing` from gate → `has_message ? 1 : 0` | `printer_print_state.cpp:831-843` |
| Delete clear on →PRINTING | `printer_print_state.cpp:294` |
| Delete clear in `reset_for_new_print()` | `printer_print_state.cpp:190` |
| **Add** clear on PRINTING → COMPLETE/CANCELLED/ERROR | `printer_print_state.cpp` state-transition block (~:283-296) |

The `:190` removal is safe, and the earlier open question about disconnect resolves in
favor of removal: `reset_for_new_print()` has no disconnect caller anywhere in `src/`
(its only production caller is `set_print_start_state()` at `:935`), and reconnect
re-seeds `display_status` independently from the `printer.objects.subscribe` snapshot
(`moonraker_discovery_sequence.cpp:1040`, `:1424-1445`).

Net: `display_message` has exactly two writers — the parse site (`:534`) and the
print-end clear. Neither can run while PRINT_START is emitting.

### XML surfaces

| View | Current | Change |
|---|---|---|
| Panel (`print_status_panel.xml:266`) | bound, dead via gate | none |
| 2 — detailed idle (`print_status_detailed_idle.xml:37`) | bound | none |
| 4 — detailed active (`print_status_detailed_active.xml:131`) | bound | none |
| 3 — library active (`panel_widget_print_status.xml:182`) | bound, works | gate to `print_status_view == 3` |
| 0 — library idle (`print_card_idle`, :16) | no surface | add row |
| 1 — compact idle (`print_card_idle_compact`, :90) | no surface | **out of scope** — see below |

**View-4 clipping.** `panel_widget_print_status.xml:182` is a child of `print_card_printing`
(:117), not a root-level sibling. In view 4 it is clipped: `print_card_printing` is
`flex_flow="column"` / `scrollable="false"`, and `print_status_detailed_active.xml:5-7` is
`height="100%"`, consuming the content box and pushing the text past the bottom edge. View
3 escapes this only because `print_card_layout` uses `flex_grow="1"` (:138).

Resolution: gate line 182 to view 3 rather than fixing the flex. `print_status_detailed_active`
already carries its own copy, so fixing the layout would produce a real double-render. Gating
makes the scope legible and drops two wasted subject observers per widget instance.

**View 1 excluded.** Compact idle is reachable only via hand-edited `settings.json`
(`panel_widget_registry.cpp:58` sets `min_colspan=2`; every shipped layout uses 2 or 3) and
is the most space-starved surface. The layout risk outweighs the coverage.

**Styling.** `text_small` + muted token; `long_mode="dots"` on widget surfaces,
`scroll_circular` on the panel (matching :266). No `lv_tr()` on the message — user data, not
a translatable string [L070]. No new widget, so no `make regen-xml-schema` [L089].

## Testing

The delta-clobber bug shipped green. `test_display_message.cpp` feeds hand-built
`{{"display_status", {{"message", ...}}}}` objects directly into `update_from_status()` —
adequate for routing, useless for sequencing [L098, L093].

1. **Delta-sequencing regression** — M117 in notification N, `state=printing` in N+1,
   delivered as full frames via `dispatch_status_update`. Assert the message survives.
   This is the test that would have caught the reported bug.
2. **Visibility across phases** — `display_message_visible == 1` at every `PrintStartPhase`,
   including COMPLETE.
3. **Writer independence** — collector phase transitions never mutate `display_message`;
   M117 never mutates `print_start_message`.
4. **Print-end clear** — M117 present during PRINTING is cleared on the transition to each
   of COMPLETE, CANCELLED, ERROR.
5. **END_PRINT M117 survives** — an M117 arriving in a notification *after* the print-end
   transition displays and is not wiped. Guards against re-creating Defect 1 at the other
   end of the job.
6. Drain the update queue before assertions [L048].

Runtime verification beyond green tests: headless launch with a mock M117, screenshot views
0/2/3/4 and the panel mid-preheat.

## Out of scope

- **Stuck-phase TOCTOU** (`print_start_collector.cpp:302` racing the deferred
  `set_print_start_state`, leaving the phase parked at COMPLETE). Removing the `is_preparing`
  gate makes it no longer affect M117, but it remains a real bug for anything else keyed on
  `print_start_phase`. File separately.
- **Dead `PrinterState::update_from_notification()`** (`printer_state.cpp:284`) — called only
  by `test_printer_state.cpp`; production uses the MoonrakerManager queue. Means some
  envelope-parsing code the tests exercise is not the code that runs. File separately.
- View 1 compact idle surface (above).

## Classification

MAJOR — 4+ files, touches print-status paths. Use a worktree
(`scripts/setup-worktree.sh`).
