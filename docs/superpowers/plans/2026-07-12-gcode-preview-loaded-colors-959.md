# #959 G-code Preview Loaded-Slot Colors — Live Subscription + Sliced Toggle — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the print-select detail-view g-code preview re-color live when AMS slot colors change externally, and add a session-local "Show sliced colors" toggle to view slicer-intended colors instead of loaded ones.

**Architecture:** The loaded-color routing already ships (`get_mapped_colors()` → `apply_mapped_tool_colors()`). This adds (a) a pure extracted color resolver for testability, (b) a lightweight card slot-data refresh that preserves user mappings, (c) one `AmsState::slots_version` observer on the detail view, and (d) a `setting_toggle_row` bound to a view-local subject that dispatches between sliced and actual color paths.

**Tech Stack:** C++17, LVGL 9.5, HelixScreen XML engine, Catch2 tests, spdlog.

## Global Constraints

- **Design spec:** `docs/superpowers/specs/2026-07-12-gcode-preview-loaded-colors-959-design.md` (authoritative).
- **No `SettingsManager` / persisted preference** — the toggle is view-local, resets to actual (loaded colors) on every `show()`.
- **Threading:** `observe_int_sync` on the **static singleton** `AmsState::slots_version` subject — no `SubjectLifetime` token (L084 applies only to dynamic per-slot subjects). `ObserverGuard` cleanup is `reset()`, never `release()` (L085).
- **The AMS-change handler MUST NOT call `FilamentMappingCard::update()`** — it recomputes `mappings_` and would wipe a user's manual remap. Use the new `refresh_slot_data()`.
- **Design tokens / semantic widgets only** in XML (L008) — no hardcoded colors/spacing.
- **XML is runtime-loaded** (L031) — XML-only changes need a relaunch, not a rebuild. C++ changes need `make -j`.
- **Commit message format:** `fix(scope): thing (prestonbrown/helixscreen#959)` or `feat(scope): …`.
- **Build discipline:** before `make -j`, check `pgrep -x cc1plus` + `free -h` (memory rule); run builds with `run_in_background: true`.

---

### Task 1: Extract `FilamentMapper::resolve_display_colors` (pure, TDD)

Move the per-tool color-resolution loop out of `FilamentMappingCard::get_mapped_colors()` into a pure static on `FilamentMapper`, and unit-test it. This is the only cleanly-unit-testable unit and guards the shipped #959 core plus the live-recolor semantics.

**Files:**
- Modify: `include/filament_mapper.h` (add static method decl to `class FilamentMapper`)
- Modify: `src/printer/filament_mapper.cpp` (add impl)
- Modify: `src/ui/ui_filament_mapping_card.cpp:254-275` (delegate `get_mapped_colors()`)
- Test: `tests/unit/test_filament_mapper.cpp` (create if absent; else append)

**Interfaces:**
- Produces: `static std::vector<uint32_t> FilamentMapper::resolve_display_colors(const std::vector<GcodeToolInfo>& tools, const std::vector<ToolMapping>& mappings, const std::vector<AvailableSlot>& slots);`
- Consumes: `GcodeToolInfo{tool_index,color_rgb,material}`, `ToolMapping{tool_index,mapped_slot,mapped_backend,is_auto,...}`, `AvailableSlot{slot_index,backend_index,color_rgb,...}` — all already in `include/filament_mapper.h`.

- [ ] **Step 1: Confirm the test file + verify `FilamentMapper` is unit-testable in isolation**

