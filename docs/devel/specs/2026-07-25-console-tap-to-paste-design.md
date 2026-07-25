# Console Tap-to-Paste + Keyboard Long-Press Documentation — Design Spec

**Date:** 2026-07-25
**Status:** Approved design, pre-implementation
**Author:** Preston Brown (with Claude)
**Origin:** Discord feature request (Sunny, clarified by Mud)

## Summary

Two independent changes from one Discord thread.

1. **Tap-to-paste** in the G-code console: tapping a previously-sent command in the
   console log copies it back into the input field, ready to edit and re-send. This is
   Fluidd's behavior, and it gives touch-only users the command recall that today is
   reachable only from a USB keyboard.
2. **Document the long-press keyboard mapping.** The long-press mechanism is documented;
   the actual key-to-character mapping is not, anywhere. Users cannot find `_` without
   hunting key by key.

Both are user-facing, so both ship with user-doc updates.

## Origin and what the users actually reported

Sunny asked for `_ - + =` on the main keyboard layer, saying underscore sits "behind 2
clicks", and separately asked to "repeat previous sent macro". Mud clarified the second
one: in the Fluidd console, clicking a macro in the log puts it back in the terminal so
you can send it again.

The underscore complaint is a **discoverability** problem, not a reachability one.
`_`, `-`, and `+` are already on the alpha layer as long-press alternates
(`ui_keyboard_manager.cpp:39`, `f`→`_`, `h`→`-`, `j`→`+`), `auto_insert_alt_` defaults
to `true` (`ui_keyboard_manager.h:179`) so a hold inserts the character immediately with
no picker, and the keyboard already draws a hint glyph in each key's top-right corner
(`ui_keyboard_manager.cpp:714-745`). Sunny was using `?123`→`#+=`→`_` because nothing
told them holding `f` works. The fix is documentation, not layout.

## Part 1 — Tap-to-paste (approved decisions)

- **Gesture:** single tap on a command line in the console log.
- **Effect:** the command text is placed in the `gcode_input` field. **Refill only —
  nothing is sent.** The user reviews, edits if they want, and taps send. A mis-tap
  must never re-run a printer command.
- **What is tappable:** `GcodeEntry::Type::COMMAND` entries only. Klipper responses
  (green) and errors (red) stay inert.
- **Commands from other clients are tappable too.** `fetch_history()` pulls
  `server.gcode_store` from Moonraker, which includes commands sent from Mainsail,
  Fluidd, or a terminal. Those arrive as `COMMAND` entries and are legitimate paste
  sources. This is a feature, not a leak.
- **No keyboard raise on tap.** Pasting does not focus the input or slide the keyboard
  up. The keyboard covers the lower half of the log the user is reading; making them
  dismiss it to keep browsing is worse than making them tap the field when they actually
  want to edit.
- **Reach:** `MAX_ENTRIES = 200` log entries versus `MAX_HISTORY = 20` for the readline
  ring. Tap-to-paste reaches roughly ten times further back than the Up-arrow ever could.

### Architecture

**Touch point:** `ConsolePanel::create_entry_widget()`, `src/ui/ui_panel_console.cpp:555`.

Both render paths need the treatment. The function branches on
`show_timestamps_ || has_html`: a `lv_spangroup` (line 594) or a plain `lv_label`
(line 625). The timestamps setting decides which one runs at runtime, so tap-to-paste
must work on both or it will silently stop working when a user toggles timestamps or a
firmware message arrives with HTML spans.

**Carrying entry identity into the click handler.** Add a monotonic `uint64_t id` to
`GcodeEntry`, assigned on creation. Store it in the widget's user_data *as an integer
value*, `(void*)(uintptr_t)id`. On click, scan `entries_` for the matching id and paste
its `message`. A linear scan over at most 200 entries on a human-speed gesture is free.

Two alternatives were considered and rejected:

