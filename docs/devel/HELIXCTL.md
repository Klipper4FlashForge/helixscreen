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
| `help`, `?` | Print the command list (same as `-h`/`--help`) |
| `current`, `pwd` | Show the current panel + overlay stack |
| `list_panels` | List the registered base panels (the fixed `PanelId` set) |
| `wake`, `screensaver` | Reset the idle timer / dismiss the screensaver |

### Introspection & widget interaction
| Command | Meaning |
|---------|---------|
| `ls`, `describe_screen` `[target]` | List on-screen widgets: name, `path`, `layer`, type, available actions. With a target, list only that widget's subtree (plus the widget itself). The response also carries `topmost_layer` and `active_screen` — compare an entry's `layer` against `topmost_layer` to tell a frontmost widget from one stacked behind it |
| `list_components` | List **every** registered XML component (live registry): panels, overlays, modals, cards, rows — the full introspectable surface |
| `list_callbacks` | List every registered event-callback name (overlay/modal open-handlers, button callbacks). Names only — nothing is fired |
| `click <target>` | Click a widget (also toggles switches/checkboxes) |
| `set_value <target> <v>` | Set a value (slider, switch, dropdown, textarea) |
| `scroll <target> [dx dy]` | Scroll a widget into view, or by a delta |
| `focus <target>` | Focus a widget through its input group. Fires the real `LV_EVENT_FOCUSED`, so a registered textarea raises the on-screen keyboard — `click` does not, and leaves it hidden. Fails if the widget is not in an input group |
| `geom <target> [depth]` | Measured geometry: position, size, declared-vs-computed size, flex/scroll state |
| `get_const [scope] <name>` | Resolve an XML `#const` to the value the renderer actually sees |

### Synthetic pointer — testing gestures

`click` is `lv_obj_send_event(obj, LV_EVENT_CLICKED)`: a widget-level event with no
input device and no coordinates behind it. That is right for "press this button",
but it cannot exercise anything gestural. Code that reads `lv_indev_active()` or
`lv_indev_get_point()` sees nothing, long-press timers never start, and LVGL's
scroll-versus-click arbitration never runs.

These commands drive a second, `ctl`-owned pointer device through **LVGL's real
input pipeline**, so gestures behave exactly as they do under a finger.

| Command | Meaning |
|---------|---------|
| `press <x> <y>` | Put the pointer down at screen coordinates x,y |
| `move <x> <y>` | Move it — a drag while pressed, a hover while released |
| `release [x y]` | Lift it, at x,y if given, otherwise where it currently is |

Each command returns only after LVGL has sampled the device twice, so sequences do
not race the indev timer. Timing that matters to the gesture is yours to control
from the shell:

```bash
# Long-press a key and lift in place
helix-screen ctl press 100 300
sleep 0.6                       # exceed the long-press threshold
helix-screen ctl release

# Long-press, then slide onto the popover above it before lifting
helix-screen ctl press 100 300
sleep 0.6
helix-screen ctl move 100 260
helix-screen ctl release

# Drag to scroll a list, proving a tap does NOT fire mid-scroll
helix-screen ctl press 400 200
helix-screen ctl move 400 160
helix-screen ctl move 400 120
helix-screen ctl release
```

Get coordinates from `geom <target>` — it reports each widget's absolute `x`, `y`,
`w` and `h`, so aim at a rect's centre rather than guessing.

The device is created lazily on the first pointer command and coexists with the
real SDL/evdev pointer; LVGL supports multiple pointer indevs. Instances that never
receive a pointer command never register it.

A **target** is either a widget `name` or a path locator taken from `ls`
(e.g. `@s/15/1/1/2`, or bare `s/15/1/1/2` — the `@` is optional, since widget
names never contain `/`). Use a path when a name is duplicated on screen
(reusable components share names — a settings page can have six `toggle`s).

Name lookup resolves to the **topmost visible** match: hidden subtrees are
skipped, and among what remains the widget in the frontmost overlay wins
(the top layer outranks the active screen; later siblings outrank earlier
ones). Overlays stay in the tree when another is pushed on top of them, so
without this a name present in both would resolve to the one behind — a click
that reports success and does nothing.

Mutating responses echo `path` (the widget actually hit), `handlers` (its
registered event count — `0` means the click cannot do anything), and
`active_screen` (a `panel > overlay > overlay` breadcrumb). Check
`active_screen` first when a command appears to do nothing: a first-run wizard
or an unexpected modal swallows input while every response still reads as
success.

A full-screen `ls` on a settings page runs to hundreds of entries. Scope it once
you know the row you want — `ls row_filament_auto_cooldown` returns that row and
its handful of children, `scope` in the response echoing the subtree root.

**Wildcards.** A target name containing `*` (any run of characters, including
none) or `?` (exactly one) is matched as a glob against every visible named
widget on the active screen and the top layer:

```bash
helix-screen ctl ls 'row_*'          # every settings row, each with its subtree
helix-screen ctl ls '*cooldown*'     # find it without knowing the full name
helix-screen ctl click '*auto_cooldown*'
```

Quote the pattern so your shell doesn't expand it against the filesystem first.

