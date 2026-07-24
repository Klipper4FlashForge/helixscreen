# helixctl — Remote Control & UI Driving

The helixctl client drives a running HelixScreen instance over JSON-RPC 2.0. It
replaces the old `-p`/`--panel` launch flags: instead of booting the binary
directly into a panel or overlay, you boot it once and then navigate, click,
fill fields, toggle switches, scroll, and capture screenshots from the command
line — with the **real widget lifecycle** (`init_subjects` / `create` /
`on_activate` / teardown), not empty shells.

This is the tool the screenshot pipeline uses, and the way to bring up any
panel/overlay/modal for debugging.

## One binary — `ctl` / `repl` subcommands

There is **no separate `helixctl` binary**. The client is folded into
`helix-screen` and reached via subcommands (they dispatch before any
app/display initialization, so they start instantly and never touch the UI):

```bash
helix-screen ctl <command> [args]     # one-shot command
helix-screen ctl -s <socket> <cmd>    # against an explicit socket
helix-screen repl                     # interactive REPL
helix-screen ctl                      # no command → also drops into the REPL
```

> **Dev/test only.** The entire remote-control subsystem (server, transports,
> and this client) is compiled in **only when `HELIX_ENABLE_REMOTE_CONTROL` is
> defined** — the default for native dev builds. Release/cross builds for
> shipped devices exclude it entirely (no code, no overhead). A plain `make -j`
> builds it; to put it in a device dev image, build with
> `make PLATFORM_TARGET=<t> ENABLE_REMOTE_CONTROL=yes`.

## Enabling the server

The control server runs as a background thread inside `helix-screen`.

| How | When it starts |
|-----|----------------|
| `--test` | Auto-enabled (no extra flag needed) |
| `--remote` | Opt-in for a non-test build |
| `--remote-socket <path>` | Override the socket path (default below); implies `--remote` |

```bash
# Boot a mock instance with the server up
./build/bin/helix-screen --test --skip-wizard --remote -vv &

# Drive it
./build/bin/helix-screen ctl navigate controls
./build/bin/helix-screen ctl ls
```

`--skip-wizard` suppresses the first-run wizard so automation lands on the home
panel. (It replaces the old side effect where `-p <panel>` implicitly skipped
the wizard.)

## Transports (socket | HTTP)

The server speaks JSON-RPC over one of two transports, selectable at runtime:

| Flag | Default | Meaning |
|------|---------|---------|
| `--remote-transport socket\|http` | `socket` | Which transport to bind |
| `--remote-socket <path>` | see below | Unix-socket path (socket transport) |
| `--remote-http-bind <host>` | `127.0.0.1` | HTTP bind address (implies http) |
| `--remote-http-port <n>` | `7130` | HTTP TCP port (implies http) |

**Unix socket** (default) — local, owner-only (0600), no network exposure. The
`ctl`/`repl` client speaks this. Socket path resolution (client and server use
the same order):
1. `--remote-socket <path>` / `helix-screen ctl -s <path>` (explicit)
2. `$XDG_RUNTIME_DIR/helixscreen-control.sock`
3. `/tmp/helixscreen-control.sock`

**HTTP/TCP** — a minimal `POST /rpc` JSON-RPC endpoint. Binds loopback by
default; LAN exposure is opt-in via `--remote-http-bind`. This is the base for
the post-1.0 web config UI (the same embedded server will serve it).

```bash
./build/bin/helix-screen --test --remote --remote-transport http --remote-http-port 7130 &
curl -s -X POST http://127.0.0.1:7130/rpc \
  -d '{"jsonrpc":"2.0","method":"ping","id":1}'
# {"id":1,"jsonrpc":"2.0","result":"pong"}
```

## Commands

`helix-screen repl` (or `helix-screen ctl` with no command) drops into an
interactive REPL — line editing, history, tab completion — whose prompt is a
live breadcrumb of the navigation stack, e.g. `controls / motion_panel_0 > `.

### Navigation (filesystem metaphor)
| Command | Meaning |
|---------|---------|
| `navigate`, `cd <target>` | Go to a base panel, or click a named widget to descend into an overlay |
| `go_back`, `back`, `cd ..` | Pop the current overlay |
| `current`, `pwd` | Show the current panel + overlay stack |
| `list_panels` | List the registered base panels (the fixed `PanelId` set) |
| `wake`, `screensaver` | Reset the idle timer / dismiss the screensaver |

### Introspection & widget interaction
| Command | Meaning |
|---------|---------|
| `ls`, `describe_screen` | List on-screen widgets: name, `path`, type, available actions |
| `list_components` | List **every** registered XML component (live registry): panels, overlays, modals, cards, rows — the full introspectable surface |
| `list_callbacks` | List every registered event-callback name (overlay/modal open-handlers, button callbacks). Names only — nothing is fired |
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
| `helix-screen --test -p motion` | boot `--test --remote`, then `helix-screen ctl navigate controls; helix-screen ctl click btn_motion` |
| `helix-screen --test -p settings` | `helix-screen ctl navigate settings` |
| `helix-screen --test -p print-status` | `helix-screen ctl demo print-status` |
| `HELIX_SSAO=0 helix-screen --test -p bed-mesh` | `HELIX_SSAO=0 helix-screen --test --remote &` then `helix-screen ctl navigate controls; helix-screen ctl click btn_bed_mesh` |

Environment variables that used to pair with `-p` (`INPUT_SHAPER_AUTO_START`,
`SCREWS_AUTO_START`, `HELIX_MOCK_DRYER_SPEED`, `HELIX_GCODE_STREAMING`, …) still
apply — set them on the launch, then navigate to the panel with the client.

The exact navigation recipe for each documentation screen lives in
`scripts/screenshot-recipes.sh` (the single source of truth), used by both
`scripts/screenshot.sh` (single shot) and `scripts/screenshot-all.sh` (batch).

## Screenshots

`scripts/screenshot.sh` drives the client end to end — it boots a fresh
instance on a private socket, runs the recipe for the requested screen,
captures, and tears the instance down:

```bash
./scripts/screenshot.sh helix-screen motion motion --test      # an overlay
./scripts/screenshot.sh helix-screen zoffset zoffset --test     # a calibration screen
./scripts/screenshot.sh helix-screen preflight preflight-check --test  # a sample-data modal
./scripts/screenshot.sh helix-screen tiny-home home --test -s 480x320  # a size variant
```

See `scripts/screenshot-recipes.sh` for every recognized token.

## Architecture

- **Server** — `src/remote/remote_control_server.cpp`: transport-agnostic
  JSON-RPC dispatch. Runs handlers on the LVGL main thread via the update queue
  + a promise so widget operations execute on the UI thread.
- **Transports** — `IRemoteTransport` (`include/remote_transport.h`) with a
  shared accept loop in `src/remote/socket_server_base.cpp`; two backends:
  `unix_socket_transport.cpp` (default) and `http_transport.cpp` (minimal
  self-contained HTTP/1.1, no libhv HttpServer dependency).
- **Client** — `src/remote/remote_client.cpp` (`helix::remote_client_main`,
  dispatched from `src/main.cpp` on the `ctl`/`repl` subcommand). Bundles
  `lib/linenoise` for the REPL. No standalone binary.
- **Sample-data screens** — `helix::show_demo_overlay()` in
  `src/application/application.cpp`.
- **Compile gate** — the whole subsystem is filtered out of the build unless
  `ENABLE_REMOTE_CONTROL=yes` (`Makefile`); the `HELIX_ENABLE_REMOTE_CONTROL`
  define guards the server start/stop, the demo bringup, and the `ctl`/`repl`
  dispatch in `main.cpp`.