| Approach | Why not |
|---|---|
| Child index → `entries_` index | `add_entry()` prunes from the front once over `MAX_ENTRIES` (line 820-831), so indices shift under a live widget. `empty_state_` also lives in the same container and offsets the mapping. |
| Heap `std::string*` in user_data | Works, but requires a per-widget `LV_EVENT_DELETE` watcher to free it, which is the exact footgun recorded in `project_child_widget_needs_own_delete_watcher`. An integer id has nothing to dangle and nothing to free. |

**Paste path.** `entry.message` is already the bare command — the `> ` prefix is added at
render time (lines 611 and 627), not stored — so there is nothing to strip.

```
lv_textarea_set_text(gcode_input_, entry.message.c_str());
history_index_ = -1;
saved_input_.clear();
```

**The `history_index_` reset is required, not cosmetic.** The readline walk
(`ui_panel_console.cpp:362-400`) keeps `history_index_` as a cursor into
`command_history_` and `saved_input_` as the user's in-progress text. If a tap sets the
input text without resetting them, a subsequent Up-arrow resumes a stale walk from
wherever the cursor was left, and a Down-arrow past the newest restores a `saved_input_`
that no longer relates to anything on screen. The two recall mechanisms coexist and do
not share state; the tap path must leave the readline cursor neutral.

**Scroll interaction: nothing to do.** LVGL gates `LV_EVENT_CLICKED` behind
`scroll_obj == NULL` (`lib/lvgl/src/indev/lv_indev.c:1526`), so flicking the log to
scroll never fires a tap on a line. This is why `MacrosPanel` hand-rolls a
`lv_indev_get_scroll_obj()` guard only for `LONG_PRESSED`
(`src/ui/ui_panel_macros.cpp:485-499`) — that event fires on hold duration alone and
bypasses the check. Since tap-to-paste defines no long-press action, it inherits the
framework's suppression for free.

**Affordance.** Command lines are already visually distinct: a `> ` prefix and the
`text` color, against green responses and red errors. Add a pressed-state background
tint so the tap is visibly confirmed. Touch has no hover, so beyond that it is
discovery-by-trying — same as Fluidd — which is why the user-doc entry below matters.

### Testing

`tests/unit/test_ui_panel_console.cpp` is pure-logic: free functions and standalone
container semantics (see the existing `"Console: command history deque operations"` case
at line 267). There is no LVGL widget fixture for this panel, and per
`docs/devel/specs/2026-07-20-print-status-panel-test-isolation.md` panel-lifetime tests
are a known trap. So the seam goes where it can be tested without widgets:

- Extract the id lookup as a free function over the entry container, and test:
  found; not-found because the entry was pruned; ids remain unique across a prune cycle
  so a recycled widget can never resolve to the wrong command.
- Test the readline-cursor reset semantics on the container logic directly, mirroring
  the existing deque test: after a paste, the cursor is neutral and a subsequent Up
  starts from the newest command rather than resuming mid-walk.
- Mutation-verify both: break the reset, and the cursor test must fail.

The pressed-state tint and the actual tap gesture are verified at runtime on device, not
in unit tests.

### Documentation

`docs/user/guide/advanced.md` § G-code Console gains a **tap to paste** entry. The
existing "Sending commands" numbered list (line 17-21) is the natural anchor. Cover:
tap a `>` line to put it back in the input, it does not send until you tap send, and it
works for commands sent from other clients too.

## Part 2 — Keyboard long-press: hint placement, slide-to-select, documentation

Scope here grew after the initial design review. Two behavioural requirements were
added: hints must stay **inside their key at every breakpoint**, and an exposed
alternate must be **selectable by sliding onto it**. Investigating those turned up
two defects.

### 2a. Hint placement used a parallel reimplementation of the layout

