# M117 Pre-Print Visibility Implementation Plan

> ⚠️ **Historical record (verified 2026-08-09) - not instructions.**
> **Status: SHIPPED.** The 54 `- [ ]` boxes were never ticked and do not mean the work is
> outstanding. Coverage landed in `tests/unit/test_display_message.cpp`, not the
> `test_print_status_widget_m117.cpp` this plan names.
>
> Code line numbers and file paths below may have drifted. Follow the **symbol**, not
> the number, and verify every predicate against current code before relying on
> anything here.

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make Klipper M117 messages (`display_status.message`) visible during pre-print (heating / QGL / purge) and on idle screens, and stop HelixScreen from destroying them.

**Architecture:** Give each subject exactly one writer. `print_start_message` becomes purely HelixScreen's derived phase label; `display_message` becomes purely the printer's M117, written only at the parse site and cleared only at print end. Two imperative UI paths are replaced with declarative subject bindings.

**Tech Stack:** C++17, LVGL 9.5, helix-xml, Catch2, nlohmann/json (via `hv/json.hpp`).

**Spec:** `docs/devel/specs/2026-07-19-m117-preprint-visibility-design.md` (commits `d0f8b101b`, `383d4704d`)

**Worktree:** `.worktrees/m117-preprint-visibility` on branch `feature/m117-preprint-visibility`, already created and building clean.

## Global Constraints

- **Work in the worktree only.** `cd /home/pbrown/Code/Printing/helixscreen/.worktrees/m117-preprint-visibility`. The shared tree has uncommitted AMS/filament work that must not be touched.
- **NEVER `git add -A` or `git add .`** — `lib/` is symlinked from the main tree in worktrees; a blanket add clobbers real `lib/` files on merge. Always stage explicit paths.
- **XML changes need no rebuild** — `ui_xml/*.xml` loads at runtime. Relaunch the binary; do not `make` for XML-only edits [L031].
- **Declarative over imperative** — no `lv_obj_add_flag(obj, HIDDEN)`, no `lv_label_set_text`. Use subject bindings (CLAUDE.md Rules 2, 3).
- **One `bind_flag_*` per flag per object.** Multiple `bind_flag_if_eq` on the same object are independent observers; last write wins [L042].
- **Word-form operators in XML conditions** (`eq ne lt le gt ge and or not`) — `&&`/`<` need escaping.
- **Do NOT edit `android/app/src/main/assets/ui_xml/`.** It is a generated build artifact, not source: `android/.gitignore:5` ignores the whole `app/src/main/assets/` tree, zero files there are tracked, and `android/app/build.gradle:86` copies from `../../ui_xml` at build time. Editing `ui_xml/` is sufficient and authoritative; the Android copy regenerates on the next build. (Earlier revisions of this plan wrongly instructed mirroring edits there — ignore any such instruction remaining in a task body.)
- **No `lv_tr()` on M117 text** — it is user data, not a translatable string [L070].
- Build command: `make -j`. Test build: `make test-build`. Check for concurrent builds first with `pgrep -xc cc1plus` [L092].

---

### Task 1: Decouple M117 from the phase-label subject

Stops `PrintStartCollector` writing user M117 text into `print_start_message`, so phase labels and user text stop clobbering each other.

**Files:**
- Modify: `src/print/print_start_collector.cpp:243-253`
- Test: `tests/unit/test_print_start_collector.cpp`

**Interfaces:**
- Consumes: nothing from earlier tasks.
- Produces: `print_start_message` now contains only collector-generated phase labels. Task 5 relies on this.

**Why the test lives in `test_print_start_collector.cpp`:** the pass-through is reached only via the `notify_status_update` callback that `PrintStartCollector::start()` registers with `MoonrakerClient` (`print_start_collector.cpp:199`). `test_display_message.cpp` never constructs a client or a collector, so a test there cannot reach this code and would pass identically before and after the change. `MoonrakerClient::dispatch_status_update()` (`include/moonraker_client.h:654`, public, inherited by `MoonrakerClientMock`) wraps a bare status object into the real envelope and fires that exact callback map — it is the correct seam.

- [ ] **Step 1: Write the failing test**

Add to `tests/unit/test_print_start_collector.cpp` as a `TEST_CASE_METHOD` on the existing `PrintStartCollectorHeaterFixture` (defined `:603-716`). Match the tag string used by the surrounding `TEST_CASE_METHOD`s in that file:

```cpp
TEST_CASE_METHOD(PrintStartCollectorHeaterFixture,
                 "PrintStartCollector: M117 does not overwrite the phase label",
                 /* match surrounding tags in this file */) {
    // Drive to a known phase that has its own label. Mirrors the existing
    // proactive bed-heating section at :955-964.
    collector().start();
    drain_async_updates();
    drain_async_updates(); // INITIALIZING settle

    set_all_temps(150, 600, 0, 0);
    collector().check_fallback_completion();
    drain_async_updates();
    drain_async_updates();
    REQUIRE(get_current_phase() == PrintStartPhase::HEATING_BED);
    REQUIRE(get_current_message() == "Heating Bed...");

    // A user M117 arrives on the same notify_status_update path the collector
    // listens on. It must NOT clobber the collector's phase label — user text
    // belongs in display_message, which PrinterPrintState owns.
    client().dispatch_status_update({{"display_status", {{"message", "Leveling 3/9"}}}});
    drain_async_updates();
    drain_async_updates();

    REQUIRE(get_current_message() == "Heating Bed...");
}
```

Fixture helpers used, all pre-existing: `client()`, `collector()`, `get_current_phase()`, `get_current_message()` (`:643-645`), `drain_async_updates()` (`:688-690`), `set_all_temps()`.