Run: `ls tests/unit/test_filament_mapper*.cpp 2>/dev/null; grep -rn "FilamentMapper::compute_defaults" tests/ | head`
Expected: find an existing filament-mapper test to append to (e.g. `tests/unit/test_filament_mapper.cpp`). If none, create the file with the header block copied from a sibling pure-logic test (e.g. `tests/unit/test_gcode_parser.cpp` top matter — SPDX + `#include "catch_amalgamated.hpp"` or the repo's Catch include, and `#include "filament_mapper.h"`). Note the exact Catch include the neighbor uses.

- [ ] **Step 2: Write the failing test**

Add to `tests/unit/test_filament_mapper.cpp`:

```cpp
#include "filament_mapper.h"

using helix::AvailableSlot;
using helix::FilamentMapper;
using helix::GcodeToolInfo;
using helix::ToolMapping;

namespace {
GcodeToolInfo tool(int idx, uint32_t rgb) {
    GcodeToolInfo t;
    t.tool_index = idx;
    t.color_rgb = rgb;
    return t;
}
AvailableSlot slot(int slot_idx, int backend, uint32_t rgb) {
    AvailableSlot s;
    s.slot_index = slot_idx;
    s.backend_index = backend;
    s.color_rgb = rgb;
    s.is_empty = false;
    return s;
}
ToolMapping mapped(int tool_idx, int slot_idx, int backend) {
    ToolMapping m;
    m.tool_index = tool_idx;
    m.mapped_slot = slot_idx;
    m.mapped_backend = backend;
    m.is_auto = false;
    return m;
}
ToolMapping unresolved(int tool_idx) {
    ToolMapping m;
    m.tool_index = tool_idx;
    m.mapped_slot = -1;
    m.mapped_backend = -1;
    m.is_auto = true;
    return m;
}
} // namespace

TEST_CASE("resolve_display_colors uses loaded slot color for mapped tools", "[filament]") {
    std::vector<GcodeToolInfo> tools = {tool(0, 0xFFFF00)};   // slicer yellow
    std::vector<AvailableSlot> slots = {slot(3, 0, 0x0000FF)}; // slot 3 loaded blue
    std::vector<ToolMapping> mappings = {mapped(0, 3, 0)};     // T0 -> slot 3

    auto colors = FilamentMapper::resolve_display_colors(tools, mappings, slots);
    REQUIRE(colors.size() == 1);
    REQUIRE(colors[0] == 0x0000FF); // blue (loaded), NOT yellow (slicer)
}

TEST_CASE("resolve_display_colors falls back to slicer color when unmapped", "[filament]") {
    std::vector<GcodeToolInfo> tools = {tool(0, 0xFFFF00)};
    std::vector<AvailableSlot> slots = {slot(3, 0, 0x0000FF)};
    std::vector<ToolMapping> mappings = {unresolved(0)}; // is_auto, no slot

    auto colors = FilamentMapper::resolve_display_colors(tools, mappings, slots);
    REQUIRE(colors.size() == 1);
    REQUIRE(colors[0] == 0xFFFF00); // slicer yellow
}

TEST_CASE("resolve_display_colors reflects a slot color change (live re-color)", "[filament]") {
    std::vector<GcodeToolInfo> tools = {tool(0, 0xFFFF00)};
    std::vector<ToolMapping> mappings = {mapped(0, 3, 0)};

    std::vector<AvailableSlot> before = {slot(3, 0, 0x0000FF)};
    REQUIRE(FilamentMapper::resolve_display_colors(tools, mappings, before)[0] == 0x0000FF);

    std::vector<AvailableSlot> after = {slot(3, 0, 0x00FF00)}; // reloaded green
    REQUIRE(FilamentMapper::resolve_display_colors(tools, mappings, after)[0] == 0x00FF00);
}

TEST_CASE("resolve_display_colors preserves a manual mapping when an unrelated slot changes",
          "[filament]") {
    std::vector<GcodeToolInfo> tools = {tool(0, 0xFFFF00)};
    std::vector<ToolMapping> mappings = {mapped(0, 2, 0)}; // T0 -> slot 2 (green)
    std::vector<AvailableSlot> slots = {slot(0, 0, 0xFF0000), slot(2, 0, 0x00FF00)};

    // slot 0 changes color; T0 is mapped to slot 2, so its color must stay green.
    slots[0].color_rgb = 0x123456;
    auto colors = FilamentMapper::resolve_display_colors(tools, mappings, slots);
    REQUIRE(colors[0] == 0x00FF00);
}
```

- [ ] **Step 3: Run the test to verify it fails to compile (method missing)**

Run: `make test 2>&1 | grep -i "resolve_display_colors\|error" | head`
Expected: FAIL — `'resolve_display_colors' is not a member of 'helix::FilamentMapper'`.

- [ ] **Step 4: Declare the method** in `include/filament_mapper.h` inside `class FilamentMapper`, next to `compute_defaults`:

```cpp
    /// Resolve the per-tool DISPLAY color: the mapped slot's loaded color_rgb
    /// when the tool is explicitly mapped to an existing slot, else the tool's
    /// slicer color_rgb (or 0x808080 when no tool info). Pure; no LVGL/AMS.
    static std::vector<uint32_t> resolve_display_colors(
        const std::vector<GcodeToolInfo>& tools,
        const std::vector<ToolMapping>& mappings,
        const std::vector<AvailableSlot>& slots);
```

- [ ] **Step 5: Implement** in `src/printer/filament_mapper.cpp` (near `compute_defaults`):

```cpp
std::vector<uint32_t> FilamentMapper::resolve_display_colors(
    const std::vector<GcodeToolInfo>& tools, const std::vector<ToolMapping>& mappings,
    const std::vector<AvailableSlot>& slots) {
    std::vector<uint32_t> colors;
    colors.reserve(mappings.size());

    for (size_t i = 0; i < mappings.size(); ++i) {
        const auto& mapping = mappings[i];
        uint32_t color = (i < tools.size()) ? tools[i].color_rgb : 0x808080;

        if (!mapping.is_auto && mapping.mapped_slot >= 0) {
            for (const auto& s : slots) {
                if (s.slot_index == mapping.mapped_slot &&
                    s.backend_index == mapping.mapped_backend) {
                    color = s.color_rgb;
                    break;
                }
            }
        }
        colors.push_back(color);
    }
    return colors;
}
```

- [ ] **Step 6: Delegate `get_mapped_colors()`** — replace the loop body in `src/ui/ui_filament_mapping_card.cpp:254-275` with:

```cpp
std::vector<uint32_t> FilamentMappingCard::get_mapped_colors() const {
    return helix::FilamentMapper::resolve_display_colors(tool_info_, mappings_, available_slots_);
}
```

(Verify `filament_mapper.h` is included in `ui_filament_mapping_card.cpp`; it is already used for `compute_defaults`.)

- [ ] **Step 7: Run the test to verify it passes**

Run: `make test-run 2>&1 | tail -20; ./build/bin/helix-tests "[filament]" 2>&1 | tail -15`
Expected: PASS — all four `resolve_display_colors` cases green, no other `[filament]` regressions.

- [ ] **Step 8: Commit**

```bash
git add include/filament_mapper.h src/printer/filament_mapper.cpp src/ui/ui_filament_mapping_card.cpp tests/unit/test_filament_mapper.cpp
git commit -m "refactor(filament): extract FilamentMapper::resolve_display_colors + tests (prestonbrown/helixscreen#959)"
```

---

### Task 2: Add `FilamentMappingCard::refresh_slot_data()`

A lightweight refresh that re-pulls loaded slot colors/presence and re-renders **without** recomputing `mappings_`, so live AMS changes never wipe a user's mapping.

**Files:**
- Modify: `include/ui_filament_mapping_card.h` (declare method)
- Modify: `src/ui/ui_filament_mapping_card.cpp` (impl, near `update()`)

**Interfaces:**
- Produces: `void FilamentMappingCard::refresh_slot_data();`
- Consumes: `AmsState::instance().is_available()`, `AmsState::instance().collect_available_slots()`, existing private `available_slots_`, `rebuild_compact_view()`.

- [ ] **Step 1: Declare** in `include/ui_filament_mapping_card.h`, next to `void update(...)`:

```cpp
    /// Re-pull loaded slot colors/presence from AmsState and re-render, WITHOUT
    /// recomputing tool->slot mappings — preserves the user's manual remap and
    /// the auto assignment. Used by the detail view's live AMS-change handler.
    void refresh_slot_data();
```

- [ ] **Step 2: Implement** in `src/ui/ui_filament_mapping_card.cpp` (immediately after `update()`):

```cpp
void FilamentMappingCard::refresh_slot_data() {
    if (!card_ || !rows_container_) {
        return;
    }
    if (!AmsState::instance().is_available()) {
        return;
    }
    // Refresh loaded colors + presence only; mappings_ and tool_info_ untouched.
    available_slots_ = AmsState::instance().collect_available_slots();
    rebuild_compact_view();
}
```

- [ ] **Step 3: Build to verify it compiles**

Run: `make -j 2>&1 | tail -5`
Expected: clean build (no test yet — behavior is exercised in Task 4 + device verification).

- [ ] **Step 4: Commit**

```bash
git add include/ui_filament_mapping_card.h src/ui/ui_filament_mapping_card.cpp
git commit -m "feat(filament): FilamentMappingCard::refresh_slot_data (mappings-preserving) (prestonbrown/helixscreen#959)"
```

---

### Task 3: Detail view — sliced/actual color dispatch

Add the view-local subject, the dispatcher, the sliced path, reset-on-show, and swap the two load callsites. After this task the toggle has no UI yet, but `apply_preview_colors()` respects the subject.

**Files:**
- Modify: `include/ui_print_select_detail_view.h` (subject member + 3 method decls)
- Modify: `src/ui/ui_print_select_detail_view.cpp` (init, reset, methods, callsite swaps)

**Interfaces:**
- Produces: `lv_subject_t detail_prefer_sliced_colors_` (XML name `"detail_prefer_sliced_colors"`); `void apply_preview_colors();`, `void apply_sliced_tool_colors();`
- Consumes: existing `apply_tool_colors()`, `apply_mapped_tool_colors()`, `current_filament_colors_`, `ui_gcode_viewer_set_tool_colors()`, `helix::parse_hex_color()`.

- [ ] **Step 1: Declare the subject** in `include/ui_print_select_detail_view.h`, right after `detail_gcode_loading_{};` (~line 513):

```cpp
    // 1 = show slicer-intended colors instead of loaded AMS slot colors.
    // View-local, resets to 0 (actual) on every show().
    lv_subject_t detail_prefer_sliced_colors_{};
```

- [ ] **Step 2: Declare the methods** in the same header, next to `apply_mapped_tool_colors()`:

```cpp
    void apply_preview_colors();      // dispatch sliced vs actual by the subject
    void apply_sliced_tool_colors();  // force slicer palette into the viewer
```

- [ ] **Step 3: Init the subject** in `src/ui/ui_print_select_detail_view.cpp` `init_subjects()`, right after the `detail_gcode_loading_` line (~line 131):

```cpp
    // Preview color mode: 0 = actual (loaded slot colors), 1 = sliced (slicer intent)
    UI_MANAGED_SUBJECT_INT(detail_prefer_sliced_colors_, 0, "detail_prefer_sliced_colors",
                           subjects_);
```

- [ ] **Step 4: Reset in `show()`** — add to the reset block at `src/ui/ui_print_select_detail_view.cpp:404-406`, after `lv_subject_set_int(&filament_mismatch_, 0);`:

```cpp
    lv_subject_set_int(&detail_prefer_sliced_colors_, 0); // every open starts on actual colors
```

- [ ] **Step 5: Add the dispatcher + sliced path** in `src/ui/ui_print_select_detail_view.cpp`, immediately after `apply_mapped_tool_colors()` (after line 960):

```cpp
void PrintSelectDetailView::apply_preview_colors() {
    if (!gcode_viewer_ || !gcode_loaded_) {
        return;
    }
    if (lv_subject_get_int(&detail_prefer_sliced_colors_) == 1) {
        apply_sliced_tool_colors();
    } else {
        // Actual (loaded) colors: firmware/slicer base, then mapped overrides win.
        apply_tool_colors();
        apply_mapped_tool_colors();
    }
}

void PrintSelectDetailView::apply_sliced_tool_colors() {
    if (!gcode_viewer_ || !gcode_loaded_ || current_filament_colors_.empty()) {
        return;
    }
    std::vector<uint32_t> tool_colors;
    for (const auto& hex : current_filament_colors_) {
        auto parsed = helix::parse_hex_color(hex);
        if (parsed) {
            tool_colors.push_back(*parsed);
        }
    }
    if (!tool_colors.empty()) {
        ui_gcode_viewer_set_tool_colors(gcode_viewer_, tool_colors);
        lv_obj_invalidate(gcode_viewer_);
    }
}
```

- [ ] **Step 6: Swap callsite A** at `src/ui/ui_print_select_detail_view.cpp:1404-1405`. Replace:

```cpp
                    // Apply AMS or slicer tool colors, then override with mapped colors
                    self->apply_tool_colors();
                    self->apply_mapped_tool_colors();
```

with:

```cpp
                    // Apply preview colors respecting the sliced/actual toggle
                    // (default actual: AMS/slicer base then mapped overrides).
                    self->apply_preview_colors();
```

- [ ] **Step 7: Swap callsite B** at `src/ui/ui_print_select_detail_view.cpp:1505-1506`. Replace the identical pair:

```cpp
                                    // Apply AMS or slicer tool colors, then override with mapped
                                    // colors
                                    self->apply_tool_colors();
                                    self->apply_mapped_tool_colors();
```

with:

```cpp
                                    // Apply preview colors respecting the sliced/actual toggle.
                                    self->apply_preview_colors();
```

- [ ] **Step 8: Build**

Run: `make -j 2>&1 | tail -5`
Expected: clean build.

- [ ] **Step 9: Commit**

```bash
git add include/ui_print_select_detail_view.h src/ui/ui_print_select_detail_view.cpp
git commit -m "feat(print-select): sliced/actual preview color dispatch subject (prestonbrown/helixscreen#959)"
```

---

### Task 4: Detail view — live subscription + shared refresh

Add the `slots_version` observer, the guarded handler, and the shared refresh method; route the existing mappings-changed callback through it so the paths can't drift.

**Files:**
- Modify: `include/ui_print_select_detail_view.h` (one `ObserverGuard` member + 2 method decls + includes)
- Modify: `src/ui/ui_print_select_detail_view.cpp` (observer setup, handler, shared method, callback swap)

**Interfaces:**
- Produces: `void on_ams_state_changed();`, `void refresh_preview_colors_and_mismatch();`, member `ObserverGuard slots_version_observer_;`
- Consumes: `AmsState::instance().get_slots_version_subject()`, `helix::observe_int_sync`, `filament_mapping_card_.refresh_slot_data()` (Task 2), `apply_preview_colors()` (Task 3), `is_visible()` (OverlayBase), existing `recompute_preflight()`, `update_color_swatches()`, `tools_used_effective()`.

- [ ] **Step 1: Add includes** to `src/ui/ui_print_select_detail_view.cpp` (near the top with the other `#include`s), if not already present:

```cpp
#include "ams_state.h"
#include "observer_factory.h"
```

Run first: `grep -n '#include "ams_state.h"\|#include "observer_factory.h"' src/ui/ui_print_select_detail_view.cpp` — add only the missing ones.

- [ ] **Step 2: Declare the observer member + methods** in `include/ui_print_select_detail_view.h`. Add the member near the other RAII members (after `SubjectManager subjects_;`), and ensure `#include "ui_observer_guard.h"` is present in the header:

```cpp
    // Live re-color when AMS slot colors/presence change while this view is open.
    // Observes the STATIC AmsState::slots_version subject — no SubjectLifetime
    // token (singleton, not a per-slot dynamic subject).
    ObserverGuard slots_version_observer_;
```

And the method decls next to `apply_preview_colors()`:

```cpp
    void on_ams_state_changed();               // AMS slots_version observer handler
    void refresh_preview_colors_and_mismatch(); // shared by card-edit + AMS observer
```

- [ ] **Step 3: Set up the observer** at the end of `init_subjects()` in `src/ui/ui_print_select_detail_view.cpp` (after the last `UI_MANAGED_SUBJECT_*` line, before `subjects_initialized_ = true;`):

```cpp
    // Re-color the preview live when a slot's loaded color/presence changes
    // (filament reloaded). Static singleton subject -> plain ObserverGuard, no
    // lifetime token. Handler no-ops while the view is closed.
    slots_version_observer_ = helix::observe_int_sync<PrintSelectDetailView>(
        AmsState::instance().get_slots_version_subject(), this,
        [](PrintSelectDetailView* self, int /*version*/) { self->on_ams_state_changed(); });
```

(Verify the namespace of `observe_int_sync` — Task-prep confirmed `helix::observe_int_sync` from `observer_factory.h`. If the existing `PrintStatusPanel` usage calls it unqualified, match that.)

- [ ] **Step 4: Implement the handler + shared method** in `src/ui/ui_print_select_detail_view.cpp`, after `apply_sliced_tool_colors()`:

```cpp
void PrintSelectDetailView::on_ams_state_changed() {
    // Cheap guard: no work while closed / not yet loaded. gcode_viewer_ is nulled
    // on on_deactivate(), so this also protects a dangling viewer pointer.
    if (!is_visible() || !gcode_loaded_ || !gcode_viewer_) {
        return;
    }
    // Refresh loaded slot colors WITHOUT recomputing mappings (preserve remap).
    filament_mapping_card_.refresh_slot_data();
    refresh_preview_colors_and_mismatch();
}

void PrintSelectDetailView::refresh_preview_colors_and_mismatch() {
    apply_preview_colors();
    lv_subject_set_int(&filament_mismatch_, filament_mapping_card_.has_mismatch() ? 1 : 0);
    // Re-evaluate the pre-flight gate so a subsequent Print reflects the current
    // tool->slot mapping (native remap flow reads get_filament_mappings()).
    recompute_preflight();
    // Re-render the FILAMENTS chips so slot number + present color track the
    // current mapping/slot state.
    if (lv_subject_get_int(&color_swatches_visible_) == 1) {
        update_color_swatches(tools_used_effective(), current_filament_colors_);
    }
}
```

- [ ] **Step 5: Route the mappings-changed callback through the shared method** — replace the body of the lambda at `src/ui/ui_print_select_detail_view.cpp:301-314` with:

```cpp
    filament_mapping_card_.set_on_mappings_changed([this]() {
        // The card already refreshed its own slot/mapping state from the user's
        // edit — just re-color + re-gate. set_mappings() fires this synchronously
        // on the main thread, so a direct call is safe.
        refresh_preview_colors_and_mismatch();
    });
```

- [ ] **Step 6: Build**

Run: `make -j 2>&1 | tail -5`
Expected: clean build.

- [ ] **Step 7: Run existing detail-view / filament tests for regressions**

Run: `make test-run 2>&1 | tail -20; ./build/bin/helix-tests "[filament]" 2>&1 | tail -10`
Expected: PASS — no regressions. (Note in the task result that the live-subscription behavior itself is covered by device verification in Task 8, not a unit test, per the spec.)

- [ ] **Step 8: Commit**

```bash
git add include/ui_print_select_detail_view.h src/ui/ui_print_select_detail_view.cpp
git commit -m "feat(print-select): live re-color preview on AMS slot changes (prestonbrown/helixscreen#959)"
```

---

### Task 5: Detail view — `set_prefer_sliced_colors` + toast

Public entry point the panel callback forwards to: writes the subject, re-applies, and toasts.

**Files:**
- Modify: `include/ui_print_select_detail_view.h` (public method decl)
- Modify: `src/ui/ui_print_select_detail_view.cpp` (impl + toast include)

**Interfaces:**
- Produces: `void set_prefer_sliced_colors(bool prefer_sliced);` (public)
- Consumes: `detail_prefer_sliced_colors_`, `apply_preview_colors()`, `ToastManager::instance().show(...)`.

- [ ] **Step 1: Add toast include** to `src/ui/ui_print_select_detail_view.cpp` if absent:

Run: `grep -n 'ui_toast_manager.h' src/ui/ui_print_select_detail_view.cpp` — if empty, add `#include "ui_toast_manager.h"`.

- [ ] **Step 2: Declare the public method** in `include/ui_print_select_detail_view.h` (in the public section, near `show()`):

```cpp
    /// Toggle the preview between slicer-intended colors (true) and actual loaded
    /// AMS slot colors (false). Session-local; the panel's toggle callback calls
    /// this with the switch's checked state.
    void set_prefer_sliced_colors(bool prefer_sliced);
```

- [ ] **Step 3: Implement** in `src/ui/ui_print_select_detail_view.cpp`, after `apply_preview_colors()`:

```cpp
void PrintSelectDetailView::set_prefer_sliced_colors(bool prefer_sliced) {
    lv_subject_set_int(&detail_prefer_sliced_colors_, prefer_sliced ? 1 : 0);
    apply_preview_colors();
    if (gcode_viewer_) {
        lv_obj_invalidate(gcode_viewer_);
    }
    ToastManager::instance().show(ToastSeverity::INFO,
                                  prefer_sliced ? "Showing sliced colors"
                                                : "Showing loaded colors",
                                  2000);
}
```

- [ ] **Step 4: Build**

Run: `make -j 2>&1 | tail -5`
Expected: clean build.

- [ ] **Step 5: Commit**

```bash
git add include/ui_print_select_detail_view.h src/ui/ui_print_select_detail_view.cpp
git commit -m "feat(print-select): set_prefer_sliced_colors entry point + toast (prestonbrown/helixscreen#959)"
```

---

### Task 6: Panel — toggle callback wiring

Register the XML callback and forward it to the detail view.

**Files:**
- Modify: `include/ui_panel_print_select.h` (public forwarder decl)
- Modify: `src/ui/ui_panel_print_select.cpp` (static callback + registration + forwarder impl)

**Interfaces:**
- Produces: XML callback name `"on_toggle_sliced_colors"`; `void PrintSelectPanel::forward_sliced_colors_toggle(bool checked);`
- Consumes: `get_global_print_select_panel()`, `detail_view_` (`std::unique_ptr<PrintSelectDetailView>`), `PrintSelectDetailView::set_prefer_sliced_colors(bool)` (Task 5).

- [ ] **Step 1: Declare the forwarder** in `include/ui_panel_print_select.h` (public section, near `show_detail_view()`):

```cpp
    /// Forward the "Show sliced colors" toggle to the detail view.
    void forward_sliced_colors_toggle(bool checked);
```

- [ ] **Step 2: Add the static callback** in `src/ui/ui_panel_print_select.cpp`, alongside the other static callbacks (e.g. after `on_print_select_print_button`, ~line 145):

```cpp
static void on_toggle_sliced_colors(lv_event_t* e) {
    auto* sw = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    bool checked = lv_obj_has_state(sw, LV_STATE_CHECKED);
    get_global_print_select_panel().forward_sliced_colors_toggle(checked);
}
```

- [ ] **Step 3: Register it** in the `register_xml_callbacks({...})` block at `src/ui/ui_panel_print_select.cpp:317-331`, in the "Detail view callbacks" group:

```cpp
        {"on_toggle_sliced_colors", on_toggle_sliced_colors},
```

- [ ] **Step 4: Implement the forwarder** in `src/ui/ui_panel_print_select.cpp`:

```cpp
void PrintSelectPanel::forward_sliced_colors_toggle(bool checked) {
    if (detail_view_) {
        detail_view_->set_prefer_sliced_colors(checked);
    }
}
```

- [ ] **Step 5: Build**

Run: `make -j 2>&1 | tail -5`
Expected: clean build. (`PrintSelectDetailView` is already a complete type in this TU — it constructs `detail_view_` — so `set_prefer_sliced_colors` resolves.)

- [ ] **Step 6: Commit**

```bash
git add include/ui_panel_print_select.h src/ui/ui_panel_print_select.cpp
git commit -m "feat(print-select): wire on_toggle_sliced_colors callback (prestonbrown/helixscreen#959)"
```

---

### Task 7: XML — the toggle row

Add the `setting_toggle_row` under the mapping card, gated to AMS presence. Runtime XML — no rebuild, relaunch to see it.

**Files:**
- Modify: `ui_xml/print_file_detail.xml` (insert after `filament_mapping_card`, ~line 215)
- Possibly modify: English translation YAML + regenerate artifacts (only if `label_tag` required)

**Interfaces:**
- Consumes: subject `detail_prefer_sliced_colors` (Task 3), callback `on_toggle_sliced_colors` (Task 6), subject `filament_mapping_visible` (existing), component `setting_toggle_row` (existing).

- [ ] **Step 1: Determine whether `setting_toggle_row` needs `label_tag`**

Run: `sed -n '1,20p' ui_xml/setting_toggle_row.xml`
Inspect the `<prop>`/`<api>` block: if `label` alone renders (with `label_tag` optional / defaulted), use plain `label`. If the text widget only renders via `label_tag`/`translation_tag`, you must supply a tag + translation (next step).

- [ ] **Step 2: Insert the row** into `ui_xml/print_file_detail.xml` immediately after the `filament_mapping_card`'s closing `</ui_card>` (line 215), as a sibling at the same 8-space indentation:

```xml
        <!-- Preview color mode: actual (loaded slot) vs sliced (slicer intent).
             Only meaningful with an AMS mapping to compare against. -->
        <setting_toggle_row name="sliced_colors_row"
                            label="Show sliced colors"
                            subject="detail_prefer_sliced_colors"
                            callback="on_toggle_sliced_colors">
          <bind_flag_if_eq subject="filament_mapping_visible" flag="hidden" ref_value="0"/>
        </setting_toggle_row>
```

If Step 1 found `label_tag` is required, add `label_tag="SHOW_SLICED_COLORS"` to the element and do Step 3; otherwise skip Step 3.

- [ ] **Step 3 (conditional): Add the translation key + regenerate artifacts (L064)**

Run: `grep -rln "PRINT OPTIONS\|FILAMENT_MAPPING" assets/ config/ ui_xml/ i18n/ locales/ 2>/dev/null | grep -i 'yaml\|yml' | head` to find the English source YAML. Add:

```yaml
SHOW_SLICED_COLORS: "Show sliced colors"
```

Then rebuild to regenerate, and stage the generated artifacts:

```bash
make -j 2>&1 | tail -5
git add <english-source>.yaml src/generated/lv_i18n_translations.c src/generated/lv_i18n_translations.h ui_xml/translations/translations.xml
```

- [ ] **Step 4: Relaunch and eyeball the row placement**

Run (background): `./build/bin/helix-screen --test -vv -p panel_print_select 2>&1 | tee /tmp/959_xml.log`
Then STOP and ask the user to: open a multi-tool file's detail view on a mock AMS build, confirm the "Show sliced colors" row appears under the FILAMENT MAPPING card and is hidden when no AMS mapping is shown. (Per L060 — interactive UI testing requires the user; do not fake it.)

- [ ] **Step 5: Commit**

```bash
git add ui_xml/print_file_detail.xml
# plus translation artifacts if Step 3 ran
git commit -m "feat(print-select): Show sliced colors toggle row (prestonbrown/helixscreen#959)"
```

---

### Task 8: End-to-end verification on hardware

The toggle dispatch + live subscription are UI-integration behaviors; verify them on a real AMS device (no fake unit asserts, per the spec).

**Files:** none (verification only).

- [ ] **Step 1: Full build + test suite**

Run: `make -j 2>&1 | tail -5 && make test-run 2>&1 | tail -20`
Expected: clean build; all tests pass (especially `[filament]`).

- [ ] **Step 2: Deploy to an AMS device** (K2 Plus / CFS or AD5X / IFS — see memory `reference_ssh_access.md`). Build + deploy per that device's flow.

- [ ] **Step 3: Manual verification checklist** (ask the user to drive; capture the log):
  - Open a multi-tool file → preview extrudes **loaded slot colors** (not slicer).
  - Manually remap a tool to a different slot → preview re-colors to that slot's loaded color.
  - Reload a slot with a different-colored filament while the detail view is open → preview **re-colors live** and the manual remap is **preserved** (not reset).
  - Toggle **"Show sliced colors" on** → preview flips to slicer-intended colors; **off** → back to loaded.
  - Close and re-open the file → toggle is back **off** (actual), per session-local reset.

- [ ] **Step 4: Record the result** in the task output (what was verified, on which device, from the log). If any check fails, treat as a bug and loop back to the relevant task — do not mark complete.

- [ ] **Step 5: Final commit / branch finalize** — invoke `superpowers:finishing-a-development-branch`.

---

## Self-Review

**Spec coverage:**
- Live subscription (§1) → Task 4 (observer + handler) + Task 2 (`refresh_slot_data`). ✓
- `resolve_display_colors` extraction (§1a) → Task 1. ✓
- Sliced/actual toggle (§2): subject + dispatch → Task 3; entry point + toast → Task 5; callback wiring → Task 6; XML row → Task 7. ✓
- No `SettingsManager` change → confirmed across tasks (view-local subject). ✓
- Threading/safety (static subject, no token, `reset()`, no sync delete) → Task 4 comments + Global Constraints. ✓
- Testing (`resolve_display_colors` unit test + device verification) → Task 1 + Task 8. ✓
- `tool_map_version` deliberately NOT observed → Global Constraints + Task 4 scope. ✓

**Placeholder scan:** every code step contains complete code; the only conditional is Task 7 Step 3 (translation), gated on an explicit inspection in Task 7 Step 1. No TBD/TODO.

**Type consistency:** `resolve_display_colors(tools, mappings, slots) -> vector<uint32_t>` consistent across Task 1 decl/impl/call. `set_prefer_sliced_colors(bool)` consistent Task 5 decl → Task 6 call. `forward_sliced_colors_toggle(bool)` consistent Task 6 decl/impl/call. Subject XML name `"detail_prefer_sliced_colors"` consistent Task 3 init → Task 7 binding. Callback name `"on_toggle_sliced_colors"` consistent Task 6 register → Task 7 XML.
