# Welcome to HelixScreen

HelixScreen is an LVGL 9.5 touchscreen UI for Klipper 3D printers. This guide gets
a new contributor from a fresh checkout to a first change.

## Prerequisites

- A working C++ toolchain and `make` (the build is a pure Makefile — no CMake/Ninja).
- The repository cloned locally: https://github.com/prestonbrown/helixscreen

## Build & Run

Before compiling, check for existing build processes (`pgrep -f 'make|c\+\+'`) —
concurrent compilations thrash the machine.

```bash
make -j                              # Build ONLY the program binary (not tests)
./build/bin/helix-screen --test -vv  # Run against a mock printer with DEBUG logs
```

Always run with verbosity when debugging: `-v` = INFO, `-vv` = DEBUG, `-vvv` = TRACE
(default is WARN). Debugging without at least `-vv` wastes time.

### Tests

```bash
make test                            # Build tests only (does NOT run them)
make test-run                        # Build AND run tests in parallel
./build/bin/helix-tests "[tag]"      # Run a specific test tag
```

Note: `make -j` builds only `helix-screen`, not the tests. Run `make test` before
`./build/bin/helix-tests` or you will be testing a stale binary.

### XML changes need no rebuild

`ui_xml/*.xml` is loaded at runtime. Edit the XML, then relaunch the binary to see
the change — no `make` needed. For live editing without restarting, set
`HELIX_HOT_RELOAD=1` and the running app re-registers components within ~500ms of a save.

### Screenshots

Press `S` in the UI, or run `./scripts/screenshot.sh helix-screen output-name [panel]`.

## Workflow Tips

- **Read `CLAUDE.md` before touching UI code.** The declarative-UI rules (no
  `lv_obj_add_event_cb`, no imperative visibility, design tokens instead of hardcoded
  colors/spacing, etc.) are strict and easy to violate if you're coming from
  imperative LVGL.
- **Use worktrees when work might collide.** If your task touches a lot of files or
  you want to keep compiling in parallel with another branch, spin up a worktree:
  `scripts/setup-worktree.sh feature/my-branch`. Not every task needs one, but for
  anything sprawling it's the smart default.
- **Start fresh per task.** Keep unrelated bugs/features in separate sessions rather
  than letting one session sprawl across a whole day of work.

## Where to Look Next

- `CLAUDE.md` (repo root) — the always-load rules: build commands, declarative-UI
  rules, threading/lifecycle safety, design tokens, where things live.
- `docs/devel/CLAUDE.md` — full developer-doc index by topic.
- `docs/devel/UI_CONTRIBUTOR_GUIDE.md` — start here for UI/layout work: breakpoints,
  tokens, colors, widgets, layout overrides.
- `docs/devel/YOUR_FIRST_CONTRIBUTION.md` — annotated walkthrough of a real settings
  overlay, plus a pattern tour for bigger features.
- `docs/devel/BUILD_SYSTEM.md` — Makefile internals, make targets, cross-compilation.

## A Good First Task

Browse the [open issues](https://github.com/prestonbrown/helixscreen/issues) and pick
one that looks approachable. Debug/fix work is a fast way to get familiar with the
codebase and its patterns — no specific ticket required, just find something you can
reproduce and investigate.
