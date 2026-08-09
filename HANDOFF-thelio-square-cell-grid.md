# Handoff: continue the square-cell home-grid rework on thelio

Paste everything below the line into a fresh Claude Code session on thelio.

---

Continue the HelixScreen square-cell home-grid rework. Work on this machine
(thelio), not the Mac.

## SETUP

    git fetch origin
    git checkout fix/grid-cell-metrics    # must be at 96126e380 or later
    git submodule update --init --recursive

If the branch is behind 96126e380, stop - it was not pushed from the Mac yet.

## WHERE THINGS STAND

Read these two, in this order. Both are tracked in git now:

    docs/superpowers/specs/2026-08-04-home-grid-sizing-design.md   (approved design)
    docs/superpowers/plans/2026-08-08-square-cell-grid.md          (13-task plan)

The plan has an "Owner rulings" section near the top with SEVEN rulings that
SUPERSEDE parts of the spec. Read all seven before touching anything. The two
that bite hardest:

- **Ruling 6**: track counts floor to a whole number of cells. Medium 800x480 is
  12x8 (not 13x8); Large 1024x600 and XLarge 1280x720 are 16x10 (not 17x10).
- **Ruling 7**: assert MEASURED cell aspect in [0.80, 1.25], never nominal.
  Nominal aspect is anti-correlated with reality - it ignores per-axis padding
  and gutters. Do not retune GRID_CELL to satisfy it.

## DONE ALREADY

Plan 1 + the full widget pixel migration: MERGED to main.

Plan 2 Tasks 1-5, on this branch:

    1  5fada4f30  half-cell capability flags on PanelWidgetDef
    2  34d69feac  resize hit band clamps to extent/3 per axis
    3  79aac6fec  step-aware snapping; snap_step_for(); TRACKS_PER_CELL
    4  7e63212d2  two-tier dot lattice
    5  e329906ba  THE SQUARE-CELL MODEL. TRACKS_PER_CELL 1->2, one
                  get_dimensions() expression, GRID_DIMS / TARGET_CELL_* /
                  MIN_PORTRAIT_COLS all deleted. 272x480 went 8 cells -> 112.

## NEXT: TASK 6

Row tracks from the grid, not from widget content. Then 7-13.

Use the `superpowers:subagent-driven-development` skill. A ledger existed at
`.superpowers/sdd/2026-08-08-square-cell-grid/progress.md` but it is gitignored
scratch, so it did NOT travel. Recreate it with this first line:

    # SDD ledger — plan: docs/superpowers/plans/2026-08-08-square-cell-grid.md

and record Tasks 1-5 as complete from the list above before dispatching Task 6.

## NINE KNOWN FAILING TESTS - EXPECTED, ALREADY ROUTED

Do not "fix" these ad hoc:

| test | cause |
|---|---|
| `test_grid_edit_drag_path.cpp:280-281` | snap granularity changed by TRACKS_PER_CELL 1->2. Intentional; the hand-computed expectation is stale. |
| `test_grid_edit_mode.cpp:820, 981, 1908-1911` | construct `GridLayout(Medium)` assuming the deleted static 6x4 table. |
| `test_panel_widget_portrait_span.cpp:228,346,401,531,588` | registry spans still authored in old units. **TASK 7 fixes these.** |

Everything else is green: `[square]` 152/5, `[grid_layout]` 462/40,
`[half_cell]` 69/9.

## TWO PROCESS RULES, LEARNED THE HARD WAY ON THIS PLAN

1. **MUTATE THE FEATURE, NOT THE PREDICATE.** Delete the code that produces an
   effect and confirm the suite reddens. Do NOT mutate a constant - the tests
   derive their inputs from the same constants, so it shifts both sides and
   proves nothing. One task in this series passed its prescribed mutation while
   having ZERO coverage of the behaviour it existed to protect.

2. **THIS PLAN'S TESTS LEAN ON PURE HELPERS.** Three separate tasks shipped a
   test that exercised an arithmetic helper instead of the wired feature, and
   the mutation passed against broken code every time. Assert on wired
   behaviour. Task 4's `dot_count()` is the cautionary example: pure arithmetic
   that never called `create_dots_overlay()`, so deleting the refresh call
   stayed green.

The plan is good but NOT infallible - it has already been wrong three times:
a rounding expectation that contradicted its own implementation; an LVGL child
ordering that would have killed edit-mode button clicks; a stale expected-grid
table. Verify its numbers and line references against the code before trusting
them, and STOP and ask rather than guessing if something does not match.

## BUILD / TEST (thelio)

    make -j                          # app binary ONLY, not tests
    make test                        # builds tests, does NOT run them
    make test-run                    # builds AND runs (~90-105s)
    ./build/bin/helix-tests "[tag]"  # never unfiltered - [slow] will hang you

- Check `pgrep -x make` before building (`pgrep -f` self-matches).
- Some tests deliberately print `[error]` lines - judge by the Catch2 summary.
- Run from the REPO ROOT or you get hundreds of phantom UI failures.

## GIT

- Stage explicitly by path. NEVER `git add -A` / `-u` / `commit -a`.
- Never `--no-verify`.
- Never modify anything under `lib/` except `lib/helix-xml`, which is our own
  repo (commit inside it, then commit the bumped pointer).
- Conventional commits: `type(scope): subject`, plus ~4 lines. Comments must not
  reference a refactor, a task number, or "the fix".

## SEQUENCING - TWO CHECKPOINTS THAT MATTER

Tasks 5-9 are a window where saved layouts hold OLD-UNIT coordinates and a dev
dashboard renders clustered top-left. Tests stay green throughout. Use a scratch
`HELIX_CONFIG_DIR` to look at it; do not "fix" the clustering early.

**TASK 11 IS THE POINT OF NO RETURN**: once a config is stamped v22, saved
col/row/span are gone and reverting the code will not bring them back. Flag it
to Preston before running it - do not land it unannounced.