- [ ] **Step 2: Run test to verify it fails**

```bash
make test-build && ./build/bin/helix-tests "[print_start]"
```

(Use whichever tag the file's tests actually carry — confirm with `./build/bin/helix-tests --list-tests | grep -i print.start`.)

Expected: FAIL on the final `REQUIRE` — `get_current_message()` reads `"Leveling 3/9"` because the collector's M117 pass-through overwrote the phase label. **If it passes immediately, stop and report** — that means the pass-through is not reachable the way the plan assumes.

- [ ] **Step 3: Remove the pass-through**

In `src/print/print_start_collector.cpp`, delete the block at `:243-253` that calls `update_message_only(msg)` in response to `display_status.message`. Leave every phase-label write (`:263`, `:343`, `:502`, `:510`, `:1055`, `:1060`, `:1064`, `:1080`, `:1109`, and the BED_MESH labels at `:811-820`, `:866-874`, `:944-958`) intact.

If `update_message_only()` (definition at `:1279`) has no remaining callers after this, delete it and its declaration in `include/print_start_collector.h`. Verify with:

```bash
grep -rn "update_message_only" src/ include/ tests/
```

- [ ] **Step 4: Run test to verify it passes**

```bash
make test-build && ./build/bin/helix-tests "[print_start]"
```

Expected: PASS — the new test, and every pre-existing phase-label assertion in the file (`:964` `"Heating Bed..."`, `:1056` `"Heating Nozzle..."`, `:2048` `"Loading Filament"`, `:2451` `"Bed mesh"`, `:2480` `"Priming"`, and the gcode-response-driven ones at `:726, 766, 793, 837`). Those are the regression net for this deletion — none of them drive `display_status`, so they must all stay green.

- [ ] **Step 5: Commit**

```bash
git add src/print/print_start_collector.cpp include/print_start_collector.h tests/unit/test_print_start_collector.cpp
git commit -m "fix(print): stop routing M117 into the phase-label subject"
```

---

### Task 2: Show M117 during pre-print

Removes the `is_preparing` suppression so M117 displays through heating, QGL, and purge.

**Files:**
- Modify: `src/printer/printer_print_state.cpp:831-843`
- Modify: `ui_xml/print_status_panel.xml:261-266` (stale comment)
- Test: `tests/unit/test_display_message.cpp`

**Interfaces:**
- Consumes: Task 1's decoupling (without it, this would double-render the same text).
- Produces: `display_message_visible == 1` whenever `display_message` is non-empty, regardless of phase.

- [ ] **Step 1: Rewrite the existing test that encodes the old behavior**

`tests/unit/test_display_message.cpp` currently contains a section asserting the opposite of what we now want. Replace the section titled `"visible=0 during print preparation even with message present"` in its entirety with:

```cpp
    SECTION("visible=1 during print preparation (M117 shows through heating/QGL/purge)") {
        // Pre-print is where macro authors put the most M117 traffic. The
        // phase label lives in print_start_message; this row is the user's text.
        state.set_print_start_state(PrintStartPhase::HEATING_BED, "Heating Bed...", 30);
        helix::ui::UpdateQueueTestAccess::drain(helix::ui::UpdateQueue::instance());

        json status = {{"display_status", {{"message", "Heating..."}}}};
        state.update_from_status(status);
        REQUIRE(std::string(lv_subject_get_string(state.get_display_message_subject())) ==
                "Heating...");
        REQUIRE(lv_subject_get_int(state.get_display_message_visible_subject()) == 1);

        // Still visible at COMPLETE, which is itself a non-IDLE phase and
        // previously suppressed the row for an entire print.
        state.set_print_start_state(PrintStartPhase::COMPLETE, "Done", 100);
        helix::ui::UpdateQueueTestAccess::drain(helix::ui::UpdateQueue::instance());
        REQUIRE(lv_subject_get_int(state.get_display_message_visible_subject()) == 1);

        // And after returning to IDLE.
        state.reset_print_start_state();
        helix::ui::UpdateQueueTestAccess::drain(helix::ui::UpdateQueue::instance());
        REQUIRE(lv_subject_get_int(state.get_display_message_visible_subject()) == 1);
    }
```

- [ ] **Step 2: Run test to verify it fails**

```bash
make test-build && ./build/bin/helix-tests "[display_message]"
```

Expected: FAIL — `display_message_visible` is 0 during `HEATING_BED` because of the `is_preparing` gate.

- [ ] **Step 3: Delete the gate**

Replace `PrinterPrintState::update_display_message_visible()` in `src/printer/printer_print_state.cpp:831-843` with:

```cpp
void PrinterPrintState::update_display_message_visible() {
    // The row mirrors display_status.message and is shown whenever there is
    // text. Pre-print is deliberately included: PRINT_START macros are where
    // most M117 traffic originates. The collector's phase label lives in a
    // separate subject (print_start_message), so there is nothing to duplicate.
    int new_value = (strcmp(lv_subject_get_string(&display_message_), "") != 0) ? 1 : 0;
    if (lv_subject_get_int(&display_message_visible_) != new_value) {
        lv_subject_set_int(&display_message_visible_, new_value);
    }
}
```

If `print_start_phase_` is now unused inside this translation unit's visibility path, leave the member alone — it is used elsewhere.

- [ ] **Step 4: Fix the now-false comment in XML**

`ui_xml/print_status_panel.xml:261-266` claims the row stays hidden during pre-print. Replace that comment block with:

```xml
            <!-- Klipper display_status.message (M117) — collapses to zero height -->
            <!-- when empty. Shown during pre-print too: the collector's phase -->
            <!-- label is a separate subject (print_start_message) rendered by -->
            <!-- preparing_overlay above, so the two do not duplicate. -->
```

- [ ] **Step 5: Run test to verify it passes**

```bash
make test-build && ./build/bin/helix-tests "[display_message]"
```

Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add src/printer/printer_print_state.cpp ui_xml/print_status_panel.xml tests/unit/test_display_message.cpp
git commit -m "fix(print): show M117 during pre-print heating/QGL/purge"
```

---

### Task 3: Fix M117 clearing (the delta-clobber bug)

Removes both HelixScreen-side clears and replaces them with a single print-end clear. This is the fix for the originally reported symptom.

**Files:**
- Modify: `src/printer/printer_print_state.cpp` — remove `:294`, remove `:190`, add print-end branch in the state-transition block (~`:283-296`)
- Test: `tests/unit/test_display_message.cpp`

**Interfaces:**
- Consumes: nothing from earlier tasks.
- Produces: `display_message` has exactly two writers — the parse site (`:534`) and the print-end clear. Task 4's idle rows depend on the print-end clear to avoid showing stale text.

**Background the implementer needs:** Moonraker's `notify_status_update` carries *deltas*. `display_status.message` is re-sent only when its value changes. Clearing the subject on an observed state transition destroys any M117 that arrived in an earlier notification, and Klipper will never re-send it. That is the bug. Print *end* is safe because no PRINT_START M117 traffic can be in flight then.

- [ ] **Step 1: Write the failing tests**

Replace the existing `TEST_CASE("Display message: resets on new print", ...)` in `tests/unit/test_display_message.cpp` — it encodes the old clear-at-start behavior — with these three cases:

```cpp
// ============================================================================
// Clearing Semantics
// ============================================================================

TEST_CASE("Display message: survives the transition into PRINTING",
          "[print][display_message]") {
    lv_init_safe();
    PrinterState& state = get_printer_state();
    PrinterStateTestAccess::reset(state);
    state.init_subjects(false);

    // Regression for the delta-clobber bug. A PRINT_START macro emits M117
    // before print_stats.state flips to "printing", so the message arrives in
    // an EARLIER notification. Moonraker sends deltas, so Klipper will never
    // re-send this value. If the PRINTING transition clears it, it is gone for
    // the whole print — which is exactly what users reported.
    json m117 = {{"display_status", {{"message", "Heating bed to 60"}}}};
    state.update_from_status(m117);
    REQUIRE(std::string(lv_subject_get_string(state.get_display_message_subject())) ==
            "Heating bed to 60");

    // Separate notification, no display_status key at all (a true delta).
    json printing = {{"print_stats", {{"state", "printing"}}}};
    state.update_from_status(printing);

    REQUIRE(std::string(lv_subject_get_string(state.get_display_message_subject())) ==
            "Heating bed to 60");
    REQUIRE(lv_subject_get_int(state.get_display_message_visible_subject()) == 1);
}

TEST_CASE("Display message: cleared at print end", "[print][display_message]") {
    lv_init_safe();

    auto run_end_state = [](const char* end_state) {
        PrinterState& state = get_printer_state();
        PrinterStateTestAccess::reset(state);
        state.init_subjects(false);

        json printing = {{"print_stats", {{"state", "printing"}}}};
        state.update_from_status(printing);

        json msg = {{"display_status", {{"message", "Layer 47/120"}}}};
        state.update_from_status(msg);
        REQUIRE(std::string(lv_subject_get_string(state.get_display_message_subject())) ==
                "Layer 47/120");

        json done = {{"print_stats", {{"state", end_state}}}};
        state.update_from_status(done);

        REQUIRE(std::string(lv_subject_get_string(state.get_display_message_subject())) == "");
        REQUIRE(lv_subject_get_int(state.get_display_message_visible_subject()) == 0);
    };

    SECTION("complete") { run_end_state("complete"); }
    SECTION("cancelled") { run_end_state("cancelled"); }
    SECTION("error") { run_end_state("error"); }
}

TEST_CASE("Display message: END_PRINT M117 survives the print-end clear",
          "[print][display_message]") {
    lv_init_safe();
    PrinterState& state = get_printer_state();
    PrinterStateTestAccess::reset(state);
    state.init_subjects(false);

    json printing = {{"print_stats", {{"state", "printing"}}}};
    state.update_from_status(printing);

    // Print ends first...
    json done = {{"print_stats", {{"state", "complete"}}}};
    state.update_from_status(done);
    REQUIRE(std::string(lv_subject_get_string(state.get_display_message_subject())) == "");

    // ...then an END_PRINT macro's M117 lands in a later notification.
    // It must display, not be swallowed.
    json farewell = {{"display_status", {{"message", "Print complete - remove part"}}}};
    state.update_from_status(farewell);

    REQUIRE(std::string(lv_subject_get_string(state.get_display_message_subject())) ==
            "Print complete - remove part");
    REQUIRE(lv_subject_get_int(state.get_display_message_visible_subject()) == 1);
}
```

- [ ] **Step 2: Run tests to verify they fail**

```bash
make test-build && ./build/bin/helix-tests "[display_message]"
```

Expected: `survives the transition into PRINTING` FAILS (message is `""` — the clobber). `cleared at print end` FAILS (message still reads `"Layer 47/120"` — no print-end clear exists yet).

- [ ] **Step 3: Remove the two clears**

In `src/printer/printer_print_state.cpp`:

1. In the state-transition block (~`:283-296`), delete the `lv_subject_copy_string(&display_message_, "");` line and its paired `update_display_message_visible();` from the `new_state == PrintJobState::PRINTING && current_state != PrintJobState::PAUSED` branch. Leave everything else in that branch intact.
2. In `reset_for_new_print()` (~`:190`), delete the `lv_subject_copy_string(&display_message_, "");` line and its paired `update_display_message_visible();`.

Confirm no other writer remains:

```bash
grep -n "display_message_" src/printer/printer_print_state.cpp
```

Expected remaining writes: the parse site (~`:534-556`) and the new print-end clear from Step 4.

- [ ] **Step 4: Add the print-end clear**

**Placement matters — read this before writing code.** The existing chain in `src/printer/printer_print_state.cpp` (~`:268-294`) is an outcome-setting `if / else if` chain whose FIRST THREE arms already match COMPLETE, CANCELLED, and ERROR:

```cpp
if (new_state != current_state) {
    if      (new_state == PrintJobState::COMPLETE)  { ...set outcome... }
    else if (new_state == PrintJobState::CANCELLED) { ...set outcome... }
    else if (new_state == PrintJobState::ERROR)     { ...set outcome... }
    else if (new_state == PrintJobState::PRINTING && current_state != PrintJobState::PAUSED) { ... }
}
```

So an `else if` appended after the `PRINTING` arm would be **unreachable dead code**. Do not add one. Do not restructure the outcome chain either — it works and is not this task's business.

Instead add a **separate `if`** immediately after that chain closes, still INSIDE the enclosing `if (new_state != current_state)` block:

```cpp
                // Clear M117 at print END, never at print start. Clearing on the
                // observed transition INTO printing destroys PRINT_START-era
                // messages permanently: Moonraker sends deltas, so Klipper never
                // re-sends an unchanged value. At print end no such traffic is in
                // flight, so this is race-free. An END_PRINT macro's M117 arrives
                // in a later notification and displays normally.
                //
                // Must stay inside the `new_state != current_state` guard so this
                // is EDGE-triggered. Level-triggered would re-clear on every
                // notification while COMPLETE and would swallow the END_PRINT M117.
                if (new_state == PrintJobState::COMPLETE ||
                    new_state == PrintJobState::CANCELLED ||
                    new_state == PrintJobState::ERROR) {
                    lv_subject_copy_string(&display_message_, "");
                    update_display_message_visible();
                }
```

A single block rather than one clear appended to each of the three terminal arms — three copies of the same two lines would be verbatim duplication.

- [ ] **Step 5: Run tests to verify they pass**

```bash
make test-build && ./build/bin/helix-tests "[display_message]"
```

Expected: PASS, all cases.

- [ ] **Step 6: Run the wider print suite for regressions**

```bash
./build/bin/helix-tests "[print]~[slow]"
./build/bin/helix-tests "[state]"
```

Expected: PASS. If `test_printer_state.cpp` or `test_printer_state_pause.cpp` fail, they likely assert the old clear-at-start behavior — read the assertion and update it to match the spec's clearing policy, or report back if it looks like a genuine regression rather than an encoded assumption.

- [ ] **Step 7: Commit**

```bash
git add src/printer/printer_print_state.cpp tests/unit/test_display_message.cpp
git commit -m "fix(print): clear M117 at print end, not print start (delta clobber)"
```

---

### Task 4: Add M117 surfaces to idle views

Gives M117 a home on the library idle card and fixes the dead/clipped copy in the detailed active view.

**Files:**
- Modify: `ui_xml/components/panel_widget_print_status.xml` — add row to `print_card_idle` (~`:16-86`), gate the row at `:182-186` to view 3
- Test: manual runtime verification (Task 7)

**Interfaces:**
- Consumes: Task 3's print-end clear — without it these idle rows would display stale text from the previous print indefinitely.
- Produces: M117 visible in views 0, 2, 3, 4.

**Note:** XML loads at runtime. No rebuild needed for this task [L031].

- [ ] **Step 1: Add the M117 row to `print_card_idle`**

In `ui_xml/components/panel_widget_print_status.xml`, inside the `print_card_idle` container (opens ~`:16`), add this as the **last child**, immediately before the closing `</lv_obj>` of `print_card_idle` (after `library_body` closes):

```xml
      <!-- Klipper display_status.message (M117). Collapses to zero height when
           empty. Cleared at print end, so this never shows the previous job's
           text. clickable/event_bubble keep taps reaching the card handler. -->
      <text_small name="idle_display_message"
                  width="100%" bind_text="display_message" style_text_align="center"
                  long_mode="dots" style_text_color="#text_muted" hidden="true"
                  clickable="false" event_bubble="true">
        <bind_flag_if_eq subject="display_message_visible" flag="hidden" ref_value="0"/>
      </text_small>
```

The `name` must be unique within the component — `display_message` and `detailed_idle_display_message` are already taken.

- [ ] **Step 2: Gate the existing row at `:182-186` to view 3**

That `text_small name="display_message"` is a child of `print_card_printing` and is clipped in view 4 (`print_status_detailed_active` is `height="100%"` and consumes the flex content box), while `print_status_detailed_active.xml:131` already renders its own copy. Scope it explicitly to the one view where it works.

Replace the block at `:182-186` with:

```xml
      <!-- Library active view only (print_status_view == 3). The detailed
           active view has its own M117 row inside print_status_detailed_active;
           this copy is clipped there by that component's height="100%". -->
      <text_small name="display_message"
                  width="100%" bind_text="display_message" style_text_align="center"
                  long_mode="dots" hidden="true" clickable="false" event_bubble="true">
        <bind_flag_if cond="print_status_view ne 3 or display_message_visible eq 0" flag="hidden"/>
      </text_small>
```

A single `bind_flag_if` with a compound condition — not two stacked `bind_flag_if_eq` children, which would be independent observers racing on the same flag [L042], and not a C++ derived subject (CLAUDE.md Rule 9).

- [ ] **Step 4: Verify the XML parses**

```bash
make -j && ./build/bin/helix-screen --test -vv -p home 2>&1 | grep -iE "xml|parse|error" | head -40
```

Expected: no XML parse errors, no `unknown-widget`, no unresolved-subject warnings for `display_message` / `print_status_view` / `display_message_visible`.

- [ ] **Step 5: Run the XML lint**

```bash
make lint-xml
```

Expected: PASS. No schema regeneration is needed — no new widget type was registered [L089].

- [ ] **Step 6: Commit**

```bash
git add ui_xml/components/panel_widget_print_status.xml
git commit -m "feat(ui): add M117 row to idle card, scope active-card row to library view"
```

---

### Task 5: Replace the `preparing_operation` observer hop with a direct binding

Deletes a C++ observer that only copies one string subject into another.

**Files:**
- Modify: `ui_xml/print_status_panel.xml:89-91`
- Modify: `src/ui/ui_panel_print_status.cpp` — remove `:206-210`, `:410-411`, `:637-638`, `:2758-2770`
- Modify: `include/ui_panel_print_status.h` — remove `:323`, `:400`, `:641`, `:661`

**Interfaces:**
- Consumes: Task 1 (`print_start_message` now holds only phase labels, which is what this overlay should show).
- Produces: `preparing_operation` subject no longer exists. Nothing else binds it — verify before deleting.

**Why this is safe:** `print_start_message` is registered in the global XML subject scope (`printer_print_state.cpp:80`, `INIT_SUBJECT_STRING(... register_xml)`) and is already bound directly at `panel_widget_print_status.xml:157`. The observer body does nothing but `strncpy` + `lv_subject_copy_string` — no default substitution at runtime, no truncation difference (both buffers are 64 bytes), no translation or formatting.

- [ ] **Step 1: Confirm nothing else binds `preparing_operation`**

```bash
grep -rn "preparing_operation" ui_xml/ src/ include/ tests/
```

Expected: only the sites listed under **Files** above. If any other consumer appears, stop and report it.

- [ ] **Step 2: Point the XML at the upstream subject**

In `ui_xml/print_status_panel.xml`, replace lines 89-91 with:

```xml
          <text_heading name="preparing_operation_label"
                        width="100%" bind_text="print_start_message" text="Preparing..." translation_tag="Preparing..."
                        style_text_color="#text" style_text_align="center"/>
```

Accepted behavior change: the `"Preparing..."` inline default is stamped over by the subject's current value at bind time, so the heading is briefly blank if the overlay renders before the collector emits its first phase message. In practice `preparing_overlay` is gated by `bind_flag_if_eq subject="preparing_visible" ref_value="0"` (`print_status_panel.xml:87`) and `preparing_visible` is only set once a phase is known — so a message has already landed. Confirm visually in Task 7.

- [ ] **Step 3: Delete the C++ observer, subject, and method**

In `src/ui/ui_panel_print_status.cpp`, delete:
- `:206-210` — the `observe_string<PrintStatusPanel>(...)` registration for `print_start_message_observer_`
- `:410-411` — the `UI_MANAGED_SUBJECT_STRING(preparing_operation_subject_, ...)` line
- `:637-638` — the two attach-sync lines that fetch `print_start_message` and call `on_print_start_message_changed(msg)`. **Keep** the surrounding phase and progress sync lines.
- `:2758-2770` — the whole `PrintStatusPanel::on_print_start_message_changed` method

In `include/ui_panel_print_status.h`, delete:
- `:323` — `lv_subject_t preparing_operation_subject_;`
- `:400` — `char preparing_operation_buf_[64] = "Preparing...";`
- `:641` — `void on_print_start_message_changed(const char* message);`
- `:661` — `ObserverGuard print_start_message_observer_;`

- [ ] **Step 4: Build and verify no dangling references**

```bash
make -j 2>&1 | tail -20
grep -rn "on_print_start_message_changed\|preparing_operation" src/ include/
```

Expected: clean build; grep returns nothing.

- [ ] **Step 5: Run the panel tests**

```bash
make test-build && ./build/bin/helix-tests "[print]~[slow]"
```

Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add src/ui/ui_panel_print_status.cpp include/ui_panel_print_status.h ui_xml/print_status_panel.xml
git commit -m "refactor(ui): bind preparing heading directly to print_start_message"
```

---

### Task 6: Replace imperative HIDDEN on `print_card_printing` with a subject binding

Removes a CLAUDE.md Rule 2 violation. A suitable derived subject already exists.

**Files:**
- Modify: `ui_xml/components/panel_widget_print_status.xml:117-120` (add binding)
- Modify: `src/ui/panel_widgets/print_status_widget.cpp:664-698` (remove the imperative block)
- Test: `tests/unit/test_display_message.cpp` (parity test)

**Interfaces:**
- Consumes: nothing from earlier tasks.
- Produces: `print_card_printing` visibility is subject-driven.

**Background:** `print_active` (registered `printer_print_state.cpp:59`, updated `:315-323` via `status_indicates_active_print()`) is 1 exactly when `print_stats.state` is `"printing"` or `"paused"` — semantically identical to the widget's `is_active_ = (PRINTING || PAUSED)`. No `subject_expr` is needed.

- [ ] **Step 1: Write the parity test**

`print_active` is derived from the JSON status path while `is_active_` came from the `PrintJobState` enum observer. They should agree, but only `print_active` requires a `print_stats` block in the delta. Prove they track before deleting the C++ path.

Append to `tests/unit/test_display_message.cpp`:

```cpp
// ============================================================================
// print_active parity (guards the declarative visibility binding)
// ============================================================================

TEST_CASE("print_active tracks PrintJobState across a full job lifecycle",
          "[print][display_message]") {
    lv_init_safe();
    PrinterState& state = get_printer_state();
    PrinterStateTestAccess::reset(state);
    state.init_subjects(false);

    struct Step {
        const char* moonraker_state;
        int expected_active;
    };
    const Step steps[] = {
        {"standby", 0}, {"printing", 1}, {"paused", 1},  {"printing", 1},
        {"complete", 0}, {"standby", 0}, {"cancelled", 0}, {"error", 0},
    };

    for (const auto& s : steps) {
        json status = {{"print_stats", {{"state", s.moonraker_state}}}};
        state.update_from_status(status);
        helix::ui::UpdateQueueTestAccess::drain(helix::ui::UpdateQueue::instance());

        INFO("moonraker state: " << s.moonraker_state);
        REQUIRE(lv_subject_get_int(state.get_print_active_subject()) == s.expected_active);
    }
}
```

If the accessor is not named `get_print_active_subject()`, find the real name:

```bash
grep -n "print_active" include/printer_print_state.h include/printer_state.h
```

- [ ] **Step 2: Run the test**

```bash
make test-build && ./build/bin/helix-tests "[display_message]"
```

Expected: PASS (this documents existing behavior). If it FAILS, `print_active` does not track the enum faithfully — stop and report; the declarative swap is unsafe without it.

- [ ] **Step 3: Add the declarative binding**

In `ui_xml/components/panel_widget_print_status.xml`, the `print_card_printing` element (~`:117-120`) already carries `hidden="true"` as its initial state. Add this as its **first child**, before the `home_paused_badge`:

```xml
      <bind_flag_if_eq subject="print_active" flag="hidden" ref_value="0"/>
```

Also update the now-false comment two lines above it — it currently reads `Visibility managed by C++ on_print_state_changed(), not subject binding`. Replace with:

```xml
    <!-- ACTIVE STATE: Shared container for preparing + printing phases -->
    <!-- Visibility bound to print_active (1 when PRINTING or PAUSED). -->
```

- [ ] **Step 4: Remove the imperative block**

In `src/ui/panel_widgets/print_status_widget.cpp`, inside `on_print_state_changed()` (~`:664`), delete the whole `if (print_card_printing_) { ... }` block (~`:679-684`) together with its explanatory comment about the wrapper's padding.

Keep `is_active_ = (...)`, the `update_view_subject()` call, and the `if (is_active_) { ... } else { ... defer_reset_print_card_to_idle(); }` block — `is_active_` still drives those.

If `print_card_printing_` has no remaining uses other than being assigned in `attach()` and nulled in `detach()`, leave the member in place; removing it is out of scope for this task.

- [ ] **Step 5: Build and verify**

```bash
make -j 2>&1 | tail -20
grep -n "print_card_printing_" src/ui/panel_widgets/print_status_widget.cpp
```

Expected: clean build. The grep should show only attach/detach bookkeeping — no `add_flag` / `remove_flag` on it.

- [ ] **Step 6: Run tests**

```bash
make test-build && ./build/bin/helix-tests "[print]~[slow]"
```

Expected: PASS.

- [ ] **Step 7: Commit**

```bash
git add ui_xml/components/panel_widget_print_status.xml src/ui/panel_widgets/print_status_widget.cpp tests/unit/test_display_message.cpp
git commit -m "refactor(ui): bind print_card_printing visibility to print_active subject"
```

---

### Task 7: Runtime verification

Green tests are not sufficient here — the original bug shipped green, and two of the defects were pure-XML visibility problems that no unit test observes.

**RUN THIS LAST.** Tasks 8 and 9 were added after this plan was first written; execute 5, 6, 8, 9, then 7. Task 8 in particular is a prerequisite — without it the mock cannot produce an arbitrary M117 and several steps below are not executable.

**Files:** none modified.

**Interfaces:** Consumes all prior tasks, including 8 and 9.

- [ ] **Step 1: Full test suite**

```bash
make test-run 2>&1 | tail -30
```

Expected: PASS. A SIGSEGV in the full suite that passes in isolation is the known `moonraker_client` #357 stress-test flake — re-run before blaming this diff.

- [ ] **Step 2: Launch with a mock printer**

```bash
./build/bin/helix-screen --test -vv 2>&1 | tee /tmp/m117-verify.log
```

Runs in the foreground so you can interact. Use `run_in_background: true` if driving it from an agent [L060].

- [ ] **Step 3: Verify each surface**

With the app running, confirm by eye and capture a screenshot of each (press `S` in the UI, or `./scripts/screenshot.sh helix-screen m117-view-N`):

1. **Home widget, library idle (view 0)** — M117 row appears when a message is set, collapses when empty.
2. **Home widget, detailed idle (view 2)** — same.
3. **Home widget, library active (view 3)** — M117 row visible during a print, and during pre-print alongside the "Preparing Print" phase label. Confirm they are two distinct lines with different text.
4. **Home widget, detailed active (view 4)** — exactly one M117 row (from `print_status_detailed_active`), not clipped, no duplicate.
5. **Print status panel, mid-preheat** — the `preparing_overlay` heading shows the phase label, the metadata strip below shows M117. Different text, both visible.
6. **Print status panel heading on first render** — confirm it is not blank (the accepted risk from Task 5, Step 2).

**How to set an M117 against the mock:** Task 8 adds `M117` parsing to `MoonrakerClientMock::gcode_script()`. Send it from the in-app console — Settings → Advanced → Console (`AdvancedPanel::handle_console_clicked()`, `src/ui/ui_panel_advanced.cpp:136`) — by typing `M117 hello world`. That routes through `ConsolePanel::send_gcode_command()` → `printer.gcode.script` → the mock, and back out via `dispatch_status_update()`, i.e. the same path real hardware uses.

Before Task 8 existed this was impossible: the mock never parsed `M117` and `display_status.message` was a hardcoded ternary (`moonraker_client_mock.cpp:3954-3966`). If for any reason Task 8 is not in place, the fallback is `HELIX_MOCK_AUTO_PRINT=1`, which drives the mock into PRINTING at low progress and emits the canned `"Purging nozzle"` through the real notification path — enough to prove the row renders, but the text cannot be chosen.

- [ ] **Step 4: Verify the clearing behavior end to end**

1. Start a mock print, send `M117 layer 5` from the console → row shows.
2. Let the print complete → row clears on all surfaces.
3. Send `M117 idle text` while idle → row shows again.
4. Send `M117` with no argument (empty message) → row collapses.

- [ ] **Step 5: Check the log for regressions**

```bash
grep -iE "error|warn|assert|unknown subject|xml" /tmp/m117-verify.log | head -40
```

Expected: no new warnings about `display_message`, `print_start_message`, `print_active`, or `preparing_operation`.

- [ ] **Step 6: Check for conflicts with parallel worktrees**

`feature/1065-material-subject` is touching subject reactivity in adjacent code.

```bash
git fetch origin
git log --oneline main..origin/main -- src/printer/printer_print_state.cpp src/ui/panel_widgets/print_status_widget.cpp ui_xml/components/panel_widget_print_status.xml
```

Report any overlapping commits before merging.

- [ ] **Step 7: Final commit if anything changed**

```bash
git status --short
# stage explicit paths only — never git add -A in a worktree
```

---

### Task 8: Teach the mock printer to handle M117

Without this, M117 cannot be exercised at runtime at all: `MoonrakerClientMock::gcode_script()` has no `M117` branch, so the command is silently swallowed, and `display_status.message` comes from a hardcoded ternary. Task 7's runtime verification depends on this.

**Files:**
- Modify: `src/api/moonraker_client_mock.cpp` — add an `M117` branch to `gcode_script()` (parser starts ~`:1240`); the existing hardcoded `display_status.message` ternary is at ~`:3954-3966`
- Modify: `include/moonraker_client_mock.h` — storage for the current message
- Test: `tests/unit/test_moonraker_mock_behavior.cpp`

**Interfaces:**
- Consumes: nothing from earlier tasks.
- Produces: sending `M117 <text>` through the mock sets `display_status.message` to `<text>`, delivered via the normal status-notification path. Task 7 relies on this.

**Design constraints:**
- The mock must emit the message through the SAME path real hardware uses — the `display_status` object in a status notification — so the production parse site is exercised unchanged. Do not add a side channel.
- `M117` with no argument (or only whitespace) clears the message to `""`. That is Klipper's behavior.
- Klipper strips a single leading space after `M117`; `M117 hello` yields `hello`, not ` hello`.
- The user-set message must take precedence over the existing hardcoded phase strings (`"Heating..."`, `"Purging nozzle"`) rather than being overwritten by the next status tick. Preserve the canned strings only for the case where the user has never sent an `M117`.
- Follow the file's existing patterns for gcode parsing and for how other commands mutate mock state and publish it.

- [ ] **Step 1: Write the failing test**

Add to `tests/unit/test_moonraker_mock_behavior.cpp`, matching the tag style of the surrounding tests in that file:

```cpp
TEST_CASE("MoonrakerClientMock: M117 sets display_status.message", "[mock][display_message]") {
    TestableMoonrakerMock mock;

    json captured;
    mock.register_notify_update([&captured](const json& notif) {
        if (notif.contains("params") && notif["params"].is_array() &&
            !notif["params"].empty() && notif["params"][0].contains("display_status")) {
            captured = notif["params"][0]["display_status"];
        }
    });

    SECTION("sets the message text") {
        mock.gcode_script("M117 Leveling 3/9");
        REQUIRE(captured.contains("message"));
        REQUIRE(captured["message"].get<std::string>() == "Leveling 3/9");
    }

    SECTION("bare M117 clears the message") {
        mock.gcode_script("M117 something");
        captured = json{};
        mock.gcode_script("M117");
        REQUIRE(captured.contains("message"));
        REQUIRE(captured["message"].get<std::string>() == "");
    }

    SECTION("strips exactly one leading space, preserves the rest") {
        mock.gcode_script("M117  two spaces");
        REQUIRE(captured["message"].get<std::string>() == " two spaces");
    }
}
```

`TestableMoonrakerMock` already exists in this file (~`:205`) and re-exports `dispatch_status_update`. If `gcode_script()` is not directly callable in the same way, use whatever entry point the neighbouring mock tests use and say so in your report.

- [ ] **Step 2: Run test to verify it fails**

```bash
make test-build && ./build/bin/helix-tests "[mock][display_message]"
```

Expected: FAIL — no `display_status` notification is emitted at all, because `gcode_script()` has no `M117` branch.

- [ ] **Step 3: Implement**

Add message storage to the mock and an `M117` branch to `gcode_script()` that stores the text and publishes `display_status`. Then change the hardcoded ternary at ~`:3954-3966` so a user-set message wins over the canned phase strings.

Match the file's existing style for command parsing and state publication — read two or three neighbouring command branches (`M104`, `M140`, `PAUSE`) before writing.

- [ ] **Step 4: Run test to verify it passes**

```bash
make test-build && ./build/bin/helix-tests "[mock][display_message]"
```

Expected: PASS.

- [ ] **Step 5: Confirm no regression in existing mock behavior**

```bash
./build/bin/helix-tests "[mock]"
./build/bin/helix-tests "[display_message]"
```

Expected: PASS. The canned `"Heating..."` / `"Purging nozzle"` strings must still appear when no user `M117` has been sent — if an existing test covers them and now fails, that is a real regression, not a stale assumption.

- [ ] **Step 6: Commit**

```bash
git add src/api/moonraker_client_mock.cpp include/moonraker_client_mock.h tests/unit/test_moonraker_mock_behavior.cpp
git commit -m "feat(mock): handle M117 so display_status.message is settable in --test"
```

---

### Task 9: Widget-level tests for M117 row visibility

Task 4 added XML rows verified only by a parse check. Nothing asserts they actually render in the right views — which is precisely the gap that let the clipped view-4 row exist undetected.

**Files:**
- Test: `tests/unit/test_print_status_widget_m117.cpp` (new), or appended to `tests/unit/test_print_status_widget_recycle.cpp` if that fits the file's scope better — your judgment, say which and why.

**Interfaces:**
- Consumes: Task 4's XML rows (`idle_display_message` in `print_card_idle`; the view-3-scoped `display_message` in `print_card_printing`), and Tasks 2/3's subject semantics.
- Produces: regression cover for XML row visibility.

**The pattern already exists — do not invent one.** `tests/unit/test_header_bar_action_hidden.cpp` demonstrates set-subject → assert `LV_OBJ_FLAG_HIDDEN`. `tests/unit/test_print_status_widget_recycle.cpp` demonstrates instantiating this exact component under `LVGLUITestFixture` (needed over `XMLTestFixture` because the component pulls in `ui_card`, `progress_bar`, `icon`, and the nested detailed components). Read both before writing.

Known obstacle, documented in the recycle test: `PrintStatusWidget` registers its own `print_status_view` and `print_status_colspan` subjects in its constructor (`src/ui/panel_widgets/print_status_widget.cpp:91,122`). They must exist BEFORE `lv_xml_create()` parses the component or the bindings silently no-op. The recycle test's setup sequence handles this — follow it.

- [ ] **Step 1: Write the tests**

Cover at minimum:

1. **Idle row tracks the subject.** Instantiate the widget in library layout. Find `idle_display_message`. Assert HIDDEN when the message is empty; drive `state().update_from_status({{"display_status", {{"message", "Heating bed..."}}}})`, drain the UpdateQueue, assert NOT HIDDEN; clear with `nullptr`, drain, assert HIDDEN again.
2. **The view-3 scoping holds.** With a non-empty message, assert the `print_card_printing` child row named `display_message` is HIDDEN when `print_status_view` is not 3, and NOT HIDDEN when it is 3. This is the assertion that would have caught the clipped/dead view-4 copy.

Drive the message subject through `state().update_from_status(...)` — the real production entry point — never by poking the subject directly. Drain the UpdateQueue before every assertion (`UpdateQueueTestAccess::drain(UpdateQueue::instance())`), since observers defer.

Check the recycle test for any required teardown (it calls `PrintStatusWidget::destroy_formatter_for_test()`); if this component needs it, do the same.

- [ ] **Step 2: Prove the tests discriminate**

A visibility test that passes regardless of the binding is worthless — that mistake has already happened twice on this branch.

For the view-3 assertion: temporarily change the `bind_flag_if` condition in `ui_xml/components/panel_widget_print_status.xml` to something always-true (e.g. drop the `print_status_view` clause), re-run, and CONFIRM the test FAILS. Then revert the XML — verify with `git diff ui_xml/` showing no changes — and confirm the test passes again. Report both outputs.

- [ ] **Step 3: Run the suite**

```bash
make test-build && ./build/bin/helix-tests "[print_status]"
./build/bin/helix-tests "[print]~[slow]"
```

Expected: PASS.

- [ ] **Step 4: Commit**

```bash
git add tests/unit/test_print_status_widget_m117.cpp
git commit -m "test(ui): assert M117 row visibility across print status views"
```

---

## Deferred (file as separate issues, do not implement here)

- **Stuck-phase TOCTOU** — `print_start_collector.cpp:302` (`active_.exchange(false)`) races the deferred `set_print_start_state()` (`printer_print_state.cpp:917`), leaving `print_start_phase` parked at `COMPLETE` for a whole print. Removing the `is_preparing` gate (Task 2) means this no longer affects M117, but it still breaks anything else keyed on that phase.
- **Dead `PrinterState::update_from_notification()`** — `printer_state.cpp:284`, called only from `tests/unit/test_printer_state.cpp`. Production routes through the MoonrakerManager queue instead, so envelope-parsing code the tests exercise is not the code that runs.
- **Compact idle M117 surface (view 1)** — excluded by the spec; reachable only via hand-edited `settings.json` and the most space-starved surface.
- **`show_sections` imperative hide** — `print_status_widget.cpp:1256-1258`, needs a new layout-style subject to bind against.
- **Last-print row dimming** — `print_status_widget.cpp:882-885`, should use the existing `print_status_idle_has_last` + `bind_state_if_eq` idiom already used at `print_status_detailed_idle.xml:32`.
