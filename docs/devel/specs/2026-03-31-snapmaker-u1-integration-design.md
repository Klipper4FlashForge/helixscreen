# Snapmaker U1 Integration Design

**Date**: 2026-03-31
**Status**: Approved
**Scope**: Toolchanger backend, RFID filament integration, device deployment, power-loss recovery

## Overview

HelixScreen integration for the Snapmaker U1 4-toolhead color printer. The U1 runs a modified Klipper/Moonraker stack on a Rockchip RK3562 (aarch64) with proprietary touchscreen UI. HelixScreen replaces the stock UI as a reversible, non-destructive alternative with full toolchanger support.

The U1 does **not** use the viesturz/klipper-toolchanger module. It exposes tool state through custom per-extruder fields and filament data through a proprietary `filament_detect` RFID system. This requires a dedicated `AmsBackendSnapmaker` backend.

## Phasing

| Phase | Scope | Hardware Required? |
|-------|-------|--------------------|
| **1: Device deployment** | Flash Extended FW, SSH, confirm display resolution, deploy HelixScreen, verify basic operation | Yes |
| **2: Toolchanger backend** | `AmsBackendSnapmaker`, `ToolState` integration, tool switching, extruder state parsing | No (mock tests) |
| **3: RFID filament** | `filament_detect` parsing, `SlotInfo` population, `filament_feed` state, slot remapping via `extruder_map_table` | No (mock tests) |
| **4: Power-loss recovery** | Detect/offer/resume/discard an interrupted print via `virtual_sdcard.pl_env_valid` | Yes (device investigation) + No (UI, mock tests) |

## Live API Surface (from 192.168.30.103)

192 Klipper objects discovered. Key custom objects:

### Per-Extruder State (extruder, extruder1, extruder2, extruder3)

Standard Klipper fields plus Snapmaker-custom:

```json
{
  "temperature": 26.0,
  "target": 0.0,
  "power": 0.0,
  "can_extrude": false,
  "extruder_index": 0,
  "state": "PARKED",
  "park_pin": true,
  "active_pin": false,
  "grab_valid_pin": false,
  "activating_move": false,
  "extruder_offset": [0.073, -0.037, 0.0],
  "switch_count": 86,
  "retry_count": 0,
  "error_count": 1,
  "last_maintenance_count": 0,
  "pressure_advance": 0.018683,
  "motion_queue": null
}
```

### filament_detect — RFID Tag Data

4-element `info` array, one per channel:

```json
{
  "VERSION": 1,
  "VENDOR": "Snapmaker",
  "MANUFACTURER": "Polymaker",
  "MAIN_TYPE": "PLA",
  "SUB_TYPE": "SnapSpeed",
  "TRAY": 0,
  "ALPHA": 255,
  "COLOR_NUMS": 1,
  "ARGB_COLOR": 4278716941,
  "RGB_1": 526861,
  "DIAMETER": 175,
  "WEIGHT": 500,
  "LENGTH": 150,
  "DRYING_TEMP": 55,
  "DRYING_TIME": 6,
  "HOTEND_MAX_TEMP": 230,
  "HOTEND_MIN_TEMP": 190,
  "BED_TYPE": 1,
  "BED_TEMP": 60,
  "FIRST_LAYER_TEMP": 230,
  "OTHER_LAYER_TEMP": 220,
  "SKU": 900001,
  "OFFICIAL": true,
  "CARD_UID": [144, 32, 196, 2]
}
```

`filament_detect.state` is a 4-element array (0 = tag present and read successfully; other values TBD — need to test with removed/unreadable tags on hardware).

### filament_feed left / right

Split into left (extruder0-1) and right (extruder2-3) modules:

```json
{
  "extruder0": {
    "module_exist": true,
    "filament_detected": true,
    "disable_auto": false,
    "channel_state": "load_finish",
    "channel_error": "ok",
    "channel_error_state": "none",
    "channel_action_state": "load_finish"
  }
}
```

### print_task_config — Extruder Mapping

