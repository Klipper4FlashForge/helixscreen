# Per-Unit Dryer Support (End-to-End)

**Date:** 2026-07-12
**Branch:** `feature/qidi-box-multi-unit`
**Status:** Implemented 2026-07-12

## Problem

The `feature/qidi-box-multi-unit` branch reworks the QIDI AMS backend from a
single-`AmsUnit`-with-a-growing-slot-vector model into one `AmsUnit` per physical
box (up to 4 boxes x 4 slots). The slot paths (load/unload/change_tool/eject/
set_slot_info/set_tool_mapping/current_error) and per-box heater/temp/humidity
reads are all per-unit in that branch. **Dryer state is not.**

Dryer state is aggregated globally across all boxes:

- `apply_heater_status` sets `dryer_info_` temp/target to the **max across all
  boxes** (`src/printer/ams_backend_qidi.cpp` ~`:488/:491`).
- `apply_box_extras` collapses every box's `end_time` into one countdown
  (~`:330-353`).
- `AmsBackend::get_dryer_info()` takes **no** unit parameter
  (`include/ams_backend.h:1028`), so there is no way to read a specific box's
  dryer state.
- `src/printer/ams_state.cpp:1438` explicitly broadcasts one `DryerInfo` to every
  unit's indicator ("system-level dryer applies to all units").

Consequence: in a multi-box setup, every box's drying UI mirrors box 1 — same
countdown, same target, same active flag.

### This is an abstraction-layer gap, not a QIDI quirk

No backend (ACE, Happy Hare, QIDI, Mock) models dryer state per-unit today. The
gap is lopsided — two of three layers are already per-unit:

| Layer | Per-unit today? |
|---|---|
| **Control/write** — `start_drying(..., int unit=0)` / `stop_drying(int unit=0)` (`ams_backend.h:1043/1058`) | Yes. QIDI maps `unit -> box = unit+1`, emits `ENABLE_BOX_DRY BOX={n}`. |
| **UI subjects** — `env_ind_drying_active_[MAX_UNITS]` / `env_ind_drying_text_[MAX_UNITS]` (`ams_state.h:1393-1394`) | Yes. Arrays sized to `MAX_UNITS=4`. |
| **State read-back** — `DryerInfo` + `get_dryer_info()` | **No.** Single struct, no unit param. |

Additionally, the per-unit **UI binding** is only half-built and this is not
dryer-specific: `ui_xml/components/ams_environment_indicator.xml` hardcodes
`ams_env_ind_0_*` subject names, so every unit card renders **unit 0's** badge
(temp, humidity, *and* drying), even though `ams_state.cpp` already registers
`ams_env_ind_%d_*` subjects for all units. The dryer control overlay
(`AmsEnvironmentOverlay`) is likewise only ever opened with unit `0`
(`ui_panel_ams.cpp:99`) and reads the global `DryerInfo`.

Because we do **not** support dryer-per-unit anywhere, the fix belongs at the
abstraction layer, not as a QIDI-local hack. All work lands on the branch; it is
not merged to main until complete.

## Scope

**Full end-to-end**, so per-unit dryer *and* per-unit temp/humidity are actually
visible to the user (the badge parameterization fixes both with one mechanism):

1. Data model — `get_dryer_info(unit)` + per-unit storage.
2. State fan-out — per-unit `ams_state.cpp` drivers.
3. UI binding — parameterize the env-indicator badge by unit index (pure-XML) +
   route the overlay to the clicked unit.

## Design

### 1. Data model (`include/ams_backend.h` + backends)

- `DryerInfo` (`include/ams_types.h:1171`) is unchanged structurally; it stops
  being a singleton per backend.
- Base interface changes:
  - `get_dryer_info(int unit = 0)` (was no-arg) — `ams_backend.h:1028`.
  - `update_drying(..., int unit = 0)` gains `unit` — `ams_backend.h:1074`.
  - `start_drying` / `stop_drying` already carry `unit` — **no change**.
  - The `= 0` default keeps every single-unit call site compiling untouched.
- Per-backend storage: replace the scalar `DryerInfo dryer_info_` member with
  `std::vector<DryerInfo>` indexed by unit (and QIDI's `dry_end_epoch_` becomes a
  per-unit vector), sized to the unit count, index 0 default.
  - QIDI's write path already keys on `box = unit+1`, so only its read-back /
    storage changes: `apply_heater_status` and `apply_box_extras` write into the
    matching unit's `DryerInfo` instead of a global max/collapse.
  - ACE / Happy Hare / Mock get a size-1 vector — behavior identical to today.
  - `get_dryer_info(unit)` returns the element, or a default not-supported
    `DryerInfo` if `unit` is out of range.

Affected headers/impls: `ams_backend.h`, `ams_backend_ace.{h,cpp}`,
`ams_backend_qidi.{h,cpp}`, `ams_backend_happy_hare.{h,cpp}`,
`ams_backend_mock.{h,cpp}`.

### 2. State fan-out (`src/printer/ams_state.cpp`)

- Replace the `:1438` "system-level dryer applies to all units" broadcast with a
  per-`i` loop calling `get_dryer_info(i)` and feeding each
  `env_ind_drying_active_[i]` / `env_ind_drying_text_[i]`.
