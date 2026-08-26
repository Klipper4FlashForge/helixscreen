# Design brief — XYZ tool offset calibration screen

A brief for redesigning one screen in **HelixScreen**, the touchscreen UI for a
multi-tool 3D printer. Everything below is the real behaviour of the shipping
screen; the goal is a better-designed version of it, not a new feature set.

---

## 1. The physical thing being done

The printer is a **FlashForge Creator 5 Pro**: a tool-changing 3D printer with
**four interchangeable print heads (T0–T3)** parked in docks. It picks one up,
prints with it, parks it, picks up another. For that to work it must know, to a
few hundredths of a millimetre, exactly where each head's nozzle tip sits
relative to the others. Those numbers are the *tool offsets*.

Measuring them works like this. Under the bed there is a fixed metal bore — the
**calibration station**. Two different things can be lowered into it:

- the **bare carriage** with no tool mounted — this establishes the *reference*
  point that everything else is measured against;
- a **tool's nozzle**, with that tool mounted — measured against the reference.

So the procedure is: measure the reference once, then measure each tool. Each
tool's result is independent — recalibrating T2 leaves T0, T1 and T3 valid.

**It takes real time and moves real hardware.** The reference pass takes roughly
a minute; each tool takes another minute or two, including picking the tool up
and putting it down. A full four-tool run is several minutes of the machine
moving on its own.

**Before starting, the user must physically:** take the removable build plate off
the bed (the station is *below* bed level, so the plate blocks it), and make sure
every nozzle is clean — a blob of melted plastic on a nozzle gets measured as
part of the nozzle and silently ruins the result.

The printer can detect the plate being left on and refuses with a clear message.
It **cannot** detect a dirty nozzle. That asymmetry matters for the copy.

**Nothing is permanent until saved.** Measurements apply immediately to the
running machine, but persisting them is a separate explicit action that restarts
the printer's firmware.

---

## 2. Who is looking at this

The owner of a fairly advanced printer, standing in front of the machine, having
just changed a nozzle or reassembled a tool. They are typically doing this
because something printed wrong, or because they were told to after maintenance.

They are **not** looking at this screen often — a few times a year. So it cannot
rely on remembered knowledge. It has to say what it is for, what state things are
in, and what to do next.

They are also **standing at the machine with the plate in one hand.** Tapping is
awkward. Reading a long paragraph is unlikely.

---

## 3. Hardware and platform constraints (hard limits)

| Constraint | Value |
|---|---|
| Physical screen | **800 × 480 px** capacitive touchscreen, landscape |
| Space this screen gets | **708 × 480 px** (a left nav rail takes 92 px and is always visible) |
| Input | **Finger only.** No mouse, no hover, no right-click, no keyboard |
| Minimum comfortable touch target | ~44 px; the current rows are 66 px tall |
| Theme | Dark by default; a light theme exists — never hard-code colours |
| Rendering | LVGL 9.5 (an embedded C UI toolkit) on a low-power ARM/MIPS CPU |
| Smaller variants | The same UI must degrade to **480 × 272** and **480 × 320** panels |

**What LVGL can and cannot do — please respect this:**

- **Yes:** flex row/column layout, scroll containers, rounded rects, borders,
  solid fills, opacity, icon fonts, spinners, images, simple animations.
- **No:** CSS gradients on arbitrary shapes, drop shadows with blur (expensive),
  SVG, backdrop blur, arbitrary transforms, web-style typography control.
- Fonts are **pre-baked bitmap sizes**. Roughly: body ~16 px, small ~13 px,
  heading ~20 px. Arbitrary sizes are not free — each one costs flash.
- Colours and spacing come from **design tokens**, not raw hex. Existing tokens
  include `card_bg`, `elevated_bg`, `text`, `text_muted`, `primary`, `success`,
  `warning`, `border`, and a `space_xs…space_lg` scale.

Anything requiring per-frame redraw of large areas will be visibly slow. Prefer
static layout with small state changes.

---

## 4. The data model

There are **five rows of the same kind of thing**, in a fixed order:

1. **Reference** (the station) — one per machine
2. **T0**, **T1**, **T2**, **T3** — the tools (a machine may have fewer; rows for
   absent tools are hidden)

Each row has:

| Field | Values |
|---|---|
| Identity | "Reference", or "T0".."T3" |
| Calibration state | *not calibrated* / *calibrated* / *currently measuring* |
| Its numbers | see below |
| An action | run the measurement for just this row |

**The numbers.** For the Reference row it is a single height, e.g.
`Station Z −1.679`. For a tool row it is three values:

```
dX +0.412    dY −0.087    Z +3.151
```

- `dX` / `dY` are **relative** — how far this tool's nozzle sits from **T0's**.
  Therefore **T0's dX and dY are always exactly zero**, by definition. This looks
  like a bug to anyone who doesn't know, and is a genuine communication problem.
- `Z` is **absolute** — the nozzle's height above the station. It is **not** zero
  for T0. On a healthy machine every tool's Z is around **+3.15 mm**, and a value
  far from that means something is wrong even if the machine did not complain.
- Typical `dX`/`dY` spread across four tools is well under a millimetre.
- Precision shown: **3 decimal places**. These are real, meaningful digits.

There is also **one global piece of state**: whether measurements are staged but
not yet saved.

---

## 5. What the user can do

1. **Calibrate one row** — the reference alone, or one tool alone.
2. **Calibrate everything** — reference, then all tools, in order.
3. **Stop** a run in progress. Stopping is clean: it finishes the row currently
   being measured, then stops. Rows already done keep their results.