`keyboard_draw_alternative_chars()` re-derived each key's rectangle by hand:
`unit_width = kb_width / 40`, `row_height = kb_height / 4`, and a width table that
recognised only 2-, 4- and 6-unit keys. All three disagree with what LVGL actually
laid out. The integer division truncates and the error compounds across a row; the
padding, row gaps and border are ignored; and the real control maps use widths of
2, 3, 4, 6 and 12, with the `#+=` layer giving backspace 4 units where the table
assumes 6. The resulting drift scales with resolution, so hints landed off their
keys at some breakpoints and over neighbours at others.

**Fix:** read the rects LVGL already computed. `lv_buttonmatrix_t::button_areas[]`
holds them, relative to the widget origin (`lv_buttonmatrix.c:682-686` copies then
offsets), so a new file-local `buttonmatrix_button_area()` translates and returns
the absolute rect. There is no public getter, so this includes
`lv_buttonmatrix_private.h` — an established pattern here, used by five other
source files. No submodule patch needed.

Two details this exposed: the map array interleaves `"\n"` row separators that
`button_areas[]` does not count, so the draw loop now tracks a separate button
index; and placement itself moved into a static, widget-free
`KeyboardManager::compute_hint_area()` so the containment invariant is testable.
That function guarantees its output rect lies inside the key, falling back from a
proportional inset to 1px and refusing outright when a glyph cannot fit — better a
missing hint than one drawn over the next key.

### 2b. Slide-to-select was unreachable

`auto_insert_alt_` defaults to `true`, so `LV_EVENT_LONG_PRESSED` inserted the
alternate immediately and set `LP_ALT_SELECTED`. On release the handler took the
"already inserted, just clean up" branch, and the entire slide-to-select block —
which only runs while the state is still `LP_LONG_DETECTED` — was dead code. The
popover was decorative: the character was committed before the user could aim at
it, and sliding away to cancel still left it in the field.

**Fix:** insertion moves to release time.

| Release lands | Result |
|---|---|
| On a popover character | That character |
| Still on the original key | The alternate (`auto_insert_alt_`), else the primary |
| Well away from both | Nothing — cancelled |

Releasing in place yielding the *alternate* is deliberate. A long-press is an
explicit request for it, and this preserves the existing one-gesture path (hold
`f`, get `_`) that the whole feature request is about. Falling back to the primary
there would have been a regression dressed up as correctness.

The "released back on the original key" hit test previously used a hardcoded
`press_point_ ± 25px` box — wrong in both directions across breakpoints. It now
uses the real key rect, which also corrects the popover's anchor.

**Ordering constraint that must not be broken.** `LV_EVENT_VALUE_CHANGED` is
emitted from *inside* the button matrix's own `LV_EVENT_RELEASED` handler
(`lv_buttonmatrix.c:436`) as a nested dispatch, and `keyboard_event_cb` suppresses
it while the long-press state is non-idle. Our release handler must therefore keep
resetting the state to `LP_IDLE` only at the very end, or a hold-and-release-in-place
would insert both the alternate and the primary. Separately, `LV_EVENT_PRESSING`
(`lv_buttonmatrix.c:402-414`) sets `btn_id_sel` to `NONE` as soon as the finger
leaves the key, which is why sliding away already suppresses the primary character.

A null `lv_indev_active()` at release now falls back to inserting the in-place
character. Under the old auto-insert the keystroke was already committed by that
point; deferring to release would otherwise have silently dropped it.

### 2c. Hint sizing is proportional to the key, not a fixed token

A fixed font token is wrong at both ends of the breakpoint ladder. Keys run from
roughly 48x34 at micro to 128x96 at xlarge, so `font_xs` is illegible on a large
panel while `font_small` crowds the key's own centred letter on a small one — both
were observed on real captures.

`pick_hint_font()` instead walks the theme's font ladder (`font_xs`, `font_small`,
`font_body`, `font_heading`) and takes the largest whose height is within
`HINT_MAX_KEY_FRACTION` (0.32) of the key height, also requiring the glyph to leave
the key's own letter at least two thirds of the width. When nothing satisfies the
fraction — which happens at micro, where even `font_xs` exceeds a third of a 34px
row — it falls back to the smallest font that physically fits rather than returning
nothing. An early version returned nullptr there and silently erased every hint on
the keyboard, which is strictly worse: an invisible hint teaches nothing.