- The **global scalar** dryer subjects (`dryer_active_`, `dryer_target_temp_`,
  `dryer_remaining_min_`, `dryer_current_temp_`, `dryer_progress_pct_`,
  `dryer_supported_`) that feed the control overlay become a **mirror of the
  currently-opened unit** — refreshed from `get_dryer_info(overlay_unit)` when the
  overlay opens and on updates for that unit. Minimal churn: the overlay keeps its
  existing subject reads; they simply point at the right unit now.
  - `sync_dryer_from_backend()` gains the notion of which unit it mirrors.

### 3. UI binding (pure-XML param passthrough)

Proven precedent: `ui_xml/filament_sensor_indicator.xml` passes a subject *name*
as a `<prop type="subject">` and consumes it via `bind_*="$subject"`. The engine
substitutes whole-value `$params` at parse time (`lib/helix-xml/src/xml/lv_xml.c`
`resolve_params` ~`:904-947`); **no in-string interpolation**, so fully-expanded
indexed subject names must be passed from C++.

- `ams_environment_indicator.xml`: add an `<api>` block with `type="subject"`
  props (`temp_text`, `humidity_text`, `humidity_status`, `humidity_visible`,
  `visible`, `drying_active`, `drying_text`) and replace the hardcoded
  `ams_env_ind_0_*` names with `$`-params.
- `ams_unit_card.xml`: declare matching props and forward them to the nested
  `<ams_environment_indicator ...="$..."/>`.
- `ui_panel_ams_overview.cpp` card-creation loop (~`:320`, currently passes
  `nullptr` attrs): build a per-unit attrs array with the fully-expanded indexed
  names (`ams_env_ind_1_temp_text`, ...) **plus** the unit index, and pass it to
  `lv_xml_create` (the `ui_panel_power.cpp:207` attrs-array pattern).
- **Click routing:** the badge click (`ui_panel_ams.cpp:95-101`, hardcoded
  `show(..., 0)`) must open the overlay for the clicked unit. Store the unit index
  on the card (via a user-data attr) and read it in `on_env_indicator_clicked`,
  passing it to `AmsEnvironmentOverlay::show(screen, unit)`.
- `ui_ams_environment_overlay.cpp`: its direct `get_dryer_info()` reads
  (~`:344/:526/:653`) gain `unit_index_`; `start_drying`/`stop_drying` already
  pass `unit_index_`.

### XML gotchas to honor

- Use a **single `bind_flag_if_not_eq`** for exclusive visibility, not multiple
  `bind_flag_if_eq` on the same object (independent observers, last-write-wins
  race) — [L042].
- `$param` substitution is whole-value only; expand indexed names C++-side.

## Testing

- **Data layer (unit, real — the test the single-struct model couldn't express):**
  extend `tests/unit/test_ams_backend_qidi.cpp` — box1 drying + box2 idle =>
  `get_dryer_info(0)` and `get_dryer_info(1)` return **distinct** active / temp /
  target / countdown. Assert `update_drying(unit)` targets the right box.
- **State fan-out:** assert `env_ind_drying_active_[0] != [1]` when only one box
  dries; assert per-unit temp/humidity subjects diverge.
- **UI:** per [L060], XML binding + click routing needs interactive on-device
  verify. QIDI Box ships blind (no hardware here), so on-device verify is a
  contributor/user step; locally, exercise the wiring with the mock backend
  configured for multiple units.

## File inventory (~13 files)

`include/ams_backend.h`, `include/ams_types.h` (only if `DryerInfo` needs a
not-supported default helper), `ams_backend_{ace,qidi,happy_hare,mock}.{h,cpp}`,
`src/printer/ams_state.{h,cpp}`, `src/ui/ui_ams_environment_overlay.cpp`,
`src/ui/ui_panel_ams.cpp`, `src/ui/ui_panel_ams_overview.cpp`,
`ui_xml/components/ams_environment_indicator.xml`, `ui_xml/ams_unit_card.xml`,
`tests/unit/test_ams_backend_qidi.cpp`.

MAJOR work — done in the `.worktrees/qidi-box-multi-unit` worktree off the branch.

## Decisions made (recorded rather than asked)

- **Storage:** `std::vector<DryerInfo>` per backend, indexed by unit. Low
  controversy, matches `AmsUnit` indexing.
- **Global scalar dryer subjects:** kept as a mirror of the opened unit rather
  than removed. Minimizes churn to the existing overlay wiring.
- **Badge per-index mechanism:** pure-XML param passthrough (vs the
  `ams_slot`-style C++ registered-widget alternative), per user's choice —
  matches the `filament_sensor_indicator` precedent, lighter engine touch.

## Out of scope (pre-existing on main, untouched)

`recover` / `reset` / `cancel`, bypass enable/disable, and `clear_slot_override`
QIDI stubs; the `qidi_box_64.png` asset TODO. Backends that don't support drying
at all (AFC, Snapmaker, Toolchanger, AD5X/IFS, CFS) keep the base not-supported
default via `get_dryer_info(unit=0)`.
