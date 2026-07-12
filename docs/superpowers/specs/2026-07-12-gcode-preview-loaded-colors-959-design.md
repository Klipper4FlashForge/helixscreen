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

`PrintSelectDetailView` currently has **no** AMS observers. Add two
`ObserverGuard` members, wired in `create()` and torn down on destroy, observing
the two **static singleton** `AmsState` subjects (no `SubjectLifetime` token
needed — these are not per-slot dynamic subjects; identical to the proven
pattern at `ui_panel_print_status.cpp:237`):

- `AmsState::instance().get_slots_version_subject()` — bumped on any slot change
  including loaded `color_rgb` (filament reloaded into a slot).
- `AmsState::instance().get_tool_map_version_subject()` — bumped on firmware-side
  tool→slot remap.

Both use `observe_int_sync<PrintSelectDetailView>(...)` (from
`ui_observer_guard.h`), which defers the callback onto the main thread via
`ui_queue_update()` — safe against the WebSocket background thread that bumps the
subjects.

Handler `on_ams_state_changed()`:

```
if (!gcode_loaded_ || overlay not visible) return;   // cheap guard
filament_mapping_card_.update(current_filament_colors_, current_filament_materials_);
apply_preview_colors();
lv_subject_set_int(&filament_mismatch_, filament_mapping_card_.has_mismatch() ? 1 : 0);
if (color_swatches_visible_ == 1)
    update_color_swatches(tools_used_effective(), current_filament_colors_);
```

This is the same body the existing `set_on_mappings_changed` callback already
runs (lines 302-313) — factor it into a shared private method
`refresh_preview_colors_and_mismatch()` and call it from both the card callback
and the two AMS observers, so the three paths cannot drift.

`filament_mapping_card_.update()` re-pulls `collect_available_slots()` from
`AmsState`, so new loaded colors and firmware remaps are picked up before
`apply_preview_colors()` reads `get_mapped_colors()`.

**Guarding for closed/destroyed view:** the observers live only while the widget
tree exists (created in `create()`, `ObserverGuard` members auto-reset when the
view is destroyed on close). The handler additionally no-ops when
`!gcode_loaded_` so a bump during the not-yet-loaded window is harmless.

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

**Callback `on_toggle_sliced_colors`:** registered in the detail-view callback
block at `ui_panel_print_select.cpp:326`, following `on_print_select_print_button`
(lines 142-161). Body: `apply_preview_colors()` + `lv_obj_invalidate(gcode_viewer_)`.
The `setting_toggle_row`'s switch owns the subject write; the callback only
re-applies. A brief toast ("Showing sliced colors" / "Showing loaded colors")
confirms the change.

## Files touched

| File | Change |
|------|--------|
| `ui_xml/print_file_detail.xml` | add `setting_toggle_row` (runtime XML, no rebuild) |
| `include/ui_print_select_detail_view.h` | `detail_prefer_sliced_colors_` subject; 2 `ObserverGuard` members; method decls |
| `src/ui/ui_print_select_detail_view.cpp` | subject init + reset in `show()`; AMS observers in `create()`; `apply_preview_colors()`, `apply_sliced_tool_colors()`, `refresh_preview_colors_and_mismatch()`, `on_ams_state_changed()`; swap the two call pairs |
| `src/ui/ui_panel_print_select.cpp` | register `on_toggle_sliced_colors` |
| translations | `print.show_sliced_colors` label + compiled artifacts |
| tests | see below |

No `SettingsManager` change (view-local state).

## Threading / safety

- `observe_int_sync` on **static** `AmsState` subjects → main-thread deferred, no
  token. (Per-slot subjects would need `SubjectLifetime`; these are singletons.)
- No sync widget deletion in the handler — it only mutates subjects and re-colors
  (which patches renderer buffers in place). No `safe_delete` concerns.
- Handler no-ops when `!gcode_loaded_`; observers torn down with the widget tree.

## Testing

- **Pure helper unit test:** factor color selection into a testable free/static
  helper `select_preview_colors(prefer_sliced, sliced_hexes, mapped_colors)`
  returning the applied vector, and unit-test: prefer_sliced → slicer hexes;
  else → mapped colors; empty-slicer edge fallback. (`[filament]` tag.)
- **Mapping-color regression:** assert `FilamentMapper::compute_defaults()` +
  `get_mapped_colors()` yield loaded slot colors when tools resolve, slicer color
  when a tool maps to no slot. Guards the already-shipped core against
  regression. (`[filament]`.)
- **Live subscription:** if feasible under `XMLTestFixture`, bump
  `get_slots_version_subject()` after changing a mock slot color and assert the
  applied override vector updates. If the harness makes this too heavy, document
  device verification instead (see below) rather than a fake `REQUIRE(true)`.
- **Device verification (manual):** on K2 Plus (CFS) or AD5X (IFS) — open a
  multi-tool file, confirm preview uses loaded colors; reload a slot with a
  different color and confirm the preview re-colors live; toggle "Show sliced
  colors" and confirm it flips to slicer intent and back; re-open a file and
  confirm it resets to actual.

## Open verification items (resolve during implementation)

1. `setting_toggle_row` exact prop names and whether the `ui_switch` writes the
   bound subject or the callback must — read `setting_toggle_row.xml` +
   `ui_switch` before wiring.
2. Existing MDI glyph availability if any icon is added (avoid `make
   regen-fonts` by reusing a registered codepoint) — N/A if the labeled row
   needs no new glyph.
3. Confirm `overlay visible` guard predicate in the handler (which member/flag
   the detail view uses for "currently shown").
