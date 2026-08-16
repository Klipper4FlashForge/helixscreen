# Creality CFS Wrapper Internals (K1 family)

Reverse-engineering reference for the **K1-family CFS box wrapper** — the Python/C-extension
module (`box_wrapper.cpython-38-mipsel-linux-gnu.so`) that Creality's official CFS upgrade
firmware loads on K1, K1C, K1 Max, K1 SE, K2 SE, GS-01 and GS-02.

This is the *firmware side*. For how HelixScreen talks to it, see
[FILAMENT_MANAGEMENT.md § CFS](FILAMENT_MANAGEMENT.md#cfs-creality-filament-system); for the
platform, see [printers/CREALITY_K1_SUPPORT.md](printers/CREALITY_K1_SUPPORT.md).

> **Scope: K1 family only.** The K2 series ships a *different generation* of the module
> (`box_wrapper.cpython-39.so`, Python 3.9, ARM) exposing `CR_BOX_*` commands and **zero**
> `BOX_*` mid-swap primitives. Nothing on this page may be assumed to hold for K2. That
> cross-family inference is what produced #968 in the first place.

---

## Sources and evidence tiers

Three independent bodies of evidence now describe this module. They agree everywhere they
overlap, which is why the conclusions below are worth acting on.

| # | Source | What it is | Strength |
|---|--------|-----------|----------|
| **A** | Creality OTA `CR4CU220812S11_ota_img_V2.3.5.34.img` | The shipped rootfs. Extracted and symbol-grepped here; identity confirmed from `/etc/ota_info` (`ota_version=2.3.5.34`, `ota_board_name=CR4CU220812S11`, built 2025-06-12). Ships 11 per-model `box.cfg` files. | **Highest** — the artifact itself |
| **B** | [FrederickAlt/CREALITY-K1-AND-K1-MAX-CFS-RETRUDE-BEFORE-CUT-MOD](https://github.com/FrederickAlt/CREALITY-K1-AND-K1-MAX-CFS-RETRUDE-BEFORE-CUT-MOD) `docs/` | ~230 KB of behavioral documentation from decompiling the Cythonized module back to `box_wrapper.py`, written explicitly for independent-wrapper authors. Surfaced by @NilsOF on #968 (2026-08-15). | **High** — decompilation, author disclaims correctness |
| **C** | #968 reporter's own `box.cfg` + console tests on a live K1C v2.3.5.33+ | Field observation on real hardware. | **High for what it covers**, narrow |

Source B has since been **checked against A on every command claim in this document and found
accurate** — including the parameter lists, the `tn_data.json` field names (`vender`
misspelling and all), and four separate runout give-up literals. That is a strong signal for
the parts of B we cannot verify directly, but it is not a licence to skip verification: B's
behavioral claims (sequencing, retry counts, timeouts) are still decompiler-derived.

### Reproducing the extraction

The image is an AES-encrypted 7z. The widely-circulated community password is dead on the
`2.3.5.x` generation; the current one is board-derived (`mkpasswd -m md5
<BOARD>C3_7e_bz -S cxswfile`), which for `CR4CU220812S11` yields
`$1$cxswfile$u/lZJ5DdXlEL9mVsL/rt70`. Then reassemble and unpack:

```bash
cat rootfs.squashfs.[0-9]* > rootfs.sqfs      # 156 chunks
unsquashfs -d rootfs rootfs.sqfs
strings -n 6 rootfs/usr/share/klipper/klippy/extras/box_wrapper.cpython-38-mipsel-linux-gnu.so \
  | grep -oE '\b(CR_)?BOX_[A-Z0-9_]+' | sort -u
```

Throughout this doc, claims are tagged **[A]**, **[B]**, **[C]** by which source establishes
them. Where B is the *only* source, treat it as strong-but-unverified: it is decompiler output
one person's understanding removed, and its own README says so.

> **Two evidence traps, restated.** Neither `box.cfg` nor `printer.gcode.help` can prove a
> `BOX_*` command **absent**. The commands are registered from the C extension, not as
> `[gcode_macro]`s, and the extension carries 5 help strings against 69 handlers — roughly 64
> commands are executable and invisible to `gcode/help`. Only a symbol grep of the `.so`
> settles presence. We have already shipped one wrong conclusion (`BOX_SAVE_FAN`) by ignoring
> this. Source B is a decompilation and therefore *can* speak to absence, but confirm against
> A before acting.

---

## The one thing to internalize

**The `BOX_*` primitives are not a public API with clean success/failure semantics. They are
the internals of a workflow engine, and HelixScreen drives them directly.**

Source B is emphatic and repeats it in four separate documents: a wrapper that composes the
low-level phases instead of calling the firmware's own `T*` entrypoint **takes over
responsibilities the firmware would otherwise have handled** — remap resolution, phase
sequencing, and above all *verification*. Its summary table:

| Full `T*` action (firmware-driven) | Explicit lower-level sequence (**what HelixScreen does**) |
|---|---|
| Wrapper resolves `Tnn_map` | Caller must pass the intended physical slot, or mirror the remap itself |
| Wrapper owns phase sequencing | Caller owns cut/retrude/load/flush order |
| Wrapper handles phase return values | **Caller must add visible checks after critical phases** |
| Flush is part of the hidden sequence | Caller decides when to flush |

This single fact explains most of the open questions on #968, including the ones that read
like firmware bugs.

---

## Command reference

### Commands HelixScreen emits today

| Command | Parameters | Notes |
|---------|-----------|-------|
| `BOX_ERROR_CLEAR` | — | Clears the active recovery condition, **and discards queued macro retry work**. May set the affected box `IDLE`. [B] |
| `BOX_CHECK_MATERIAL` | — | A `[gcode_macro]` (not a C command) wrapping `WAIT_EXTRUSION_ALL_MATERIALS`. [A][C] |
| `BOX_GO_TO_EXTRUDE_POS` | — | Move to configured extrude position. **No-ops during resume.** [B] |
| `BOX_CUT_MATERIAL` | — | Closes model fan, runs the cutter flow. Can home/move axes and change acceleration. Records `cut_err` and **queues the command line** on failure. No-ops during resume. [B] |
| `BOX_RETRUDE_MATERIAL` | — | Unloads the *currently active* material. Records `retrude_err` and queues on failure. No-ops during resume. [B] |
| `BOX_EXTRUDE_MATERIAL` | `TNN=<Tnn>` | Box-side feed. **Always pass `TNN`.** Queues when another error is active. No-ops during resume. [A][B] |
| `BOX_EXTRUDER_EXTRUDE` | `[TNN=<Tnn>]` | Toolhead-extruder-side grab. Separate failure class (`extruder_extrude_err`). No-ops during resume. [A][B] |
| `BOX_MATERIAL_FLUSH` | `[LEN]` `[VELOCITY]` `[TEMP]` `[PERCENT]` | **No `TNN` parameter** — see below. Runs nozzle clean + small retract afterward. No-ops during resume. [A][B] |
| `BOX_NOZZLE_CLEAN` | — | Wipe/fan sequence. [A][B] |
| `BOX_MOVE_TO_SAFE_POS` | — | Parks at `safe_pos_y` if XY is homed. [A][B] |
| `BOX_SAVE_FAN` / `BOX_RESTORE_FAN` | — | Suppress and restore part-cooling around the whole operation. Present on K1 [A]; added to the K1 envelope for #1278. |
| `BOX_MODIFY_TN` | `T1A=T2C` | Remap table write; persists all 16 keys, prints nothing. Applies to the slicer's `T0`-`T15`, not to our `TNN=` primitives — see [Tool remap](#tool-remap-box_modify_tn---corrected-no-defect). [A][B] |
| `BOX_MODIFY_TN_DATA` | `ADDR=` `PART=` `[NUM=]` `DATA=` | Live state edit. `PART` names match `tn_data.json` field names. [B] |
| `BOX_ENABLE_AUTO_REFILL` | `ENABLE=<0\|1>` | Runtime flag only; **separate from** the persisted `BOX_ENABLE_CFS_PRINT`. [B] |

`BOX_MATERIAL_FLUSH`'s parameter list is now confirmed by all three sources independently
(shipped `box.cfg` comment blocks [A], disassembly of `cmd_material_flush` [A], and source
B's command catalog). `PERCENT` is new information from B — we were not aware of it, and the parameter string is present in the extension [A]. Values
default from `[box]` keys `Tn_retrude`, `Tn_extrude_velocity`, `Tn_extrude_temp`; `LEN` is
capped at 200 and `TEMP` outside 180–300 silently falls back to the configured value. [B]

### Commands we do not emit but probably should

| Command | Parameters | Why it matters |
|---------|-----------|----------------|
| `BOX_MATERIAL_CHANGE_FLUSH` | `[LAST_TNN=]` `[TNN=]` | The **color-aware** flush, used by the internal `T*` path. Deliberately not adopted — see [Flush](#flush-two-legitimate-choices-and-we-make-the-vendors). |
| `BOX_TNN_RETRY_PROCESS` | — | The firmware's own recovery entrypoint. We only ever offer `BOX_ERROR_CLEAR`. |
| `BOX_EXTRUSION_ALL_MATERIALS` | — | Purges "ending material" left in the path after a runout or cut. |
| `BOX_SHOW_TNN_INNER_DATA` | — | Dumps live box state — a diagnostic we could surface in debug bundles. |
| `BOX_GET_FILAMENT_SENSOR_STATE` | `ADDR=` `POSITION=MATERIAL\|CONNECTIONS` | Live sensor mask. The verification primitive source B says a composing wrapper needs. |
| `BOX_GET_BUFFER_STATE` | `ADDR=` | Buffer occupancy; the load-completion signal. |

---

## Findings

Status key: **Fixed** — corrected in the tree · **Open** — real, not yet acted on ·
**Corrected** — we believed something false; the belief is what changed.

### Tool remap (`BOX_MODIFY_TN`) — *Corrected, no defect*

Our code carried, in three places, the claim that *"on K1, BOX_MODIFY_TN is a confirmed
no-op."* That is **wrong**, and it originated in a misreading of the #968 reporter's console
test.

`BOX_MODIFY_TN T1A=T2C` works on K1. It updates the in-memory remap table and persists the
whole 16-key map to `tn_data.json`. It just produces **no console output**, which is exactly
what the reporter saw and reported as "did not appear to do anything." [B]

It also lands where we need it. The remap exists for the **slicer's** in-print tool changes,
and those arrive as `T0`–`T15` — precisely the entrypoint that resolves `Tnn_map`:

```text
T0..T15  ->  default physical slot (Tn_map)  ->  actual physical slot (Tnn_map)  ->  hardware
T1A..T4D ->                                      actual physical slot (Tnn_map)  ->  hardware
BOX_EXTRUDE_MATERIAL TNN=T1A  ->                                                     hardware
```

Our own manual load/swap takes the bottom path and bypasses the table — which is **correct**,
because at that point we already know the physical slot we want and do not want a stale remap
redirecting it.

What *is* genuinely K1-specific is the absence of an echo: the K1 box status publishes no
`map` field, so no frame confirming a remap ever arrives. That is why
`reports_firmware_tool_mapping()` returns false on K1, and that behaviour was always right —
only its stated reason was wrong. Both have been corrected in
`src/printer/ams_backend_cfs.cpp`.

### Flush: two legitimate choices, and we make the vendor's — *Open, not a defect*

`swap_gcode()` emits a bare `BOX_MATERIAL_FLUSH` for a material change. There is a second,
color-aware command:

```gcode
BOX_MATERIAL_CHANGE_FLUSH LAST_TNN=<old slot> TNN=<new slot>
```

Both are confirmed present at tier **[A]** — distinct handlers `cmd_material_flush` and
`cmd_material_change_flush`, with `LAST_TNN` in the parameter string pool. The difference is
flush **length**: [B]

```text
if no previous material or either color unknown:
    use configured Tn_extrude          <-- what the bare flush always does
else:
    color-distance flushing volume + nozzle_volume, converted to length,
    times flush_multiplier
```

**Why we are staying on the bare flush.** Our K1 swap is modelled on the firmware's own
`BOX_LOAD_MATERIAL_WITH_MATERIAL` macro — the thing Creality ships for a user-initiated
material change — and that macro calls `BOX_MATERIAL_FLUSH` **bare**, verified in all 11
shipped `box.cfg` files [A]. So our current emission matches vendor guidance for this
operation. `BOX_MATERIAL_CHANGE_FLUSH` is what the *internal* `T*` path uses for in-print
tool changes, which is a different operation with a different caller.

Switching would mean a purge-length change on hardware nobody here owns: potentially much
longer purges into a finite waste chute, plus the segmented-flush path's measuring-wheel
blockage detection (`diff_length`) introducing a "nozzle blocked" failure mode the bare flush
does not have. The upside is only cosmetic (less colour bleed on dark→light swaps). That
trade needs a K1 + CFS to settle, so it stays behind the #1278 gate — now on [A] evidence
rather than a guess.

`swap_gcode()` would also need the *outgoing* slot, which it is not currently passed;
`do_change_tool` has it in `system_info_.current_slot` before overwriting it.

### Failures are deferred, not raised — *Fixed (host-side verification)*

Every K1 primitive we emit **records an error and queues the command line for later replay**
rather than failing at the point of failure. [B] A HelixScreen sequence can therefore run to
completion, with every RPC returning success, while the load silently did not happen.

Source B's rule for wrappers in our position is blunt: *"after every critical phase, verify the
externally visible result before continuing."* The checks it names:

- local toolhead filament sensor clear after unload/retrude;
- target slot reported loaded after `BOX_EXTRUDE_MATERIAL`;
- target slot still loaded after flush;
- target box not in `PRELOADING` or error mode before the next material command.

We used to do none of these, which is the most likely explanation for the #968 reporter's
"screen showed the wrong process and was stuck at step 2": our progress model advanced on
gcode acceptance, and gcode acceptance means nothing here.

**Now implemented**, in the one place we have independent evidence. On completion,
`AmsBackendCfs::finish_action()` checks the toolhead filament switch against the operation's
latched intent (`AmsBackendCfs::verify_phase_outcome()`, a pure function):

| Intent | Expected end state | Verdict on mismatch |
|---|---|---|
| `LOADING` (load or swap) | filament at the nozzle | `LoadDidNotReachNozzle` |
| `UNLOADING` | no filament at the nozzle | `UnloadLeftFilament` |
| anything else | — | always `Ok` |

On a mismatch the backend enters `AmsAction::ERROR` with an explanatory
`operation_detail`, and `AmsErrorBridge` presents it via the new
`AmsBackendCfs::current_error()` with the same Resume / Reset CFS pair the runout path uses.

Three design points worth keeping:

- **The intent is latched at dispatch**, not read at completion.
  `apply_synthesized_action_locked()` overwrites `system_info_.action` with the synthesized
  sub-phase (CUTTING/PURGING) as signals arrive, so by completion it no longer says what the
  user asked for. `PhaseTracker::intent` is the durable copy.
- **No sensor reading means no verdict.** Klipper publishes `filament_detected` as null until
  the switch first reports, and a machine without the switch never publishes at all — the
  default would otherwise read as "load failed" on every successful load. `Unverifiable` is a
  deliberate outcome, not an oversight.
- **No timer is involved.** The status subscription and the RPC reply share one ordered
  WebSocket, and the switch trips seconds before the script drains (flush, wipe and park all
  follow), so the sensor state is already settled when the completion callback runs.

Still not verified: the *slot* actually loaded. The box's own reported slot is not independent
evidence on K1 — it names a cassette-staged slot even with an empty nozzle, which is the
original #968 phantom-cut bug — so the toolhead switch is the only witness worth trusting.

### `BOX_ERROR_CLEAR` is not free — *Open, judgement call*

We open **every** K1 load, unload and swap with `BOX_ERROR_CLEAR`. Source B: it clears the
active recovery condition, may set the affected box `IDLE`, and **discards queued macro retry
work** — the deferred phases described above.

The firmware's paired command is `BOX_TNN_RETRY_PROCESS`, which actually retries the failed
phase and can resume a paused print. B's guidance is that clear is for *"hardware has been
inspected and you do not want automatic retry"*, which is not the situation at the start of a
user-initiated load. Worth revisiting whether the unconditional clear is right, and whether
our "Reset CFS" recovery button should offer retry alongside clear.

### Resume is a silent trap — *Open*

Seven commands **log a warning and do nothing** while the printer is in resume handling: [B]

`BOX_EXTRUDE_MATERIAL` · `BOX_RETRUDE_MATERIAL` · `BOX_EXTRUDER_EXTRUDE` ·
`BOX_MATERIAL_FLUSH` · `BOX_GO_TO_EXTRUDE_POS` · `RESTORE_POSITION` · `BOX_CUT_MATERIAL`

That is essentially our entire K1 body. A load issued during resume executes zero useful work
and reports success at every layer. The firmware's dedicated path is `BOX_RESUME_EXTRUDE`,
which only runs when the resume target and the active material resolve to the same slot.

Note the exception, which is a hazard in the other direction: `BOX_RETRUDE_MATERIAL_WITH_TNN`
is **not** resume-guarded and *will* move material during resume. [B]

### Runout give-up wordings — *Fixed*

There are **four** give-up literals, all read directly out of the extension [A]:

| Firmware literal | Meaning | Matched by |
|---|---|---|
| `no identical supplies` | No compatible `same_material` group exists at all | `"identical suppl"` |
| `disable material automatic refill` | Auto-refill is switched off | `"automatic refill"` + `"disab"` |
| `no auto refill` | Same cause, terse spelling | `"no auto refill"` |
| `no tray with ingredients found` | A compatible group exists, but none of its slots has material at the sensor | `"tray with ingredient"` |

`classify_error()` previously matched only the first two, plus a weak tier requiring the
substring `"refill"` **and** the box's own runout latch.

Both missing literals fell through everything. `no tray with ingredients found` contains no
`"refill"` at all, so even the weak tier could not see it. `no auto refill` says "auto
refill", not "automatic refill", and never says "disab" — so it could only ever reach the weak
tier, and only when the latch happened to be set. Either way the user got a paused print and
no modal.

Both are now matched, with `no tray...` getting its own message: the fix there is to load one
of the *known matching* slots, which is different advice from "go find matching filament".
Tests: `[ams][cfs][968]` in `tests/unit/test_ams_backend_cfs.cpp`.

### Part-cooling left on through every K1 operation — *Fixed*

The K1 envelope omitted `BOX_SAVE_FAN` / `BOX_RESTORE_FAN` on the grounds that they were
"verified absent in the public K1-Max `box.cfg` dump". That evidence was structurally
incapable of showing what it was claimed to show — both are C-extension commands
(`cmd_save_fan` / `cmd_restore_fan`), never `[gcode_macro]`s, so no config dump could list
them. Symbol grep confirms both present [A].

The consequence was that every K1 load, cut and flush ran with the part-cooling fan blowing
across the nozzle — exactly what the K2 envelope has always suppressed. Now emitted, ordered
to mirror K2: save before the body (so the cut is covered), restore before the park. The
`dispatch_action_script` error unwind, which was gated to K2 for the same wrong reason, now
runs on K1 too. `BOX_MODE_WAIT` really is absent and stays out. (#1278)

---

## `tn_data.json` — the persistence contract

`creality/userdata/box/tn_data.json`. This closes the open ask from #968 comment 5 (we asked
the reporter for this file and never received it).

```json
{
  "base_data": {
    "T1": {
      "vender":        ["-1", "-1", "-1", "-1"],
      "remain_len":    ["-1", "-1", "-1", "-1"],
      "color_value":   ["-1", "-1", "-1", "-1"],
      "material_type": ["-1", "-1", "-1", "-1"]
    },
    "T2": { "...": "same shape" },
    "T3": { "...": "same shape" },
    "T4": { "...": "same shape" }
  },
  "enable": 1,
  "tnn_map": { "T1A": "T1A", "T1B": "T1B", "...": "all 16 keys", "T4D": "T4D" },
  "last_cmd": "T1A"
}
```

Every `base_data` list is four elements in **A, B, C, D** order. Sentinel values: `"-1"`
uninitialized, `"none"` explicitly empty, `"unknown"` present but unidentified. [B]

| Field | Lifecycle |
|-------|-----------|
| `base_data` | Long-lived identity cache. **Survives** end-print and power-loss cleanup. |
| `enable` | Written by `BOX_ENABLE_CFS_PRINT`. Removed by cleanup. |
| `tnn_map` | Written by `BOX_MODIFY_TN` and by auto-refill. Removed by cleanup; end-print also resets the live map to identity. |
| `last_cmd` | Active physical slot; written when a load starts while printing or paused. Removed by cleanup. |

**This is wrapper state, not box state.** The box does not need the file to load, unload, cut,
or report sensors. Corrupting it confuses restore and remap; it does not move motors. [B]

### What this settles for material-type writeback

The blocker on #968 comment 5 item 2 was not knowing the field name or format, and the risk
that a malformed value takes Klipper down. The `PART=` argument to `BOX_MODIFY_TN_DATA` uses
the **same spellings as the JSON fields**, which our existing color sync already demonstrates
(`PART=color_value`, `src/printer/ams_backend_cfs.cpp:1856`). So:

```gcode
BOX_MODIFY_TN_DATA ADDR=<1..4> NUM=<A|B|C|D> PART=material_type DATA=<value>
```

Caveats before wiring it:

- **`vender` is misspelled** in both the JSON and the `PART` argument. Preserve it exactly.
- The **value format** for `material_type` is still unknown. The field name is confirmed [A],
  but B calls the value only a "material type identifier" used for same-material grouping and
  material-database lookups, without giving the domain. The extension contains **no
  material-name strings at all** — no `PLA`, `PETG`, `ABS` anywhere in the blob — which points
  at a numeric code resolved against an external table, consistent with the `cfs` scheme in
  `assets/filament_catalog.json`. Suggestive, not proof. Read a populated `tn_data.json` from
  a live CFS before writing one.
- Writes are **not atomic** — the file is rewritten in place, not swapped through a temp file.
  Do not edit it while the wrapper is running. [B]
- Never write a **partial** `tnn_map`: restore expects all 16 keys when the object is
  non-empty, and a partial map can fail restore. [B]

---

## Behavioral model

### Box modes

| Code | Name | Meaning |
|-----:|------|---------|
| 0 | `IDLE` | Idle |
| 1 | `PRELOADING` | Preloading material — **wait for this to clear before any material command** |
| 2 | `PRINTING` | Print/feed mode |
| 3 | `WRAPPERING` | Observed name; meaning unclear [B] |
| 4 | `ERR` | Error |
| 5 | `TEST` | Test |

### Sensor masks

Slot bits are `A=0x01, B=0x02, C=0x04, D=0x08`. Two independent masks — **material** (filament
at the slot's material sensor) and **connection** (detected at the five-way sensor). Buffer
state is scalar: `0` = middle/not full, `>= 1` = full/ready. [B]

### Staged loading

`BOX_EXTRUDE_MATERIAL` is not one round trip. The firmware runs a stage machine: [B]

```text
stage 0  start/connection setup
stage 4  begin material extrusion
stage 5  poll loading progress        <-- polled for up to ~90 s
stage 6  advance/recover
stage 7  final extrusion toward buffer-full
final    wrapper-side buffer/extruder verification, one extruder-gear retry on failure
```

Before surfacing a failure it attempts four bounded recovery strategies — connection retry,
filament-sensor retry, second filament-sensor retry, extruder-gear retry (which cuts,
retrudes, repositions and restarts from stage 0). **A visible load failure means all of those
already failed**, so an aggressive HelixScreen-side retry on top is redundant and probably
unsafe.

### Serial timeouts

Relevant because these bound how long a single `BOX_*` gcode can block. [B]

| Operation | Timeout |
|-----------|--------:|
| Simple queries / control | 2 s |
| `EXTRUDE_PROCESS` (per call, called repeatedly) | 15 s |
| `RETRUDE_PROCESS`, `EXTRUDE_PROCESS_MODEL2`, `TIGHTEN_UP_ENABLE` | 150 s |
| `SET_PRE_LOADING RUN`/`TIGHT` | 300 s **× slots selected** (up to 1200 s) |
| `GET_BOX_STATE` | 3600 s (returns no data on timeout rather than failing) |

A four-slot preload can legitimately block for twenty minutes. Any HelixScreen-side timeout on
CFS gcode needs to accommodate that or it will report a spurious failure over working
hardware.

### Error phases

`cut_err` · `retrude_err` · `box_extrude_err` · `extruder_extrude_err` · `flush_err` ·
`filament_err` · `empty_print`

**Only one recovery condition is active at a time** — the first failure is retained and later
symptoms are hidden behind it. Diagnose from the phase and live sensor state, not from message
text, which B notes is the least stable surface. [B]

---

## Open questions

| Question | Why it's open | What would settle it |
|----------|--------------|---------------------|
| `material_type` value domain | Field name confirmed [A], but the extension carries **no material-name strings** (no `PLA`/`PETG`/…), consistent with a numeric code resolved against an external table — matching our `cfs` scheme in `assets/filament_catalog.json`. Not proven. | Read `tn_data.json` from a live CFS with identified spools |
| Does K2's module read `Tnn_map` the same way? | This page is K1-only. The K1 answer is now settled (it does, via `T*`), but K2 ships a different module generation. | Symbol-grep the K2 `.so`, or a live K2 remap test |
| Should swaps use `BOX_MATERIAL_CHANGE_FLUSH`? | Command and params verified present [A]; the trade is purge length + a new blockage failure mode vs. colour bleed | K1 + CFS hardware |
| `#1278` motion-sequence divergences | Deliberately unresolved — changing them blind risks the belt-skip / chute-collision class already reported | K1 + CFS hardware |
| Bed-area shrink (~5 mm Y) for the rear-mount upgrade | Not applied via printer database | Confirmed `position_max` from a CFS-equipped K1 |

The command surface is now settled from the artifact, but **no behaviour on this page has been
validated against a running K1 + CFS**, which is why #968 stays open. Nobody on the project
has that hardware.

---

## Appendix: verified command inventory

Every `BOX_*` string in `box_wrapper.cpython-38-mipsel-linux-gnu.so` (v2.3.5.34, MIPS32,
stripped). **Zero `CR_BOX_*` entries** — that dialect is K2-only, and its absence here is the
whole of #968.

```
BOX_ADD_TNN                        BOX_GET_RFID
BOX_BLOW                           BOX_GET_VERSION_SN
BOX_CHECK_MATERIAL_REFILL          BOX_GO_TO_BOX_EXTRUDE_POS
BOX_COMMUNICATION_TEST             BOX_GO_TO_EXTRUDE_POS
BOX_CREATE_CONNECT                 BOX_MATERIAL_CHANGE_FLUSH
BOX_CTRL_CONNECTION_MOTOR_ACTION   BOX_MATERIAL_FLUSH
BOX_CUSTOM_COMMAND                 BOX_MEASURING_WHEEL
BOX_CUT_HALL_TEST                  BOX_MODIFY_TN
BOX_CUT_HALL_ZERO                  BOX_MODIFY_TN_DATA
BOX_CUT_MATERIAL                   BOX_MODIFY_TN_INNER_DATA
BOX_CUT_STATE                      BOX_MOVE_TO_CUT
BOX_ENABLE_AUTO_REFILL             BOX_MOVE_TO_SAFE_POS{,1,2,3,6}
BOX_ENABLE_CFS_PRINT               BOX_NOZZLE_CLEAN
BOX_END                            BOX_NUM_POS
BOX_END_PRINT                      BOX_POWER_LOSS_RESTORE
BOX_ERROR_CLEAR                    BOX_RESTORE_FAN
BOX_EXTRUDER_EXTRUDE               BOX_RESUME_EXTRUDE
BOX_EXTRUDE_2_PROCESS              BOX_RETRUDE_MATERIAL
BOX_EXTRUDE_MATERIAL               BOX_RETRUDE_MATERIAL_WITH_TNN
BOX_EXTRUDE_PROCESS                BOX_RETRUDE_PROCESS
BOX_EXTRUSION_ALL_MATERIALS        BOX_SAVE_EXTRUDE_POS
BOX_FIND_CUT_POS                   BOX_SAVE_FAN
BOX_GENERATE_FLUSH_ARRAY           BOX_SEND_DATA
BOX_GET_BOX_STATE                  BOX_SET_BOX_MODE
BOX_GET_BUFFER_STATE               BOX_SET_PRE_LOADING
BOX_GET_FILAMENT_SENSOR_STATE      BOX_SHOW_TNN_INNER_DATA
BOX_GET_FIVE_WAY_STATE             BOX_START_PRINT
BOX_GET_FLUSH_LEN                  BOX_START_PRINT_EXTRUDE_MATERIAL
BOX_GET_FLUSH_VELOCITY_TEST        BOX_TIGHTEN_UP_ENABLE
BOX_GET_REMAIN_LEN                 BOX_TNN_RETRY_PROCESS
                                   BOX_TN_EXTRUDE
                                   BOX_UPDATE_SAME_MATERIAL_LIST
```

Notable absences and additions:

- **`BOX_MODE_WAIT` is not here.** Confirms the K1 envelope is right to omit it.
- **`BOX_CHECK_MATERIAL` is not here either** — it is a `[gcode_macro]` in `box.cfg`, not a C
  command, which is why we can emit it. Same for `BOX_LOAD_MATERIAL_WITH_MATERIAL`,
  `BOX_LOAD_MATERIAL_WITHOUT_MATERIAL`, `BOX_QUIT_MATERIAL` and `BOX_INFO_REFRESH`.
- `BOX_MOVE_TO_SAFE_POS1/2/3/6`, `BOX_NUM_POS` and `BOX_START_PRINT_EXTRUDE_MATERIAL` appear
  in neither source B nor any `box.cfg`. Undocumented; do not use without investigation.

Parameter tokens present in the string pool: `ACTION` `ADDR` `DATA` `ENABLE` `LAST_TNN` `LEN`
`MODE` `NUM` `PART` `PERCENT` `POSITION` `TEMP` `TNN` `TRIGGER` `VELOCITY`.

`tn_data.json` field names present verbatim: `base_data` `color_value` `last_cmd`
`material_type` `remain_len` `tnn_map` `vender` (misspelling confirmed at source).

---

## Related

- [FILAMENT_MANAGEMENT.md § CFS](FILAMENT_MANAGEMENT.md#cfs-creality-filament-system) — HelixScreen's backend, dialect table, schema detection
- [printers/CREALITY_K1_SUPPORT.md](printers/CREALITY_K1_SUPPORT.md) — K1 platform, firmware prerequisites
- [printers/CREALITY_K2_SUPPORT.md](printers/CREALITY_K2_SUPPORT.md) — K2 series and the community Kalico port
- [#968](https://github.com/prestonbrown/helixscreen/issues/968) — K1/K1C CFS compatibility
- [#1278](https://github.com/prestonbrown/helixscreen/issues/1278) — K1 sequence divergences pending hardware