`ls` lists **all** matches — that is the point of it — and reports `scope` as an
array plus a `matched` count when there is more than one. A match nested inside
another match is not a scope of its own: `row_*` on a settings page hits both
each row and the `row_icon` within it, and listing both would emit the icon
twice (once as a scope root, once inside its parent's subtree) and report a
doubled count. Only the outermost matches become scopes; the inner ones still
appear, in their parent's listing. `click`, `set_value`
and `scroll` instead require the pattern to identify **exactly one** widget: on
multiple matches they fail with the candidate names and `@path`s rather than
acting on whichever came first, since driving the UI somewhere unintended is
much worse than an error. Glob matching skips hidden subtrees, same as `ls`.

**Composite rows resolve to the control inside them.** A settings row is a
clickable container wrapping the actual switch/dropdown, so a naive
`click <row>` would fire CLICKED on the container and do nothing visible.
`click` and `set_value` therefore descend to a **value-control** (switch,
checkbox, slider, arc, dropdown, textarea) when the target isn't one itself and
its visible subtree holds exactly one — the response reports `descended_to` with
that child's path. Rows with no value-control (a category row that opens an
overlay) are clicked as-is, so navigation is unaffected. If several candidates
exist the container is clicked and they are listed under `candidates`, so you
can re-issue against a specific `@path`.

#### `geom` — why a widget is the size it is

`ls` tells you a widget exists; `geom` tells you how big it ended up and what it
asked for. The pair is what distinguishes "my widget is missing" from "my widget
is present but computed to zero", which look identical on screen.

```bash
helix-screen ctl geom details_catalog_selector
helix-screen ctl geom details_view 2      # recurse 2 levels into children
```

| Field | Meaning |
|-------|---------|
| `x`, `y` | Absolute screen coordinates (comparable against a screenshot) |
| `w`, `h` | Computed size |
| `content_w`, `content_h` | Inner area, padding excluded |
| `req_w`, `req_h` | The size **as authored**: `"content"`, `"50%"`, or a pixel count |
| `flex_grow` | Flex grow factor |
| `hidden`, `scrollable` | Flag state |
| `scroll` | `top`/`bottom`/`left`/`right` scrollable extents |

`req_*` reports the authored form rather than the raw coord, because LVGL packs
`LV_SIZE_CONTENT` and percentages into the integer — printed raw they surface as
meaningless sentinels. A `req_h` of `content` or a nonzero `flex_grow` sitting
next to a computed `h` of `0` is the signature of a flex child collapsing in a
content-sized parent, which has no free space to distribute.

#### `get_const` — what value the renderer actually resolved

```bash
helix-screen ctl get_const color_swatch_grid grid_width   # scoped lookup
helix-screen ctl get_const space_md                       # globals
helix-screen ctl get_const @color_swatch_grid             # dump every const in a scope
```

A scoped lookup falls back to `globals` when the name is not in the component's
own scope, mirroring how the renderer resolves an unqualified `#const`; the
`scope` field in the reply says which one answered. Consts registered from C++
can silently disagree with what XML resolves — `lv_xml_register_const()` is a
no-op when the name already exists in the scope, so a component's fallback
`<consts>` win unless the C++ side uses `lv_xml_update_const()`. This command
reads the resolved value, so it shows which one is really in effect.

### Screenshots & sample-data screens
| Command | Meaning |
|---------|---------|
| `screenshot [path]` | Capture a screenshot. With no path, a timestamped `.bmp` in the runtime dir; a path ending in `.png` is encoded as PNG (in-app, via lodepng). The response reports the file actually written under `path` |
| `demo <name>` | Bring up a screen that can't be reached by navigation in mock mode |

`demo` covers screens that only appear on a real printer event or configured
state, constructed with representative sample data and the real lifecycle:
`preflight-check`, `runout-modal`, `lock-screen`, `print-status`, `print-tune`,
`ams`, `camera`.

### Diagnostics & lifecycle
| Command | Meaning |
|---------|---------|
| `log [-n N]` | Tail the app's in-memory log ring buffer (default 50 lines). Printed as raw lines, so it pipes to `grep` |
| `shutdown` | Ask the app to exit its main loop (`app_request_quit`), running the normal shutdown path |

`log` reads the same ring buffer the debug bundle's `log_tail` uses — capacity
is `HELIX_LOG_RING_LINES` (default 2000). It means a scripted run can read the
app's own log without redirecting stdout to a file first.

From the one-shot client, `quit` and `exit` are accepted as aliases for
`shutdown`. **In the REPL they are not** — there, `quit`/`exit`/Ctrl-D leave the
REPL and `shutdown` stops the app, which is the only reading that keeps both
meanings available.

### Subjects & scenarios
| Command | Meaning |
|---------|---------|
| `get <subject>` / `set <subject> <value>` | Read / write a bound subject |
| `list_subjects` | List all registered subjects |
| `wait_for <subject> <value> [--timeout N]` | Block until a subject matches |
| `scenario <name>` / `list_scenarios` | Apply / list mock scenarios |

## Interactive REPL

`helix-screen repl` (or `helix-screen ctl` with no command) opens an interactive
session with line editing, persistent history, and Tab completion (over commands,
subject names, panels, and scenarios). It reconnects per command, so it survives
the app restarting mid-session — handy while iterating on XML with hot reload.

The prompt is a **live breadcrumb of the navigation stack**, so you always know
where you are. It uses the filesystem metaphor — `cd` to descend, `cd ..`
(`back`) to pop, `pwd` (`current`) to show the stack, `ls` to list what's here:

```text
$ helix-screen repl
helix-screen control REPL — type 'help' for commands, Tab for completion, Ctrl-D to quit

> cd controls
controls > ls
  navigate  btn_motion btn_nozzle_temp btn_extrusion btn_fan btn_bed_mesh ...
  (42 widgets — `ls` shows all; @path targets any one)
controls > cd btn_motion
controls / motion_panel_0 > set_value jog_distance 10
controls / motion_panel_0 > back
controls > pwd
  panel: controls, overlays: []
controls > quit
```

Every command from the tables above works at the prompt. A handful are
REPL-only:

| Command | Meaning |
|---------|---------|
| `help` | Show the in-REPL command summary |
| `refresh` | Reload the Tab-completion caches (subjects/scenarios/panels) after state changes |
| `quit`, `exit`, `Ctrl-D` | Leave the REPL |

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
