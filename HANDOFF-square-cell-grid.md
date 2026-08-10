# Handoff: finish the square-cell home-grid plan

Paste everything below the line into a fresh Claude Code session on thelio.

---

Continue the HelixScreen square-cell home-grid rework. Work on this machine (thelio).

## SETUP

    cd /home/pbrown/Code/Printing/helixscreen/.worktrees/grid-cell-metrics
    git log --oneline -1        # expect 0f145b14c or later on fix/grid-cell-metrics

The worktree already exists and is built. `lib/` is symlinked into the main tree, so
`git status` permanently shows ` T lib/helix-xml` — that is expected, never stage it, and
never run a whole-tree `git add`.

## READ THESE FIRST, IN THIS ORDER

1. `.superpowers/sdd/2026-08-08-square-cell-grid/progress.md` — **the ledger. It is the
   authority.** It carries measured state, every correction to the plan, and the deferred
   findings. Where the ledger and the plan disagree, the ledger wins.
2. `docs/superpowers/plans/2026-08-08-square-cell-grid.md` — the 14-task plan. Its "Owner
   rulings" section near the top has SEVEN binding rulings. **Its reference data table at
   lines 138-147 is STALE** (see below).
3. The evidence reports beside the ledger: `contentbox-scope-report.md`,
   `pstat-reliability-audit.md`, `task-7-report.md`, `task-14-report.md`,
   `sizing-fix-report.md`, `moonraker-deadlock-report.md`, `async-coalesce-report.md`.

## FIRST THING YOU MUST DO: MERGE MAIN. IT WILL CONFLICT.

The branch is **340 commits behind main** and 24 ahead. Seven files were changed on both
sides:

    mk/patches.mk
    src/ui/grid_edit_mode.cpp
    src/ui/panel_widgets/print_status_widget.cpp        <-- the hard one
    src/ui/panel_widgets/print_status_widget.h          <-- the hard one
    tests/helix_test_fixture.cpp
    tests/unit/test_camera_widget.cpp
    tests/unit/test_panel_widget_temp_graph.cpp

`print_status_widget.*` is genuinely contested. This branch carries three reliability fixes
to it (a790e0517 thumbnail deferral, f1fad309e, 4ef659870 formatter re-creation + observer
lifetimes). Main separately received a print-status DRY pass (merge 3e6be635f) that rewrote
`DetailedFormatter`'s FORMATTING methods and deleted its `progress_pct`. The regions are
mostly disjoint — reliability work is in the thumbnail path and observer registration,
DRY work is in the string builders — but `DetailedFormatter` is touched by both. Resolve by
KEEPING BOTH: the lifetime/deferral changes and the canonical-formatter calls are
independent improvements. If a hunk looks like it forces a choice, you have mis-resolved it.

Worktree merge procedure (git cannot scan symlinked submodules):

    ../../scripts/setup-worktree.sh --unlink
    git merge main                    # resolve conflicts now
    ../../scripts/setup-worktree.sh --relink     # REQUIRED before building or committing

Then rebuild and re-run the grid tags before doing anything else.

## WHAT IS DONE

