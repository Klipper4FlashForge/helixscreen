# #959 — G-code preview follows AMS tool mapping (loaded slot colors)

**Issue:** prestonbrown/helixscreen#959
**Date:** 2026-07-12
**Status:** design approved, pending spec review

## Problem

The print-select detail view's g-code preview should render extrusion in the
**actual loaded slot colors** (what the printer will extrude), not the
slicer-intended colors baked into the file.

## What already works (do not rebuild)

The core routing shipped in `5a0040b92` (2026-03-10), before the issue was
filed:

- `FilamentMapper::compute_defaults()` resolves every tool it can to a physical
  slot via firmware → color → positional matching. `is_auto` is set true only
  when a tool matches **no** slot at all.
- `FilamentMappingCard::get_mapped_colors()` (`ui_filament_mapping_card.cpp:254`)
  returns, per tool, the resolved slot's loaded `color_rgb`, falling back to the
  tool's slicer color only for unmapped tools.
- `PrintSelectDetailView::apply_mapped_tool_colors()`
  (`ui_print_select_detail_view.cpp:949`) pushes those into both the 2D and 3D
  renderers via `ui_gcode_viewer_set_tool_colors()`, and runs **after**
  `apply_tool_colors()` on load (lines 1404-5, 1505-6), so loaded colors win.
- The in-app remap callback `set_on_mappings_changed` (line 301) re-colors live
  when the user edits the mapping card.

So the default preview already shows loaded slot colors. This work fills the two
remaining gaps.

## Scope (approved)

1. **Live subscription to external AMS changes.**
2. **A "show sliced colors" toggle** (view-local, resets to actual each open),
   as a labeled row in the options column.

Explicitly **out of scope**: any `SettingsManager` / persisted preference. The
toggle is session-local state.

## Design

### 1. Live subscription

`PrintSelectDetailView` currently has **no** AMS observers. Add **one**
`ObserverGuard` member, set up once in `init_subjects()` (runs once per object;
guarded by `subjects_initialized_`), observing the **static singleton**
`AmsState` subject (no `SubjectLifetime` token needed — this is not a per-slot
dynamic subject; identical to the proven pattern at
`ui_panel_print_status.cpp:237`):

- `AmsState::instance().get_slots_version_subject()` — bumped on any slot change
  including loaded `color_rgb` (filament reloaded into a slot). This is the exact
  signal for the issue's scenario.

**Why not `tool_map_version` too:** reacting to a firmware tool→slot remap would
require re-running `compute_defaults()`, and `FilamentMappingCard::update()`
recomputes `mappings_` from scratch — which would **silently wipe a user's manual
remap** on any external change. So the handler must NOT call `update()`. A
firmware-side tool remap is instead picked up on the next file open (which does
call `update()`). Live re-coloring is scoped to slot-color/presence changes,
which is what the issue asks for.

Uses `observe_int_sync<PrintSelectDetailView>(...)` (from `observer_factory.h`),
which defers the callback onto the main thread via `ui_queue_update()` — safe
against the WebSocket background thread that bumps the subject.

**New lightweight card refresh** — add `FilamentMappingCard::refresh_slot_data()`
that re-pulls `available_slots_` from `AmsState::collect_available_slots()` and
re-renders (`rebuild_compact_view()`) **without** recomputing `mappings_`, so
manual and auto tool→slot choices are preserved while loaded colors update.

Handler `on_ams_state_changed()`:

```cpp
if (!is_visible() || !gcode_loaded_ || !gcode_viewer_) return;   // cheap guard
filament_mapping_card_.refresh_slot_data();       // new loaded colors, mappings preserved
refresh_preview_colors_and_mismatch();
```

`refresh_preview_colors_and_mismatch()` is the shared private method factored out
of the existing `set_on_mappings_changed` body (lines 302-313), called from both
the card-edit callback and the AMS observer so the two paths cannot drift:

```cpp
apply_preview_colors();
lv_subject_set_int(&filament_mismatch_, filament_mapping_card_.has_mismatch() ? 1 : 0);
recompute_preflight();
if (lv_subject_get_int(&color_swatches_visible_) == 1)
    update_color_swatches(tools_used_effective(), current_filament_colors_);
```

