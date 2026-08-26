# Tool offset calibration — per-tool UI mockups

Design mockups for reworking the tool offset calibration overlay from the current
one-shot `CALIBRATE_TOOL_OFFSETS` console-mirror flow into a per-tool list, in the
spirit of klipper-toolchanger's decomposed `TOOL_LOCATE_SENSOR` /
`TOOL_CALIBRATE_TOOL_OFFSET` commands.

The `mock*.xml` files are static layouts (no subjects, throwaway) that were
hot-reloaded over `ui_xml/calibration_tool_offset_panel.xml` in a `--test`
instance to render the `v2_*.png` screenshots at 800x480.

| State | File | Notes |
|-------|------|-------|
| Overview | `v2_1_overview.png` | One row per tool: color chip, index, current X/Y/Z ("--" = never calibrated), per-row Calibrate. Full-width Calibrate All at the bottom. |
| Confirm | `v2_2_confirm.png` | Every start goes through a "remove the build plate" confirmation. |
| Running | `v2_3_running.png` | Same layout; active tool's button becomes a spinner (blue outline), other buttons disabled. Stop is a clean between-steps cancel. |
| Complete | `v2_4_complete.png` | Overview layout with new values; Save Offsets persists via SAVE_CONFIG. |