Tasks 1-7 and Task 14 are complete, plus a sizing correction that is not in the plan's
task list. In branch order:

    34d69feac  T2  resize hit band scales with the widget
    79aac6fec  T3  step-aware snapping, snap_step_for(), TRACKS_PER_CELL
    7e63212d2  T4  two-tier dot lattice
    e329906ba  T5  THE SQUARE-CELL MODEL (GRID_DIMS / TARGET_CELL_* / MIN_PORTRAIT_COLS gone)
    109abb950  T6  row tracks come from the grid, /ui/cached_grid removed
    decbb7f6b  T7  registry spans in half-cell tracks
    063080c29  T7  #1216 premises derived from the live grid
    99681c0d3  --  SIZING FIX: content box + nearest-cell rounding
    fbedb74eb  --  grid tests driven from 16 MEASURED content boxes
    3838654ea  T14 content-fit sweep
    (T1 5fada4f30 is older, from before this branch's current range)

Adjacent work that landed here and is NOT grid work: 462351f1c (libhv staleness build fix),
a790e0517 / f1fad309e / 4ef659870 (print-status reliability), 940ba66ae / 0f145b14c
(CoalescedTimer::schedule_once + two call sites).

## WHAT REMAINS: TASKS 8-13

**Task 8 (`default_layout.json` in the new units) is next**, and two things about it changed:

1. **DO NOT author it from the plan's reference table at lines 138-147.** That table lists
   Medium 13 cols, Large 17, XLarge 17, Portrait 13 rows. The shipped code gives **12, 16,
   16, 12** — the table predates ruling 6's floor-to-even AND the sizing fix. The authoritative
   numbers are the 16 measured geometries pinned in `tests/unit/test_grid_square_cells.cpp`.
2. **Author it knowing that widgets at their authored minimum frequently already clip** —
   Task 14 measured 128 of 296 widget/geometry pairs failing. Anchors that place widgets at
   minimum are placing them somewhere broken. See the clipping section below.

Tasks 9-13 follow the plan as written, with one hard gate:

**TASK 11 IS THE POINT OF NO RETURN.** Once a config is stamped v22, saved col/row/span are
gone and reverting the code will not bring them back. Flag it to Preston before running it.

Re-run Task 14's `[content_fits]` sweep at the end of Task 8 and Task 9 — both re-author
what sizes widgets actually get.

## BLOCKING FOLLOW-UP, BEFORE TASK 9

**Merged card backgrounds are dead.** `panel_widget_manager.cpp` gates the flood fill on
`colspan == 1 && rowspan == 1`; nothing is 1x1 any more since the minimum authored span is
2 tracks. The one-line `== TRACKS_PER_CELL` fix is WRONG — the BFS adjacency and
maximal-rectangle decomposition below it are written in cell coordinates that are now track
coordinates, so it would draw a quarter-size card per widget, which is worse than the clean
absence. ~80 lines, no existing test. Needs its own task.

## KNOWN RED, AND WHOSE IT IS

- `tests/unit/test_grid_edit_drag_path.cpp:285-286` (`4 == 3`, `2 == 1`) — snap granularity
  changed by TRACKS_PER_CELL 1->2. Confirmed pre-existing and unmoved by the sizing fix.
  **Still unowned; no task in the plan claims it.** Decide whether the expectation or the
  behaviour is wrong.
- On MAIN, not this branch: an unfiltered single-process `./build/bin/helix-tests` run
  SIGSEGVs at `test_clock_widget.cpp:157` around case 2762 of 11294, **and still prints a
  totals line** ("2762 test cases, 2761 passed"). Sharding hides it so `make test-run` is
  green. Unowned.

## THE 128 CLIPPING FINDINGS — DECISIONS, NOT BUGS

`[content_fits]` enumerates them against a `kKnownClipping` baseline: NEW clipping fails the
test, a fixed widget produces a STALE warning rather than a red. Seven causes:

1. Icon+caption buttons at a half-cell width (`shutdown`, `lock`, `firmware_restart`,
   `led_controls`) — 31-40px box, icon and caption both overflow. Raise the minimum?
2. `control_buttons` — caption overruns its button by 81-100px on EACH side, at an authored
   minimum of 4x2. The largest overflow in the sweep.
3. `printer_image` state overlays, 40-88px — state-conditional (disconnected/shutdown only).
4. `print_status` idle library card — thumbnail needs 143-151px more HEIGHT, all 8 geometries.
5. `clock` at 1x1 — a half-cell square, 31x31 at portrait Micro; `clock_time` needs 14px more.
6. `tool_badge` vs `#badge_size` — a two-character label exceeds its circle by 2-4px. A token
   that is too small, not a layout problem.
7. Caption-under-indicator stacks (9 widgets), 1-17px, mostly the caption falling off.

Each is a different call: raise the authored minimum (1, 2, 5), add a smaller layout branch
(2, 4, 7), grow a token (6), or accept (3, 6). **Do not fix them in a test task.**

Two limits of that sweep, stated by its own report: the TEXT check is under-exercised because
subject-bound labels are empty under test (only static strings were measured, exactly one
fired), and **13 pairs pass with 0px slack** — one translation from clipping, and the suite
runs in English only.

## PRINT-STATUS AUDIT: WHAT WAS NOT DONE

`pstat-reliability-audit.md` ranked ten hazards. H1, H2, H5 and H9 are fixed on this branch.
Still open: H3 (two live instances share every per-widget layout subject and the single
`arc_widget_`), H4 (`dismiss_*_picker()` clears the static pointer unconditionally), H6
(ABA on the idle deferral's `live_instances()` keying), H7 (8x `lv_obj_is_valid()`, a Tier 1
crash family), H8 (a wrong comment about widget multiplicity), H10. The audit's §6 groups
H3+H4+H8 as one small containment change (~9 lines) plus an optional 50-150 line refactor.

Also open, found in passing: the idle runout path suppresses on
`AmsState::is_filament_operation_active()` and the paused path does not, so a manual unload
during a PAUSED print still pops guidance. Confirmed still true on main.

## PROCESS RULES THIS PLAN LEARNED THE HARD WAY

1. **A Catch2 totals line does NOT prove a run completed.** Catch2's fatal-error handler
   prints a summary after a SIGSEGV — a crashed run reported "2762 test cases, 2761 passed"
   from a binary holding 11294. Judge completeness by the case count, and prefer sharded
   `make test-run`.
2. **A tag-filtered run is not a census.** Two known-red tests were missed for days because
   they carried no `[grid]` tag.
3. **MUTATE THE FEATURE, NOT A CONSTANT.** The tests derive their inputs from the same
   constants, so mutating one shifts both sides and proves nothing. After restoring a mutated
   file, `touch` it or make will have nothing to do and you will test the mutated binary.
4. **Assert on wired behaviour, not a pure helper.** Three tasks in this plan shipped tests
   that exercised arithmetic instead of the feature and passed against broken code.
5. **Never pipe a build or test through `tail`/`head`** — the pipeline returns the filter's
   exit status, so a failure reads as success. And do not end a build check with
   `grep -c error:`: grep exits 1 on zero matches, so a clean build reports as failed.
6. **Poll the `make` PROCESS, never the `cc1plus` count** — cc1plus reads zero in the gaps
   between file compiles and gives false all-clears. Other worktrees and the main tree build
   constantly; wait rather than compete.
7. **Verify the build actually compiled something** before trusting a test result. A `&&`
   short-circuit that skipped the build once produced a confident "verification" against a
   stale binary.
8. **The plan has been wrong many times** — fabricated test harnesses, wrong line numbers, a
   stale reference table, an impossible grep gate, and an expected-failure list that was four
   fifths obsolete. Verify its claims against the code before trusting them, and STOP and ask
   rather than guessing.

## BUILD / TEST

    make -j                          # app binary ONLY
    make test                        # builds tests, does NOT run them
    make test-run                    # builds AND runs, sharded — the real gate
    ./build/bin/helix-tests "[tag]"  # from the WORKTREE ROOT only; never unfiltered

## GIT

Stage explicitly by path. NEVER `git add -A` / `-u` / `commit -a` — this is a shared
worktree. Never `git rm` (use plain `rm`). Never `--no-verify`. Never modify anything under
`lib/` except `lib/helix-xml`, which is our own repo. Conventional commits, subject plus
~4 lines; comments must not reference a refactor, a task number, or "the fix".

Generated-artifact gates that fail AFTER your change looks done: `make regen-tokens` for any
`ui_xml` change, `make translation-sync` + `make translations` for new `lv_tr()` strings,
`make regen-fonts` for new icon codepoints, `make regen-xml-schema` after
`lv_xml_register_widget`.