4. **Save** — persist everything staged. This restarts the printer's firmware,
   which takes a few seconds and briefly disconnects the UI.
5. **Leave** the screen (a back arrow in the header).

Every calibration start is preceded by **one confirmation dialog** covering the
physical prerequisites (plate off, nozzles clean, tools cold).

---

## 6. The states the screen moves through

**A — Never calibrated (a new machine).** Everything reads "Not calibrated".
Printing is impossible in this state; the printer will refuse. This must be
obvious, not subtle.

**B — Partially calibrated.** Common and important: the reference exists and
three tools are done, but T2 was just given a new nozzle. Printing with T2 will
be refused; printing without it is fine. The screen must make it clear *which*
rows are the problem.

**C — Fully calibrated, nothing pending.** The steady state. Numbers on every
row. Nothing to do. This is what the user sees most often when they open it "just
to check".

**D — Running.** One row is being measured, the others wait. The machine is
moving. The user cannot do anything except stop. Expect to sit here for
**several minutes** — this is the state most in need of design attention, and the
current version handles it worst (a spinner and a line of text).

**E — Finished, unsaved.** Numbers updated, but they vanish on restart unless
saved. The most dangerous state to leave silently.

**F — Refused / failed.** The machine declined, with a specific and genuinely
useful message. Real examples, verbatim:

> `plate check: station Z 1.240 is 2.919 mm above the calibrated -1.679 — is the build plate still on?`

> `no tool is mounted. SELECT_TOOL T=<0..3> first`

> `T2 nozzle_z 6.900 is 8.579 above station_z -1.679, outside gap_min/gap_max [1.50, 5.00] — probe mis-trigger suspected, nothing saved`

These are **long** — 100–200 characters — and they are the most valuable text on
the screen when they appear. A failed run changes nothing; previous results
survive. Currently they are dumped into a small muted one-line label, which is
the single worst part of the existing design.

---

## 7. What exists today, and what's wrong with it

The current screen is a vertical list: a status line, a warning line, five rows
(each = name, numbers, a "Calibrate" button), then "Save Offsets" and
"Calibrate All" as full-width buttons pinned at the bottom. The row list scrolls
because five rows plus the buttons do not fit in 480 px.

Known problems, in the order they hurt:

1. **The running state is nearly contentless.** A small spinner and "Calibrating
   T1 — probing nozzle… 47s". For a multi-minute automated procedure where the
   machine is moving by itself, the user has no sense of overall progress, of how
   long is left, or of what is physically happening.
2. **Errors are unreadable** — long, important messages in a small muted label.
3. **The numbers are unexplained.** `dX +0.000 dY +0.000 Z +3.151` on T0 is
   correct but reads as broken. Nothing says these are millimetres, that dX/dY
   are relative to T0, or what a healthy value looks like.
4. **Two full-width buttons stacked at the bottom** ("Save Offsets", "Calibrate
   All") have equal visual weight despite being very different in consequence —
   one restarts the firmware, the other drives the machine for five minutes.
5. **Everything is the same visual weight.** The reference row is a prerequisite
   for all the others, but looks identical to them.
6. **Scrolling is a trap.** The most important thing (a failed row) can be off
   screen with no indication.
7. **"Not calibrated" vs a row of numbers** is the only distinction — there is no
   sense of "this one is suspicious", which the domain genuinely has (a Z far
   from ~3.15 mm).

---

## 8. What a good redesign would achieve

In rough priority order:

1. **State legible in one glance** — is this machine ready to print, or not, and
   if not, which rows are the problem.
2. **A running state worth looking at** for several minutes: overall progress
   across the whole sequence, not just the current row; what step is happening;
   ideally some sense of remaining time.
3. **Errors treated as first-class content**, not a status line — they are long,
   specific, and actionable.
4. **The numbers made comprehensible** without a manual — units, what they are
   relative to, and ideally what "normal" looks like.
5. **A clear action hierarchy** — the common path (calibrate everything) vs the
   surgical path (one tool) vs the consequential path (save).
6. **Unsaved work impossible to miss.**

---

## 9. Fixed vs open

**Fixed — cannot be designed away:**

- The five entities and their order; the reference is genuinely a prerequisite.
- One confirmation before any run; the physical prerequisites are real.
- Save is separate, explicit, and restarts the firmware.
- The measurement is slow, and the machine moves on its own during it.
- Stop only takes effect between rows, never mid-measurement.
- 708 × 480 px, finger input, LVGL primitives only.

**Open — please redesign freely:**

- The entire visual language: cards, list, grid, tabs, wizard, whatever fits.
- How progress is expressed during a run.
- How errors are surfaced (inline, dedicated area, expandable, modal…).
- How numbers are formatted, labelled, grouped, or visualised.
- Whether the reference is a peer row, a header, a separate step, or implicit.
- Whether "Calibrate All" is a button, a primary CTA, or a guided flow.
- Iconography and colour semantics.
- Whether the whole thing should be a multi-step flow rather than one screen.

**Two ideas worth exploring, not prescriptions:** a small diagram of the four
tools with their offsets makes the spatial relationship real in a way a table of
numbers does not; and the ~3.15 mm expected Z means "is this value sane" is
something the UI could show rather than leave to the user's judgement.

---

## 10. Deliverable

A design for the screen across states **A/B/C/D/E/F** in section 6 at
**708 × 480**, dark theme, plus a note on how it degrades to a 480 × 272 panel.
Keep it buildable with the LVGL primitives in section 3 — no gradients on complex
shapes, no blur, no SVG.
