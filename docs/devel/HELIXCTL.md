# helixctl — Remote Control & UI Driving

`helixctl` drives a running HelixScreen instance over a Unix-domain socket
(JSON-RPC 2.0). It replaces the old `-p`/`--panel` launch flags: instead of
booting the binary directly into a panel or overlay, you boot it once and then
navigate, click, fill fields, toggle switches, scroll, and capture screenshots
from the command line — with the **real widget lifecycle** (`init_subjects` /
`create` / `on_activate` / teardown), not empty shells.

This is the tool the screenshot pipeline uses, and the way to bring up any
panel/overlay/modal for debugging.

## Enabling the server

The control server runs as a background thread inside `helix-screen`.

| How | When it starts |
|-----|----------------|
| `--test` | Auto-enabled (no extra flag needed) |
| `--remote` | Opt-in for a non-test/production build |
| `--remote-socket <path>` | Override the socket path (default below) |

Socket path resolution (both server and client use the same order):
1. `--remote-socket <path>` / `helixctl -s <path>` (explicit)
2. `$XDG_RUNTIME_DIR/helixscreen-control.sock`
3. `/tmp/helixscreen-control.sock`

```bash
# Boot a mock instance with the server up
./build/bin/helix-screen --test --skip-wizard --remote -vv &

# Drive it
./build/bin/helixctl navigate controls
./build/bin/helixctl ls
```

`--skip-wizard` suppresses the first-run wizard so automation lands on the home
panel. (It replaces the old side effect where `-p <panel>` implicitly skipped
the wizard.)

## Commands

Run `helixctl` with no arguments to drop into an interactive REPL (line editing,
history, tab completion) whose prompt is a live breadcrumb of the navigation
stack, e.g. `controls / motion_panel_0 > `.

### Navigation (filesystem metaphor)
| Command | Meaning |
|---------|---------|
| `navigate`, `cd <target>` | Go to a base panel, or click a named widget to descend into an overlay |
| `go_back`, `back`, `cd ..` | Pop the current overlay |
| `current`, `pwd` | Show the current panel + overlay stack |
| `list_panels` | List the registered base panels |
| `wake`, `screensaver` | Reset the idle timer / dismiss the screensaver |

### Introspection & widget interaction
| Command | Meaning |
|---------|---------|
| `ls`, `describe_screen` | List on-screen widgets: name, `path`, type, available actions |
| `click <target>` | Click a widget (also toggles switches/checkboxes) |
| `set_value <target> <v>` | Set a value (slider, switch, dropdown, textarea) |
| `scroll <target> [dx dy]` | Scroll a widget into view, or by a delta |

A **target** is either a widget `name` or an `@path` locator taken from `ls`
(e.g. `@s/15/1/1/2`). Use `@path` when a name is duplicated on screen (reusable
components share names — a settings page can have six `toggle`s).

### Screenshots & sample-data screens
| Command | Meaning |
|---------|---------|
| `screenshot` | Capture a screenshot (`/tmp/ui-screenshot-<timestamp>.bmp`) |
| `demo <name>` | Bring up a screen that can't be reached by navigation in mock mode |

`demo` covers screens that only appear on a real printer event or configured
state, constructed with representative sample data and the real lifecycle:
`preflight-check`, `runout-modal`, `lock-screen`, `print-status`, `print-tune`,
`ams`, `camera`.

### Subjects & scenarios
| Command | Meaning |
|---------|---------|
| `get <subject>` / `set <subject> <value>` | Read / write a bound subject |
| `list_subjects` | List all registered subjects |
| `wait_for <subject> <value> [--timeout N]` | Block until a subject matches |
| `scenario <name>` / `list_scenarios` | Apply / list mock scenarios |

## Bringing up a panel or overlay (replaces `-p`)

| Old | New |
|-----|-----|
| `helix-screen --test -p motion` | boot `--test --remote`, then `helixctl navigate controls; helixctl click btn_motion` |
| `helix-screen --test -p settings` | `helixctl navigate settings` |
| `helix-screen --test -p print-status` | `helixctl demo print-status` |
| `HELIX_SSAO=0 helix-screen --test -p bed-mesh` | `HELIX_SSAO=0 helix-screen --test --remote &` then `helixctl navigate controls; helixctl click btn_bed_mesh` |

Environment variables that used to pair with `-p` (`INPUT_SHAPER_AUTO_START`,
`SCREWS_AUTO_START`, `HELIX_MOCK_DRYER_SPEED`, `HELIX_GCODE_STREAMING`, …) still
apply — set them on the launch, then navigate to the panel with helixctl.

The exact navigation recipe for each documentation screen lives in
`scripts/screenshot-recipes.sh` (the single source of truth), used by both
`scripts/screenshot.sh` (single shot) and `scripts/screenshot-all.sh` (batch).

## Screenshots

`scripts/screenshot.sh` drives helixctl end to end — it boots a fresh instance
on a private socket, runs the recipe for the requested screen, captures, and
tears the instance down:

```bash
./scripts/screenshot.sh helix-screen motion motion --test      # an overlay
./scripts/screenshot.sh helix-screen zoffset zoffset --test     # a calibration screen
./scripts/screenshot.sh helix-screen preflight preflight-check --test  # a sample-data modal
./scripts/screenshot.sh helix-screen tiny-home home --test -s 480x320  # a size variant
```

See `scripts/screenshot-recipes.sh` for every recognized token.

## Architecture

- Server: `src/remote/remote_control_server.cpp` (background thread; JSON-RPC
  dispatch; marshals work to the LVGL main thread via the update queue + a
  promise so widget operations run on the UI thread).
- Client: `tools/helixctl.cpp` (standalone, no LVGL/libhv dependency; bundles
  `lib/linenoise` for the REPL).
- Sample-data screens: `helix::show_demo_overlay()` in
  `src/application/application.cpp`.
