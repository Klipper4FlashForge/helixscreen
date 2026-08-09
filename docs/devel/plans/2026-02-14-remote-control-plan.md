# Remote Control System for HelixScreen (`helixctl`)

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

## Context

HelixScreen currently has basic CLI arguments (`-p motion`, `--screenshot`) for navigating to panels at startup, but no way to control a **running** instance. For AMS debugging — which requires manipulating filament sensor states, loading/unloading sequences, multi-lane configurations, and error conditions — we need a proper remote control interface. This also enables automated screenshot workflows and regression testing.

## Design Summary

- **Transport**: Unix domain socket with JSON-RPC 2.0 protocol
- **Server**: Background thread in HelixScreen, auto-starts in `--test` mode, opt-in via `--remote` otherwise
- **Client**: Standalone `helixctl` binary — one-shot CLI mode AND interactive REPL mode
- **Scripting**: Bash scripts calling `helixctl` one command per invocation — no built-in script runner
- **Socket path**: `$XDG_RUNTIME_DIR/helixscreen-control.sock` → `/tmp/helixscreen-control.sock` → `--remote-socket` override

## Architecture

```
helixctl (CLI/REPL)  ──Unix socket──▶  RemoteControlServer (bg thread)
                                              │
                                        ui_queue_update()
                                              │
                                              ▼
                                        LVGL main thread
                                        (NavigationManager, subjects, widgets)
```

Key pattern: Socket thread receives JSON-RPC request → posts work to UI thread via `ui_queue_update()` → blocks on `std::promise<json>` → UI thread fulfills promise → socket thread sends response.

---

## Phase 1: Socket Server + Navigation + Screenshots ✅ COMPLETE

### New Files

| File | Purpose |
|------|---------|
| `include/remote_control_server.h` | `RemoteControlServer` singleton class declaration |
| `src/remote/remote_control_server.cpp` | Server: accept loop, JSON-RPC dispatch, command handlers |
| `tools/helixctl.cpp` | CLI tool: parse args → JSON-RPC → socket → print result |
| `mk/helixctl.mk` | Build rules for helixctl (standalone, no LVGL deps) |

### Modified Files

| File | Change |
|------|--------|
| `include/cli_args.h` | Add `--remote`, `--remote-socket <path>` flags |
| `src/system/cli_args.cpp` | Parse new flags, resolve socket path |
| `src/application/application.cpp` | Start/stop `RemoteControlServer` based on flags |
| `mk/tools.mk` | Add `helixctl` to `tools` phony target |
| `Makefile` | Include `mk/helixctl.mk` |

### Phase 1 Commands

| helixctl command | JSON-RPC method | What it does |
|---|---|---|
| `helixctl navigate <panel>` | `navigate` | Switch panel via NavigationManager |
| `helixctl go_back` | `go_back` | Pop current overlay |
| `helixctl list_panels` | `list_panels` | Return list of valid panel names |
| `helixctl current` | `get_current` | Return current panel |
| `helixctl screenshot` | `screenshot` | Trigger `helix::save_screenshot()` |
| `helixctl status` | `status` | Return panel, connection state, klippy state |
| `helixctl ping` | `ping` | Health check, returns `"pong"` |

---

## Phase 2: Subject Get/Set + Wait-For

### Modified Files

| File | Change |
|------|--------|
| `include/subject_debug_registry.h` | Add `lookup_by_name()` and `list_all()` methods + reverse map |
| `src/system/subject_debug_registry.cpp` | Implement reverse name→pointer lookup |
| `src/remote/remote_control_server.cpp` | Add get/set/list/wait_for handlers |
| `tools/helixctl.cpp` | Add get/set/list_subjects/wait_for commands |

### SubjectDebugRegistry Extension

Add `std::unordered_map<std::string, lv_subject_t*> name_to_subject_` populated in existing `register_subject()`. Add:
- `lv_subject_t* lookup_by_name(const std::string& name)` — returns pointer or nullptr
- `std::vector<std::pair<std::string, SubjectDebugInfo>> list_all()` — for enumeration

### Phase 2 Commands

| helixctl command | What it does |
|---|---|
| `helixctl get <subject>` | Read current value of named subject (int or string) |
| `helixctl set <subject> <value>` | Set subject value on UI thread |
| `helixctl list_subjects` | List all registered subjects with types |
| `helixctl wait_for <subject> <value> [--timeout N]` | Block until subject matches value or timeout (default 30s) |

### wait_for Implementation

1. Post observer creation to UI thread via `ui_queue_update()`
2. Check current value immediately (might already match)
3. If no match, LVGL observer callback signals a `std::condition_variable`
4. Socket thread waits on CV with timeout
5. On match or timeout, post observer removal to UI thread
6. Return `{"matched": true}` or error with timeout

---

## Phase 3: Widget Interaction + Mock Scenarios

### New Files

| File | Purpose |
|------|---------|
| `src/remote/mock_scenarios.h` | Scenario registry: name → lambda that sets up mock state |
| `src/remote/mock_scenarios.cpp` | Built-in scenarios (printing, error, afc_loaded, etc.) |

### Modified Files

| File | Change |
|------|--------|
| `src/remote/remote_control_server.cpp` | Add click/set_value/scenario handlers |
| `tools/helixctl.cpp` | Add click/set_value/scenario/list_scenarios commands |

### Phase 3 Commands