**Guarding for closed/destroyed view:** the single observer is set up once and
lives for the object's lifetime; it observes a singleton (`AmsState`) that is
always valid. `gcode_viewer_` is nulled on `on_deactivate()`
(`ui_print_select_detail_view.cpp:611`), and the handler no-ops when
`!is_visible()` / `!gcode_loaded_` / `!gcode_viewer_`, so a bump while the view
is closed is harmless. `ObserverGuard` auto-`reset()`s on object destruction
(L085 — `reset()`, never `release()`).

### 1a. Extract `resolve_display_colors` (DRY + testability)

Move the per-tool color-resolution loop out of
`FilamentMappingCard::get_mapped_colors()` into a pure static:

```cpp
std::vector<uint32_t> FilamentMapper::resolve_display_colors(
    const std::vector<GcodeToolInfo>& tools,
    const std::vector<ToolMapping>& mappings,
    const std::vector<AvailableSlot>& slots);
```

For each mapping: loaded slot `color_rgb` when `!is_auto && mapped_slot >= 0` and
a matching slot exists; else the tool's slicer `color_rgb` (or `0x808080` when no
tool info). `get_mapped_colors()` becomes a one-line delegate. This is the unit
under test (see Testing) and guards the already-shipped #959 core.

### 2. Sliced / actual toggle

**State:** a view-local int subject `detail_prefer_sliced_colors_` (0 = actual,
1 = sliced), declared alongside `detail_gcode_viewer_mode_` (~line 129).
**Reset to 0 in `show()`** so every fresh file open starts on actual colors.

**UI:** a `setting_toggle_row` in `ui_xml/print_file_detail.xml`, placed in the
right `options_section` immediately after `filament_mapping_card` (line 198),
bound to the same visibility as the mapping card — the toggle is meaningless
without an AMS mapping to compare against:

```xml
<setting_toggle_row name="sliced_colors_row"
    label="Show sliced colors" label_tag="print.show_sliced_colors"
    subject="detail_prefer_sliced_colors"
    callback="on_toggle_sliced_colors">
    <bind_flag_if_eq subject="filament_mapping_visible" flag="hidden" ref_value="0"/>
</setting_toggle_row>
```

(Exact `setting_toggle_row` prop names + whether the switch writes the subject
itself or the callback must — verified against `setting_toggle_row.xml` /
`ui_switch` during implementation.)

```
options_section (right column)
  ┌─ FILAMENTS card ────────────┐   (legacy swatch fallback, no-AMS)
  ├─ FILAMENT MAPPING card ─────┐   (AMS)
  ├─ [ Show sliced colors  (○) ]┐   ← new row, visible with mapping card
  ├─ PRINT OPTIONS card ────────┐
  └─ [ Delete ] [ Print ] ──────┘
```

**Color dispatch:** introduce `apply_preview_colors()` and replace the two
`apply_tool_colors(); apply_mapped_tool_colors();` call pairs (lines 1404-5,
1505-6) with a single `apply_preview_colors()` call:

```cpp
void PrintSelectDetailView::apply_preview_colors() {
    if (!gcode_viewer_ || !gcode_loaded_) return;
    if (lv_subject_get_int(&detail_prefer_sliced_colors_) == 1) {
        apply_sliced_tool_colors();
    } else {
        apply_tool_colors();          // existing: AMS-firmware or slicer fallback
        apply_mapped_tool_colors();   // existing: loaded/mapped overrides win
    }
}
```

`apply_sliced_tool_colors()` factors out the existing slicer-hex branch already
living in `apply_tool_colors()` (lines 934-946): parse `current_filament_colors_`
→ `std::vector<uint32_t>` → `ui_gcode_viewer_set_tool_colors()` + invalidate.
This forces slicer colors into the same override channel both renderers already
consume, so 2D and 3D behave identically.

**Callback `on_toggle_sliced_colors`:** registered in the panel's
`register_xml_callbacks({...})` block (`ui_panel_print_select.cpp:317`), following
`on_print_select_print_button` (line 142). The `ui_switch` binding is **one-way**
(subject → CHECKED state); the switch does **not** write the subject
(`ui_switch.cpp` only plays a sound on `value_changed`). So the callback reads
`lv_obj_has_state(target, LV_STATE_CHECKED)` and forwards to the detail view,
which writes the subject, re-applies, and toasts. Wiring:
`on_toggle_sliced_colors` (file-static in the panel) → `get_global_print_select_panel().forward_sliced_colors_toggle(checked)` →
`detail_view_->set_prefer_sliced_colors(checked)`.

## Files touched