```json
{
  "extruder_map_table": [0, 1, 2, 3, 0, 0, ...],
  "extruders_used": [false, false, false, false],
  "filament_color_rgba": ["080A0DFF", "E2DEDBFF", "E72F1DFF", "F4C032FF"],
  "filament_type": ["PLA", "PLA", "PLA", "PLA"],
  "filament_vendor": ["Snapmaker", "Snapmaker", "Snapmaker", "Snapmaker"],
  "filament_exist": [true, true, true, true],
  "auto_replenish_filament": true
}
```

32-entry `extruder_map_table`: virtual slot index → physical extruder (0-3).

### Other Notable Objects

| Object | Purpose |
|--------|---------|
| `toolchanger` | Exists but **empty** (`{}`) — not used for state |
| `toolhead.extruder` | Currently active extruder name (e.g., `"extruder"`) |
| `machine_state_manager` | `main_state` + `action_code` for action gating |
| `purifier` | Air purifier fan speed, RPM, work time |
| `defect_detection` | Spaghetti/noodle/residue detection settings |
| `filament_parameters` | Per-material load/unload/flow params database |
| `filament_entangle_detect e{n}_filament` | Tangle detection per channel |
| `power_loss_check` / `power_loss_check e{n}` | Power loss recovery state |

### Available Macros (Tool-Related)

- `T0`-`T3`: Native Klipper tool change (extruder0-3)
- `T4`-`T31`: Gcode macros for virtual tool mapping
- `SM_PRINT_CHECK_SWITCH_EXTRUDER`: Print-time tool switch check
- `AUTO_FEEDING` / `MANUAL_FEEDING`: Filament load
- `INNER_FILAMENT_UNLOAD`: Filament unload
- `ROUGHLY_CLEAN_NOZZLE` / `FINELY_CLEAN_NOZZLE_STAGE_1/2`: Nozzle cleaning
- `XYZ_OFFSET_CALIBRATE` / `XYZ_OFFSET_CALIBRATE_ALL`: Tool offset calibration
- `EXTRUDER_OFFSET_ACTION_PROBE_CALIBRATE_ALL`: Probe-based calibration

---

## Phase 1: Device Deployment

### Steps