| helixctl command | What it does |
|---|---|
| `helixctl click <widget_name>` | Find widget by name via `lv_obj_find_by_name()`, send `LV_EVENT_CLICKED` |
| `helixctl set_value <widget_name> <value>` | Set slider/switch/textarea value on named widget |
| `helixctl scenario <name>` | Apply named mock scenario (sets multiple subjects/state) |
| `helixctl list_scenarios` | List available scenarios with descriptions |

### Initial Scenarios

- `idle` — default idle state
- `printing` — mid-print with temps, progress, filename
- `paused` — paused mid-print
- `error` — klippy error state
- `disconnected` — no printer connection
- `ams_hh_4gate` — Happy Hare 4-gate setup with filament loaded
- `ams_afc_8lane` — AFC 8-lane with mixed sensor states
- `ams_loading` — AMS mid-load operation
- `ams_error` — AMS filament jam error

---

## Phase 4: Interactive REPL Mode

### Overview

When `helixctl` is run with no arguments, it enters an interactive REPL mode with a persistent socket connection. Commands can be entered at a prompt, and the tool supports navigating into "sub-sections" for grouped functionality.

### Design

```
$ helixctl
Connected to helix-screen (home panel)
helixctl> ping
pong
helixctl> navigate controls
Navigated to controls
helixctl> status
panel: controls, connection: connected, klippy: ready

helixctl> subjects
subjects> list | grep ams
ams_type (INT) = 0
ams_action (INT) = 0
...
subjects> get ams_type
0
subjects> set ams_type 2
OK
subjects> ..
helixctl> scenarios
scenarios> list
idle, printing, paused, error, disconnected, ams_hh_4gate, ...
scenarios> apply ams_afc_8lane
Applied scenario: ams_afc_8lane
scenarios> ..
helixctl> quit
```

### Modified Files

| File | Change |
|------|--------|
| `tools/helixctl.cpp` | Add REPL loop, sub-section routing, persistent connection |

### Key Features

- **Persistent connection**: Single socket connection for the session (no reconnect per command)
- **Sub-sections**: `subjects`, `scenarios`, `widgets` — enter with name, exit with `..` or `back`
- **Implicit commands**: In `subjects>` context, `get x` maps to `helixctl get x`, `list` maps to `helixctl list_subjects`
- **Output piping**: Support `| grep` style filtering for list commands (shell pipe via `popen`)
- **Readline support**: Optional — use `libedit`/`readline` if available, fall back to basic `stdin` line reading
- **Help**: `help` at any prompt shows available commands for current context
- **History**: Command history within session (and optionally persisted to `~/.helixctl_history`)
- **Clean exit**: `quit`, `exit`, or Ctrl+D

### Sub-Section Routing

| Context | Available Commands |
|---------|-------------------|
| `helixctl>` (root) | All Phase 1-3 commands + `subjects`, `scenarios`, `widgets`, `quit` |
| `subjects>` | `list`, `get <name>`, `set <name> <value>`, `wait_for <name> <value>`, `..` |
| `scenarios>` | `list`, `apply <name>`, `..` |
| `widgets>` | `click <name>`, `set_value <name> <value>`, `..` |

### One-Shot Mode (Unchanged)

When arguments are provided, helixctl works exactly as before — single command, print result, exit. This preserves scriptability:

```bash
# Scripting still works
helixctl navigate controls
helixctl screenshot
helixctl set ams_type 2
helixctl wait_for ams_action 1 --timeout 5
```

---

## Verification

### Phase 1 Test ✅
```bash
# Terminal 1: Start with remote control
./build/bin/helix-screen --test --remote

# Terminal 2: Control it
./build/bin/helixctl ping                    # → "pong"
./build/bin/helixctl navigate controls       # UI switches to controls
./build/bin/helixctl screenshot              # Saves screenshot, prints path
./build/bin/helixctl current                 # → {"panel": "controls"}
./build/bin/helixctl go_back                 # Closes overlay
```

### Phase 2 Test
```bash
./build/bin/helixctl list_subjects | grep ams    # Shows all AMS subjects
./build/bin/helixctl get ams_type                 # → 0 (none)
./build/bin/helixctl set ams_type 2               # Set to AFC
./build/bin/helixctl get ams_type                 # → 2
./build/bin/helixctl wait_for ams_action 1 --timeout 5  # Wait for LOADING
```

### Phase 3 Test
```bash
./build/bin/helixctl scenario ams_afc_8lane       # Set up full AFC state
./build/bin/helixctl navigate ams                  # Open AMS panel
./build/bin/helixctl screenshot                    # Capture
./build/bin/helixctl click load_button             # Simulate load click
./build/bin/helixctl wait_for ams_action 1 --timeout 10
./build/bin/helixctl screenshot
```

### Phase 4 Test
```bash
# Interactive session
./build/bin/helixctl
helixctl> subjects
subjects> list
[shows all subjects]
subjects> set ams_type 2
OK
subjects> ..
helixctl> navigate ams
Navigated to ams
helixctl> screenshot
Saved: /tmp/ui-screenshot-xxx.bmp
helixctl> quit
```

### Unit Tests

Add tests in `tests/unit/test_remote_control.cpp`:
- JSON-RPC parsing/dispatch
- Socket path resolution logic
- Subject registry lookup_by_name
- Command handler responses (using mock NavigationManager)

---

## Development Approach

- **TDD**: Write failing tests BEFORE implementation for each component
- **Code reviews**: After each phase completion, run review before committing
- **RAII everywhere**: Socket fd ownership, observer guards, promise lifecycle
- **Follow existing patterns**: Match the style in `mk/tools.mk`, `ui_update_queue.h`, `subject_debug_registry.h`
- **Clean C++**: No raw `new`/`delete`, proper error handling, no leaky abstractions