| File | Change |
|------|--------|
| `include/filament_mapper.h` / `src/printer/filament_mapper.cpp` | new pure `FilamentMapper::resolve_display_colors(tools, mappings, slots)` (§1a) |
| `src/ui/ui_filament_mapping_card.cpp` / `include/ui_filament_mapping_card.h` | `get_mapped_colors()` delegates to the new pure helper; new `refresh_slot_data()` |
| `ui_xml/print_file_detail.xml` | add `setting_toggle_row` (runtime XML, no rebuild) |
| `include/ui_print_select_detail_view.h` | `detail_prefer_sliced_colors_` subject; **one** `ObserverGuard slots_version_observer_`; method decls |
| `src/ui/ui_print_select_detail_view.cpp` | subject init + observer in `init_subjects()`; reset in `show()`; `apply_preview_colors()`, `apply_sliced_tool_colors()`, `refresh_preview_colors_and_mismatch()`, `on_ams_state_changed()`, `set_prefer_sliced_colors()`; swap the two call pairs; mappings-changed body → shared method |
| `include/ui_panel_print_select.h` / `src/ui/ui_panel_print_select.cpp` | register `on_toggle_sliced_colors`; `forward_sliced_colors_toggle(bool)` |
| translations | `SHOW_SLICED_COLORS` label + regenerated artifacts (L064), IF `setting_toggle_row` needs `label_tag`; else plain `label` |
| tests | `tests/unit/test_filament_mapper*.cpp` — see below |

No `SettingsManager` change (view-local state).

## Threading / safety

- `observe_int_sync` on the **static** `AmsState::slots_version` subject →
  main-thread deferred, no `SubjectLifetime` token (singleton, not a per-slot
  dynamic subject — L084 does not apply).
- No sync widget deletion in the handler — it only mutates subjects, refreshes
  card slot data, and re-colors (renderer buffers patched in place). No
  `safe_delete` concerns.
- Handler no-ops when `!is_visible()` / `!gcode_loaded_` / `!gcode_viewer_`.
- `ObserverGuard` cleanup uses `reset()`, never `release()` (L085).

## Testing

- **`resolve_display_colors` unit test** (`tests/unit/test_filament_mapper*.cpp`,
  `[filament]`) — the pure helper from §1a. Cases:
  1. tool with a manual mapping (`is_auto=false`, `mapped_slot>=0`) to a slot →
     returns that slot's `color_rgb` (loaded), NOT the slicer color. (Core #959.)
  2. tool with an auto mapping that has no resolved slot (`mapped_slot<0`) →
     returns the tool's slicer `color_rgb` (fallback).
  3. **live-recolor semantics:** same `mappings`, slot `color_rgb` changed
     between two calls → second call returns the new color (proves a slot-color
     change re-colors without touching mappings).
  4. **preservation:** a manual mapping to slot B is unaffected when an unrelated
     slot A's color changes.
  This test fails if the loaded-color routing or the live-recolor behavior
  regresses. It does not need LVGL/AMS singletons — pure vectors in/out.
- **Device verification (manual):** on K2 Plus (CFS) or AD5X (IFS) — open a
  multi-tool file, confirm preview uses loaded colors; reload a slot with a
  different color and confirm the preview re-colors live **without** resetting a
  manual remap; toggle "Show sliced colors" and confirm it flips to slicer intent
  and back; re-open a file and confirm it resets to actual. The end-to-end UI
  dispatch (toggle + observer wiring) is covered here, not by a fake unit assert.

## Resolved verification items

1. **`setting_toggle_row` props / switch write** — RESOLVED. Props: `label`,
   `label_tag`, `icon`, `description`, `description_tag`, `subject`, `callback`,
   `disabled`. The inner `ui_switch` binding is **one-way**; the callback must
   write the subject (see §2 wiring).
2. **Glyph/font** — N/A; the labeled row needs no new icon.
3. **"View visible" guard** — RESOLVED. `is_visible()` inherited from
   `OverlayBase` (`overlay_base.h:188`); `gcode_viewer_` is nulled on
   `on_deactivate()` (`ui_print_select_detail_view.cpp:611`). Guard on
   `is_visible() && gcode_loaded_ && gcode_viewer_`.

## Remaining impl-time check

- Whether `setting_toggle_row` requires `label_tag` or accepts a plain `label`.
  If a tag is required, add `SHOW_SLICED_COLORS` to the English source YAML and
  regenerate translation artifacts (L064: `src/generated/lv_i18n_translations.{c,h}`,
  `ui_xml/translations/translations.xml`); else use the plain `label`.
