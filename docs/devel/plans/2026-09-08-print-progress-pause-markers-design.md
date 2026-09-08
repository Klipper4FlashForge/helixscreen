# Pause markers on the print progress indicator — axis analysis

Scope: prestonbrown/helixscreen#1509, written after fixing prestonbrown/helixscreen#1510.
Point-in-time scaffolding: delete this file in the change that ships the markers.

The issue asks for markers showing scheduled gcode pauses (M600, PAUSE, slicer
pause-at-layer/height) on the print progress indicator, and names the hard part:
progress is not a file-position axis, so a pause at 60% of the gcode's byte length
does not sit at 60% of the bar. This document answers that question. It does not
describe shipped code — nothing in #1509 shipped.

## What #1510 settled

Every render surface now fills from `print_progress_display`, and only from it: the
linear bars via `bind_value` (`print_status_preview_card.xml`,
`panel_widget_print_status.xml`), the percentage label via `print_progress_text`
(written by the same call), and the detailed arc via `bind_value` as of #1510. The
raw `print_progress` subject keeps its non-render consumers
(`src/print/print_start_collector.cpp#PrintStartCollector`,
`src/application/moonraker_manager.cpp`, `PrintStatusPanel`'s lifecycle guard).

So "which subject" is answered: `print_progress_display`. The remaining question is
what that subject's number *means*, which is the axis question.

## Which axis `print_progress_display` fills

`publish_progress_display()` (`src/printer/printer_print_state.cpp#publish_progress_display`)
has exactly two writers, both in `#update_from_status`, and they are on different axes:

| Writer | Source field | Axis |
|---|---|---|
| virtual_sdcard branch | `virtual_sdcard.progress` | **byte position** — Klipper's `file_position / file_size` |
| display_status branch | `display_status.progress` | **slicer time** — the `P` value of an `M73` the slicer emitted |

Which one wins is decided at runtime by `slicer_progress_active_`
(`include/printer_print_state.h:1018`), a latch set the first time
`display_status.progress` arrives non-zero and cleared only when a new print starts.
Once set, the slicer value wins for the rest of the job, and the virtual_sdcard
branch skips its own update.

Two consequences:

1. **The axis can flip mid-print.** The latch is not known at print start. Until the
   first non-zero `display_status.progress`, the bar is filling on byte position;
   after it, on slicer time. Marker positions therefore cannot be computed once at
   load and left alone — they have to be a function of a flag that changes.

2. **The latch does not actually distinguish the two axes.** It sets on *any*
   non-zero `display_status.progress`, and Klipper reports that field whether the
   number came from an `M73` or from its own fallback when no `M73` was ever seen.
   In the fallback case `display_status.progress` and `virtual_sdcard.progress` are
   the same number and the distinction is harmless; in the `M73` case they are
   different functions of the file and the distinction is the whole problem. Nothing
   in HelixScreen currently tells the two apart. (Exactly what Klipper falls back to
   is worth confirming against hardware before relying on it — but the design below
   does not depend on the answer, because it derives the axis from the file rather
   than from the latch.)

## The reconciliation: one scan answers both questions

The two axes look irreconcilable because the mapping from file position to
slicer-time fraction lives inside the slicer. It does not: the slicer wrote that
mapping into the file. `M73 P<pct> R<minutes>` lines are emitted throughout a sliced
file, not once, and each one states the slicer's own progress at that byte offset.

So a single pass over the gcode yields, for every pause it finds, **both**
coordinates:

- byte fraction — `file_offset / file_size`, exact on the virtual_sdcard axis
- slicer fraction — the `P` of the last `M73` at or before that offset, exact on the
  `display_status` axis

and, as a by-product, the axis itself: **a file that contains `M73 P` lines is a
print whose bar will fill on slicer time; a file with none is a print whose bar fills
on byte position.** That is a property of the file, known before the print starts,
and it does not depend on reading the runtime latch or on what Klipper does when no
`M73` arrives. Store both fractions per pause, pick with the file-derived flag, and
every marker lands on the axis its bar is actually filling.

The infrastructure for the scan already exists.
`GCodeLayerIndex::build_from_file()` (`include/gcode_layer_index.h`) is a
single-pass scan that already records a byte offset per layer
(`StreamingLayerEntry::file_offset`) and already reports total bytes. Pause
detection and `M73` tracking are two more line predicates on a pass the code
already makes; nothing needs a second read of the file.

Commands to detect: `M600`; the `StandardMacroSlot::Pause` spellings already
tabulated in `src/printer/standard_macros.cpp` (`PAUSE`, `M601`); and `M0`, which is
what several pause-at-height post-processors emit. Slicer pause-at-layer and
pause-at-height are not distinct commands — they are one of the above, inserted at a
layer boundary — so detecting the commands covers them.

## The real blocker is availability, not the axis

The axis question has a clean answer. What actually stops markers from shipping as
described is that **the file is usually not there to scan.**

The only place HelixScreen fetches an active print's gcode is
`PrintStatusPanel::load_gcode_for_viewing()` — lazily, when the print status panel is
opened, and gated on `helix::is_gcode_2d_streaming_safe()` (`include/memory_utils.h`),
which declines large files on memory-constrained devices. It never runs for:

- a print the user has not opened the print status panel for
- a file over the 2D streaming size gate
- an external print start (the `[PrintStartNav] Auto-navigating to print status`
  case the issue calls out), where nothing was committed in-app
- the **home panel widget** — which is where the arc lives, and which has no gcode
  fetch of its own at all

The arc is the surface the issue most wants markers on, and it is the surface least
likely to have file data. Under the "degrade to absent, not to wrong" rule from the
issue, the arc would render bare markers-absent in the common case.

## What I would do

Three changes, in this order, each independently useful:

1. **Extend the layer-index scan** to collect a `std::vector<ScheduledPause>`, each
   entry carrying `{file_offset, byte_fraction, slicer_fraction, layer_index, kind}`,
   plus a file-level `has_m73` flag. Pure parsing work, testable against gcode
   fixtures with no UI and no printer, and correct on both axes by construction.
2. **Give the active print a place to hold that list** — a `PrinterPrintState`-owned
   vector plus a subject carrying its version, published when a scan completes and
   cleared on `reset_for_new_print()`. Absent by default, which is the correct
   degradation.
3. **Draw.** The linear bar is a `DRAW_POST` hook placing ticks at
   `x = fraction * width`. The arc is the same hook placing ticks at
   `angle = 135 + fraction * 270`, which is separate drawing and separate work. Both
   are legitimate `DECLARATIVE_OK` sites (draw hooks have no declarative equivalent —
   `.claude/rules/declarative-ui.md`).

Step 3 for the bar alone is a defensible first ship — it is the surface whose panel
already has the file. But it is worth doing step 1 and 2 first regardless, because a
correct marker list with no renderer is recoverable and a renderer fed from the wrong
axis is not.

**Deciding whether to fetch the file eagerly is a product call, not a rendering
detail**, and it is the thing that determines whether this feature is visible most of
the time. Scanning a 40MB gcode file on a MIPS box to draw three ticks is not
obviously worth it; scanning it because the print status panel was going to download
it anyway is free. That question should be settled before step 3, not after.

## What shipped

Nothing from #1509. #1510 shipped: the arc reads `print_progress_display`, so the
axis this document is about is at least a single axis per surface rather than two.