The inset is likewise derived from the **key**, not the glyph (`btn_h / 20`, floor
1). Deriving it from the glyph gave every size roughly the same relative gap, which
is backwards — small keys are the ones short of room and need the hint pushed
hardest into the corner.

### 2d. Punctuation alternates and session MRU

`,` and `.` previously had no alternates at all, since `find_alternatives()` only
matched `a`–`z`. They are now the only keys carrying a **multi-character** set:

| Key | Characters |
|-----|------------|
| `,` | `=` `<` `>` |
| `.` | `/` `[` `]` |

All six are otherwise unreachable from the alpha layer. This also resolves the
earlier `=` problem without displacing anything: `=` needed a slot, and these keys
were empty.

Selecting a character off the popover promotes it to the front of that key's list
(`promote_alternative()`), so the next plain long-press yields it. Because the
corner hint draws element 0, it updates to the new default for free — the hint is
already a live indicator. Reordering is session-only, held in `alt_order_`, seeded
lazily from the shipped table; nothing is persisted.

**Lifetime hazard:** `find_alternatives()` returns a `c_str()` into `alt_order_`.
`std::unordered_map` keeps element addresses stable across rehash, but promoting
rewrites the string in place and invalidates that pointer. `alternatives_` is
therefore cleared before `promote_alternative()` is called, and the promotion is
sequenced after every read of it.

The key's own tap character is deliberately **not** changed by promotion. Making the
key face mutable would need a mutable button map and would make plain typing
non-deterministic — tap `,` and get `=` because of an earlier long-press.

### 2e. Documentation

`docs/user/guide/getting-started.md` § On-Screen Keyboard (lines 139-148) states that
long-press yields alternate characters and gives one example (`a`→`@`). It never lists
the mapping. Add the full table, sourced from `alt_char_map_` in
`ui_keyboard_manager.cpp:39-96`:

- Top row `q`–`p` → digits `1`–`0`
- Home row `a s d f g h j k l` → `@ # $ _ & - + ( )`
- Bottom row `z x c v b n m` → `* " ' : ; ! ?`

Call out `f`→`_`, `h`→`-`, and `j`→`+` explicitly in prose as the ones that matter for
G-code and macro names, since that is the reported pain point.

The gesture table at line 41 already says long press accesses alternate characters and
needs no change.

**Docs must stay in sync with `alt_char_map_`.** If that table changes, this doc changes
with it.

### 2d. Testing

`tests/unit/test_keyboard_hint_placement.cpp` covers `compute_hint_area()` directly.
The load-bearing case is a sweep over key widths from a cramped micro-breakpoint key
to a full-width spacebar, key heights across every shipped row height, and glyph
sizes spanning `font_xs` at every DPI, asserting that each call either refuses or
returns a rect fully inside the key — there is no third outcome. Keys are placed at
a non-zero origin so an anchoring bug that happens to work at (0,0) still fails. The
sweep asserts both branches were exercised, so it cannot pass vacuously.

Mutation-verified: deleting the too-small guard fails both the explicit small-key
case and the sweep, which reports the offending `key 4x6 glyph 5x6`.

The **gesture** behaviour in 2b is verified through the synthetic pointer added in
Part 3, driving LVGL's real input pipeline at 800x480:

| Gesture | Result | Handler branch taken |
|---|---|---|
| Long-press `f`, lift in place | `_` | `Release on key - inserted '_'` |
| Long-press `f`, slide onto the popover, lift | `_` | `Release in overlay area: true` → `Selected nearest label '_' (dist=1)` |
| Long-press `f`, slide far away, lift | *(nothing)* | `Released outside - cancelled` |
| Short tap `f` | `f` | none — normal `VALUE_CHANGED` path |

The short-tap case is the regression guard. Moving insertion to release time could
plausibly have broken ordinary typing; it logs no long-press activity at all, which
confirms plain keystrokes still take the untouched path.