1. Flash [Extended Firmware](https://github.com/paxx12/SnapmakerU1-Extended-Firmware) via USB
2. SSH in: `ssh root@192.168.30.103` (password: `snapmaker`)
3. Confirm display: `fbset`, `cat /sys/class/graphics/fb0/virtual_size`
4. Identify stock UI service: `ps aux | grep unisrv`, `ls /etc/init.d/S*`
5. Stop stock UI: `killall unisrv`
6. Build: `make snapmaker-u1-docker`
7. Deploy: `make deploy-snapmaker-u1-fg SNAPMAKER_U1_HOST=192.168.30.103`
8. Verify: rendering, touch input, printer detection

### Display Resolution — CONFIRMED

**480x320, 32bpp** (`rockchipdrmfb`). Confirmed via `fbset` on real hardware (2026-03-31). Uses the TINY layout breakpoint. Community reports of 800x480 or 1024x600 were incorrect.

### Reversibility

| Level | Method | Revert |
|-------|--------|--------|
| 1 | SSH + killall unisrv + run HelixScreen | Reboot |
| 2 | Systemd override on writable partition | Delete override + reboot |
| 3 | Extended Firmware overlay | Reflash stock .bin via USB |

Stock UI lives on read-only SquashFS — cannot be damaged. A/B firmware slots + MaskRom = unbrickable.

---

## Phase 2: Toolchanger Backend

### New: AmsBackendSnapmaker

A new `AmsBackend` subclass dedicated to the Snapmaker U1's toolchanger system.

**File**: `include/ams_backend_snapmaker.h`, `src/printer/ams_backend_snapmaker.cpp`

#### Properties

| Property | Value |
|----------|-------|
| `type()` | `AmsType::SNAPMAKER` (new enum value) |
| `topology()` | `PARALLEL` |
| `name()` | `"Snapmaker SnapSwap"` |
| Physical tools | 4 (extruder0-3) |
| Virtual slots | Up to 32 (from `extruder_map_table`, only populated entries) |

#### Klipper Object Subscriptions

```
extruder, extruder1, extruder2, extruder3
filament_detect
filament_feed left, filament_feed right
print_task_config
toolhead
filament_motion_sensor e0_filament ... e3_filament
filament_entangle_detect e0_filament ... e3_filament
machine_state_manager
```

#### Data Mapping

| AMS Concept | U1 Source |
|-------------|-----------|
| Active tool | `extruder{n}.state != "PARKED"`, cross-check `toolhead.extruder` |
| Tool mounted | `extruder{n}.active_pin == true` |
| Tool parked | `extruder{n}.park_pin == true` |
| Tool changing | `extruder{n}.activating_move == true` |
| Tool offsets | `extruder{n}.extruder_offset` [x, y, z] |
| Tool health | `switch_count`, `retry_count`, `error_count` |
| Filament present | `filament_feed.{channel}.filament_detected` |
| Feed state | `filament_feed.{channel}.channel_state` |
| Feed error | `filament_feed.{channel}.channel_error` |

#### Operations

| Operation | Gcode |
|-----------|-------|
| Change to tool N | `T{N}` |
| Load filament (slot N) | `AUTO_FEEDING` (parameters TBD from macro source) |
| Unload filament (slot N) | `INNER_FILAMENT_UNLOAD` (parameters TBD) |
| Clean nozzle | `ROUGHLY_CLEAN_NOZZLE` or `FINELY_CLEAN_NOZZLE_STAGE_1` |

### ToolState Integration

`ToolState::init_tools()` gets a new Snapmaker-specific branch (third path alongside existing toolchanger and multi-extruder paths):

- When `AmsType::SNAPMAKER` backend exists → create 4 `ToolInfo` entries
- Each entry: index 0-3, name "T0"-"T3", extruder binding
- Fan mapping: U1 uses `fan` (e0 part cooling), `fan_generic e1_fan`, `fan_generic e2_fan`, `fan_generic e3_fan` — discovered generically during object parsing, mapped to tools by index
- `backend_index` points to `AmsBackendSnapmaker`

`ToolState::update_from_status()` gets a new branch:

- Parse `extruder{n}.state` → active/parked
- Parse `extruder{n}.park_pin` / `active_pin` → `DetectState`
- Parse `extruder{n}.activating_move` → tool change in progress
- Parse `extruder{n}.extruder_offset` → gcode offsets

### Detection Update

In `include/printer_discovery.h`:
- `parse_objects()`: Add `has_snapmaker_` flag, set when `filament_detect` object is found
- `finalize_detection()`: When `has_snapmaker_` is true, set `AmsType::SNAPMAKER` — **must take priority** over generic `toolchanger` detection (see "Detection Priority" section above)

### New Enum Value

In `ams_types.h`, add after `CFS = 6`:
```cpp
SNAPMAKER = 7,  ///< Snapmaker U1 SnapSwap toolchanger
```

Update helper functions:
- `ams_type_to_string()`: Add `case AmsType::SNAPMAKER: return "Snapmaker";`
- `ams_type_from_string()`: Add `"snapmaker"` / `"Snapmaker"` / `"snapswap"` mapping
- `is_tool_changer()`: Return true for `SNAPMAKER` (each slot is a complete toolhead)
- `is_filament_system()`: Also return true for `SNAPMAKER` (RFID provides filament data per slot)

The U1 is a **hybrid** — it is both a tool changer (4 independent toolheads) and a filament system (RFID tag data per channel).

### Detection Priority: toolchanger Object Conflict

The U1 exposes an **empty** `toolchanger` object (`{}`). The existing detection logic will set `has_tool_changer_ = true` when it sees this object, potentially misdetecting the U1 as `AmsType::TOOL_CHANGER`.

Resolution: In `finalize_detection()` in `printer_discovery.h`, check for `filament_detect` **before** the generic `toolchanger` path. When `filament_detect` is present, set `AmsType::SNAPMAKER` and skip the generic toolchanger path.

---

## Phase 3: RFID Filament Integration

### SlotInfo Population

Map `filament_detect.info[n]` to `SlotInfo` for each physical slot:

| SlotInfo field | Source | Transform |
|----------------|--------|-----------|
| `material` | `MAIN_TYPE` | Direct string (PLA, PETG, ABS, etc.) |
| `color_rgb` | `ARGB_COLOR` | Extract RGB: `(argb & 0x00FFFFFF)` |
| `color_name` | `SUB_TYPE` | Use as color hint (SnapSpeed, Silk, HF, etc.) |
| `nozzle_temp_min` | `HOTEND_MIN_TEMP` | Direct int |
| `nozzle_temp_max` | `HOTEND_MAX_TEMP` | Direct int |
| `bed_temp` | `BED_TEMP` | Direct int |
| `total_weight_g` | `WEIGHT` | Direct float (grams) |
| `brand` | `MANUFACTURER` | Direct string (Polymaker, etc.), fallback to `VENDOR` |
| `status` | `filament_detect.state[n]` | 0 → `SlotStatus::AVAILABLE`, non-zero → `SlotStatus::EMPTY` |

Fields from RFID **not stored in SlotInfo** (dropped or logged):
- `DIAMETER` (175 = 1.75mm — universal, not per-slot relevant)
- `OFFICIAL` (Snapmaker-specific flag, no SlotInfo equivalent)
- `FIRST_LAYER_TEMP` / `OTHER_LAYER_TEMP` (slicer concerns, not UI state)
- `SKU`, `CARD_UID`, `MF_DATE`, `RSA_KEY_VERSION` (metadata, not displayed)

When no RFID tag is detected, the slot shows as empty with unknown filament.

### Virtual Slot Remapping

The `print_task_config.extruder_map_table` maps 32 virtual slots to 4 physical extruders. The existing slot/tool edit dialog supports remapping, so we expose this:

- Show populated virtual slots (where `extruders_used[n]` or map entry differs from default)
- Remapping writes update the `extruder_map_table` — exact gcode/API mechanism TBD (needs investigation of Snapmaker's Moonraker fork for the setter endpoint)

### filament_feed State

Per-channel feed state from `filament_feed left/right`:

| channel_state | UI Meaning |
|---------------|-----------|
| `"load_finish"` | Filament loaded, ready |
| `"unloading"` | Unload in progress |
| `"loading"` | Load in progress |
| Other TBD | Error states from `channel_error` |

---

## Phase 4: Power-Loss Recovery

Snapmaker's Klipper fork adds a custom brownout-detect + continuous-save mechanism (`[power_loss_check]` extra, patched `virtual_sdcard.py`) that PAXX Extended Firmware inherits unchanged — it's part of Snapmaker's fork, not the stock GUI PAXX replaces. On brownout, the MCU latches stepper positions to its own flash while `virtual_sdcard` snapshots print progress to JSON on disk; on the next boot, Klipper validates the JSON against MCU flash and exposes the result as a standard Moonraker object field. No custom endpoint or plugin is involved — the whole feature is reachable through the objects HelixScreen already subscribes to on Snapmaker.

Full device investigation: `.claude/scratchpad/2026-07-15-u1-power-loss-recovery-findings.md` (bench U1, 192.168.30.103, software_version 0.9.0).

### Detect

`virtual_sdcard.pl_env_valid` (bool) — `true` only after the post-boot MCU/JSON validation succeeds on an idle printer; resets to `false` on every processed line during a normal print. HelixScreen subscribes to it alongside the other Snapmaker `virtual_sdcard` fields already tracked (`file_path`, `progress`, `file_position`, `file_size`) and stores it as the `pl_env_valid` subject on `PrinterPrintState`.

### Offer

`PlrOfferController` (owned by `SubjectInitializer`) watches `pl_env_valid` and connection state and shows a one-shot Resume/Discard prompt when all of: `pl_env_valid` is true, the printer is idle, the connected backend is Snapmaker, the offer hasn't already fired this connection, and the setup wizard isn't active. The latch re-arms on a CONNECTED → not-CONNECTED transition so a disconnect/reconnect cycle offers again. Decision logic lives in the pure `plr_should_offer()` / `plr_should_rearm()` helpers (`include/plr_offer.h`) — no LVGL or threading in the predicate itself.

### Resume / Discard

| Action | Gcode | Firmware guard | Coded error on guard failure |
|--------|-------|-----------------|-------------------------------|
| Resume | `SDCARD_PRINT_PL_RESTORE` | `machine_state_manager.main_state` must be IDLE (0) | `0001-0531-0000-0005` |
| Discard | `SDCARD_PRINT_PL_CLEAR_ENV` | Refused while PRINTING | `0001-0531-0000-0006` |

Restore reheats bed/extruder, homes X/Y, reconstructs Z from MCU-flash stepper data (not the JSON), restores PA/flow/speed/fan state, and resumes the file from its saved position. Once the print transitions to a normal PRINTING state, the existing #1099 auto-navigation (`print_start_navigation`) takes over and pushes the print-status overlay — PLR does not need its own progress UI.

### Error surfacing

Firmware errors on both gcodes arrive as a script-error message containing embedded JSON, e.g. `{"coded":"0001-0531-0000-0005","msg":"...","action":"none"}`. `snapmaker_extract_coded_msg()` (`src/printer/snapmaker_resume.cpp`) pulls the `coded` field out for display; the toast falls back to the raw message if the JSON doesn't parse. Dismissing the prompt via backdrop tap leaves the recovery environment untouched (stock behavior — it keeps offering next connect until Resume or Discard is chosen explicitly).

### Out of scope for this phase

- `machine_state_manager.action_code` progress states (e.g. `PRINT_PL_RESTORE=128`, `PRINT_RESUMING=130`) during the restore sequence itself — not surfaced as a distinct "recovering…" UI state; the modal simply closes and normal print-status navigation picks up once printing resumes.
- Editing/inspecting the saved print state (layer, Z, flow) before deciding — the prompt is binary (resume as-is, or discard).

---

## Files

### New Files

| File | Purpose |
|------|---------|
| `include/ams_backend_snapmaker.h` | Backend header |
| `src/printer/ams_backend_snapmaker.cpp` | Backend implementation |
| `tests/unit/test_ams_backend_snapmaker.cpp` | Unit tests |
| `include/plr_offer.h`, `src/printer/plr_offer.cpp` | Pure `plr_should_offer()` / `plr_should_rearm()` decision helpers |
| `include/plr_offer_controller.h`, `src/ui/ui_plr_offer_controller.cpp` | `PlrOfferController` — owns the pl_env_valid/connection observers |
| `include/snapmaker_resume.h`, `src/printer/snapmaker_resume.cpp` | `snapmaker_extract_coded_msg()` JSON-coded error parser |
| `src/ui/ui_plr_prompt.h`, `src/ui/ui_plr_prompt.cpp` | Resume/Discard modal + gcode dispatch |
| `tests/unit/test_plr_offer.cpp`, `test_plr_state.cpp`, `test_plr_error.cpp`, `test_plr_prompt.cpp` | Unit tests (`[plr]` tag) |

### Modified Files

| File | Change |
|------|--------|
| `include/ams_types.h` | Add `AmsType::SNAPMAKER` enum value, update `ams_type_to_string()`, `ams_type_from_string()`, `is_tool_changer()`, `is_filament_system()` |
| `include/printer_discovery.h` | Add `has_snapmaker_` flag in `parse_objects()`, Snapmaker detection in `finalize_detection()` (priority over `toolchanger`) |
| `src/printer/ams_backend.cpp` | Add `case AmsType::SNAPMAKER` in `create()` factory, add `#include "ams_backend_snapmaker.h"` |
| `src/printer/ams_state.cpp` | Create `AmsBackendSnapmaker` when detected |
| `src/printer/tool_state.cpp` | Add Snapmaker-specific `init_tools()` branch (4 tools from extruder objects) and `update_from_status()` branch (parse `state`, `park_pin`, `active_pin`, `activating_move`, `extruder_offset`) |
| `src/api/moonraker_discovery_sequence.cpp` | Add `filament_detect`, `filament_feed left/right`, `print_task_config`, `machine_state_manager` to object subscriptions in `complete_discovery_subscription()`; also subscribe `virtual_sdcard.pl_env_valid` |
| `include/printer_print_state.h`, `src/printer/printer_print_state.cpp` | Add `pl_env_valid` subject + `pl_recovery_file()` |
| `include/printer_state.h` | Expose `get_pl_env_valid_subject()` |
| `include/subject_initializer.h`, `src/application/subject_initializer.cpp` | Construct/own `PlrOfferController` |

### Existing Infrastructure (No Changes Needed)

- Tool switcher widget — works with ToolState subjects
- Temperature display — works with multi-extruder
- Gcode parser — already handles T0-T3 commands
- Slot/tool edit dialog — works with AMS backend interface
- Filament panel — works with SlotInfo data

---

## Testing Strategy

### Unit Tests (test_ams_backend_snapmaker.cpp)

- `AmsType::SNAPMAKER` enum: `is_tool_changer()` returns true, `is_filament_system()` returns true
- `ams_type_to_string(SNAPMAKER)` returns "Snapmaker", `ams_type_from_string("snapmaker")` returns SNAPMAKER
- Empty `toolchanger` object does NOT trigger generic TOOL_CHANGER detection when `filament_detect` present (regression)
- Parse extruder status JSON with Snapmaker-custom fields (`state`, `park_pin`, `active_pin`, `activating_move`, `extruder_offset`, `switch_count`)
- Map `state`/`park_pin`/`active_pin` to tool state correctly
- Parse `filament_detect.info[]` to SlotInfo with correct field mapping
- Parse `filament_feed` left/right to per-channel state
- Handle missing/empty RFID data gracefully (no tag → `SlotStatus::EMPTY`)
- Parse `extruder_map_table` virtual-to-physical mapping
- Tool change request generates correct `T{n}` gcode
- Active tool detection via `state` field + `toolhead.extruder` cross-check

### Integration Testing (on hardware)

- Deploy to U1, verify printer detection
- Verify 4 tools shown with correct temperatures
- Verify RFID filament data displayed (material, color)
- Verify tool switching via UI sends correct gcode
- Verify feed state updates in real-time

---

## Out of Scope

- Exception manager (`exception_manager` — error/warning state, monitor later)
- Air purifier control (`purifier` object)
- Defect detection settings (`defect_detection`)
- XYZ offset calibration UI (`XYZ_OFFSET_CALIBRATE` macros)
- Camera integration
- Snapmaker Cloud connectivity
- `filament_parameters` material database (Snapmaker's own param DB)

These are potential future enhancements but not part of this initial integration.

---

## References

- [Snapmaker/u1-klipper](https://github.com/Snapmaker/u1-klipper) — Open source Klipper fork
- [Snapmaker/u1-moonraker](https://github.com/Snapmaker/u1-moonraker) — Open source Moonraker fork
- [paxx12/SnapmakerU1-Extended-Firmware](https://github.com/paxx12/SnapmakerU1-Extended-Firmware) — SSH access
- [Toolchanger Research](../printer-research/SNAPMAKER_U1_RESEARCH.md) — API analysis
- [U1 Support Doc](../printers/SNAPMAKER_U1_SUPPORT.md) — Build/deploy/hardware details
- [Tool Abstraction](../TOOL_ABSTRACTION.md) — ToolState architecture
- [Filament Management](../FILAMENT_MANAGEMENT.md) — AMS backend pattern
- `.claude/scratchpad/2026-07-15-u1-power-loss-recovery-findings.md` — power-loss recovery device investigation (bench U1, firmware source paths, gotchas)