Hint containment was also confirmed visually at 800x480 — every alternate glyph sits
inside its key's top-right corner across all four rows, including the wide shift and
backspace keys the old width table got wrong.

**Gotcha for anyone scripting this:** read the keyboard's geometry only *after* the
slide-in animation settles (`KEYBOARD_SLIDE_DURATION_MS`, 200ms). Query it
immediately after `focus` and you get the parked off-screen position (`y=480` on a
480-tall display), and every derived coordinate misses. The keyboard is named
`on_screen_keyboard` so `ctl geom` can find it.

## Part 3 — `ctl` gains focus and a synthetic pointer

Verifying Part 2 exposed a gap in the tooling. `ctl click` is
`lv_obj_send_event(obj, LV_EVENT_CLICKED)` (`remote_control_server.cpp:341`) and
`ctl scroll` is `lv_obj_scroll_by()` — both widget-level, neither involving an input
device. So `ctl` could not raise the on-screen keyboard (no `LV_EVENT_FOCUSED`) and
could not exercise any gesture at all.

The trap worth recording: driving the keyboard through `click` would not merely fail
to test slide-to-select, it would appear to pass. With no `lv_indev_active()`, the
release handler falls into the null-indev fallback and inserts the in-place
character. Green result, nothing tested.

Two additions, both only reachable via the control server:

- **`focus <target>`** — focuses through the widget's input group, so the real
  `LV_EVENT_FOCUSED` fires and `KeyboardManager` raises the keyboard for a
  registered textarea. This is what makes hint placement visually checkable at
  several resolutions.
- **`press` / `move` / `release`** — a second LVGL pointer indev (`remote_pointer.h`)
  whose state `ctl` sets explicitly. Because it goes through LVGL's real input
  pipeline, long-press timing, `PRESSING` slide detection and scroll-versus-click
  arbitration all behave as under a finger. Each command returns only after the
  device has been sampled twice, so sequences do not race the indev timer; gesture
  timing is controlled by the caller (`sleep` between press and release).

Scope beyond this task: nothing gesture-driven was previously testable without a
human — jog-pad drags, home-widget page swipes, scroll-versus-tap on the console
list added in Part 1, the HDMI5 phantom-edge-touch repro, touch calibration. This is
the primitive all of those need.

## Deferred / not in this spec

- **Adding `=` to the long-press map.** All 26 letters are already mapped, and in
  auto-insert mode only `alternatives[0]` is ever reachable, so a second character on an
  existing key would be dead weight. Adding `=` means displacing an existing mapping —
  `l`→`)` is the weakest candidate for a G-code context. This keyboard is global, not
  console-specific, so the change costs `)` everywhere in the app. Needs an explicit
  yes/no before implementing; not assumed.
- **Bumping the hint glyph size or contrast.** Placement and containment are now
  correct, but the glyph is still `font_xs` at `LV_OPA_60`. Whether that is legible
  enough on a real 5" panel is unverified — judge it on hardware before changing it,
  since the documentation fix may be sufficient on its own.
- **A console-specific g-code keyboard layout** (visible digit row, uppercase default,
  dedicated symbol keys) via a new `KeyboardHint::GCODE`. The hint mechanism exists
  (`NUMERIC` already ships), so this is feasible, but it is a much larger change than
  the reported problem justifies, and a five-row layout has real height risk on 480x320
  panels. Recorded here so the option is not rediscovered from scratch.
- **A touch affordance for the readline ring itself** (a history-step button, or a
  history popup list). Tap-to-paste covers the reported use case and reaches further
  back. Revisit only if users ask to recall a command that has scrolled out of the log.

## Answer to the reporters

The long-press gesture already does what Sunny wants and needs no release to start
using: hold `f` for `_`, `h` for `-`, `j` for `+`, and hold any top-row letter for its
digit. Worth saying in the thread rather than waiting on this work.
