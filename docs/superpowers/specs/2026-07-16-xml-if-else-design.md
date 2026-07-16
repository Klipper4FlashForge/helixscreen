# `<if cond>` / `<else/>`: Structural Conditionals

**Date:** 2026-07-16
**Status:** Design — approved in brainstorming; ready for implementation plan.
**Scope:** MAJOR (new reactive parser construct in `lib/helix-xml/`, reusing the `<repeat>` crash-class machinery). Feature 2 of `docs/superpowers/specs/2026-07-16-xml-parser-enhancements-design.md`. Independent; ships on its own.
**Branch / worktree:** `feature/xml-if-else` → `.worktrees/xml-if-else`.

## Summary

Conditional UI today is *created-then-hidden* (`bind_flag hidden` / `cond=`): a printer-type-specific panel builds *every* variant even when one is shown — wasted widgets + startup cost on small devices. `<if>` creates a body only when its condition holds and tears it down reactively when it flips.

```xml
<if cond="printer_has_chamber and chamber_supported">
  <chamber_control_card/>
  <else/>
  <divider_horizontal/>
</if>
```

`<if>` is effectively `<repeat count="cond ? 1 : 0">` with a two-part body — it reuses the `<repeat>` capture / replay / reactive-rebuild / async-off-tree-teardown machinery almost wholesale. `<else/>` is a self-closing divider child: everything before it is the true-body, everything after it (up to `</if>`) is the false-body.

## Decisions (locked in brainstorming)

1. **Inline `<else>` divider, not a sibling block and not `<endif>`.** `<if cond="X"> …true… <else/> …false… </if>`. `</if>` is the terminator (expat enforces the match — no hand-rolled `<endif>` balancing). `<else>` is an empty marker *child* that splits the captured body at a single event index. **Both spellings are accepted** — self-closing `<else/>` and empty-element `<else></else>` behave identically (the split is taken at the `<else>` *open*; the marker's own open+close events are skipped). Author convention: leave it empty. This sidesteps the no-lookahead sibling-pairing problem entirely: there is no "find the `<else>` that follows this `<if>`" — the divider is inside the one matched `<if>…</if>`.
2. **Reactive via `lv_xml_expr_bind`.** `cond` is compiled with the existing `lv_xml_expr` evaluator (word-form house style, same grammar as `<subject_expr>`/`cond=`). Because a condition can reference *multiple* subjects (`a and b > c`), `<repeat>`'s single-int-subject observer is insufficient; `lv_xml_expr_bind(expr, view_root, cb, ud)` observes every referenced subject, fires on any change, and detaches on the owner's `LV_EVENT_DELETE`. A **static** cond (no subjects) expands once at load, no observer.
3. **Shared `xml_frag_*` core.** The body-agnostic `<repeat>` machinery (SAX capture buffer, replay loop, async off-tree teardown, instance-lifetime record) is renamed to neutral `xml_frag_*` names and consumed by both `<repeat>` and `<if>` (and, later, `<for_each>`). Repeat-specific pieces (count resolution, `$i` index) stay layered on top. The rename is mechanical and gated by running the existing `[repeat]` crash-class tests before and after to prove `<repeat>` behaviour is unchanged.
4. **`<else/>` edge rules.** A stray `<else/>` with no enclosing `<if>` → `LV_LOG_WARN` + ignore (create nothing), consistent with the engine's warn-and-degrade norm. A second `<else/>` inside one `<if>` → warn, first split wins.

## Engine facts this design rests on (verified against the code)

All refs `lib/helix-xml/src/xml/` unless noted.

- **`<repeat>` is recognised by hardcoded `lv_streq(name,"repeat")`** in the two view handlers (`lv_xml.c:1862` start, `:2013` end) — there is no runtime special-element registry. `<if>`/`<else>` are added the same way. `<repeat>` returns early on start **without pushing `parent_ll`** (`:1884`); during capture (`cap->active && !cap->replaying`) every start/end/chardata is *buffered*, not executed (`:1888-1891`, `:2032`, `:1092-1101`).
- **Depth is implicit** — `lv_ll_get_len(&state->parent_ll)`; the capture records `cap->base_depth` at the start tag (`:1875`) and the matching close is `name=="repeat" && depth==base_depth` (`:2012-2013`). Because buffered elements never push `parent_ll`, this works only when the construct is *not nested in itself* — which is why nested `<repeat>` is deferred, and nested `<if>` is likewise a v1 non-goal.
- **The capture/replay engine is body-agnostic:** `lv_xml_repeat_capture_t` (`lv_xml_parser.h:74`) holds the buffered SAX event array; `xml_repeat_buffer_event` (`lv_xml.c:1217`) appends; `xml_repeat_expand` (`:1345`) replays a body `count` times through `view_*_element_handler`; `xml_repeat_capture_free` (`:1252`) frees. The `$i` index code (`xml_repeat_index_string` `:1273`, and index use in expand `:1356/:1408`) is the only clearly repeat-specific part.
- **Instance-lifetime record + async teardown (the crash-class machinery to reuse verbatim):** `lv_xml_repeat_t` record (`lv_xml_component_private.h:93`, rationale comment `:74-92`) holds `parent`, `view_root`, `observer`, `capture`, `roots[]`, scope snapshot; `xml_repeat_retain` (`lv_xml.c:1520`) creates it in the *registered* scope's `repeat_ll`, attaches `xml_repeat_instance_delete_cb` (`:1603`) to the view root's `LV_EVENT_DELETE`, and registers the observer **last** (immediate fire = initial build). `xml_repeat_teardown_expansion` (`:1427`) is the pure-C `safe_delete_subtree`: reparent roots to a `LV_LAYOUT_NONE` condemned container on `lv_layer_top()`, then `lv_obj_delete_async()`. Unregister detaches before subject teardown: `repeat_ll` walk runs **before** `subjects_ll` in `lv_xml_component.c:323-336`, calling `lv_xml_repeat_record_free` (`:1622`) which removes the instance-delete cb first.
- **`lv_xml_expr` API** (`lv_xml_expr.h`): `lv_xml_expr_compile(src, resolver, ctx)`; `lv_xml_expr_eval → int32_t`; `lv_xml_expr_subject_count/at`; **`lv_xml_expr_bind(expr, owner, cb, user_data)` (`:50`)** — "Observe every referenced subject; call cb on any change and once immediately. Frees expr AND detaches observers when owner is deleted (LV_EVENT_DELETE). Takes ownership of expr." Resolver typedef `lv_subject_t *(*)(void *ctx, const char *name)`; mirror `cond_flag_scope_resolver` (`lv_xml_obj_parser.c:797`) = `lv_xml_get_subject(scope, name)`.
- **Schema/linter:** `<repeat>` is added to **both** `schema["widgets"]` and `special_elements` in `tools/xml-linter/schema/extract_schema.py:865-883` (because it isn't a registered widget). `<if>`/`<else>` get the same dual registration (`<if>` with a `cond` attr, `<else>` with none).
- **Tests:** `tests/unit/test_xml_repeat_subject_count.cpp` is the crash-class template — reactive expand/rebuild, rapid churn coalescing, 0→N→0 cycles, **GLOBAL observer removed on unregister (no UAF)** (`:105`), **count change AFTER instance delete does not UAF** (`:149`), **re-instantiation does not accumulate stale observers** (`:192`). Harness: `LVGLTestFixture`, component-from-data, `lv_subject_set_int` + `process_lvgl(50)` to drain async teardown, teardown order `lv_obj_delete(v)` then `lv_xml_component_unregister`. Runs under ASAN in `make test-run`.

## Design

### Tag recognition + body capture

Add `<if>` recognition alongside `<repeat>` in the two view handlers:

- **`view_start_element_handler`:** on `name=="if"` and not currently replaying, allocate a fragment capture (`xml_frag_capture_t`), record `base_depth = lv_ll_get_len(parent_ll)`, compile+store the `cond` string, hang it on `state->context`, and return without creating an object / pushing `parent_ll` — identical to the `<repeat>` start path.
- **During capture** (`cap->active && !cap->replaying`): buffer children as today. **On the `<else>` start** at the fragment's child level: do **not** buffer it as a replayable event — record `cap->else_split = cap->event_count` (the index at which the false-body begins) and set `cap->has_else`; **on the matching `<else>` end**, likewise skip buffering. This makes `<else/>` (self-closing: start+end back-to-back) and `<else></else>` (empty element) identical — both skip only the marker's own two events, so the false-body is every buffered event *after* the split. A second `<else>` → `LV_LOG_WARN`, leave the first split (its open/close still skipped). An `<else>` seen with no active fragment capture → warn + ignore.
- **`view_end_element_handler`:** on `name=="if" && depth==base_depth`, finalize: evaluate `cond`; select the body range (`[0, else_split)` when true, `[else_split, end)` when false — the whole buffer is the true-body when there is no `<else/>`); if `cond` is static, expand once and free the capture; if `cond` references subjects, retain the record and bind the observer (below).

### The `xml_frag_*` shared core (rename + generalize)

Mechanically rename the body-agnostic `<repeat>` internals to neutral names, adding the `<if>` needs:

| Current (`<repeat>`) | Shared `xml_frag_*` | Change |
|---|---|---|
| `lv_xml_repeat_capture_t` | `xml_frag_capture_t` | + `else_split` / `has_else`; `count_raw` and the `$i` (`idx_*`, `current_index`) fields stay but are repeat-only |
| `xml_repeat_buffer_event` / `_copy_attrs` / `_capture_free` | `xml_frag_buffer_event` / … | none |
| `xml_repeat_expand(state, cap, count, out_roots, out_n)` | `xml_frag_expand(state, cap, body_lo, body_hi, count, out_roots, out_n)` | take an explicit event range so `<if>` replays a slice; `<repeat>` passes `[0, event_count)` |
| `xml_repeat_teardown_expansion` | `xml_frag_teardown` | none |
| `lv_xml_repeat_t` record + `repeat_ll` | `xml_frag_record_t` + `frag_ll` | none (holds capture + roots + parent + scope snapshot + instance-delete cb) |
| `xml_repeat_retain` | `xml_frag_retain` | split: create record + attach instance-delete cb, but do **not** register the trigger observer — the caller wires the trigger (repeat: its int-subject observer; if: `lv_xml_expr_bind`) |
| `xml_repeat_rebuild_cb` | `xml_frag_rebuild(record, active_body_lo, active_body_hi, count)` | teardown + expand the selected slice |
| `xml_repeat_instance_delete_cb` / `_record_free` | `xml_frag_instance_delete_cb` / `xml_frag_record_free` | none |

`<repeat>`'s count resolution (`xml_repeat_resolve_count`, `xml_repeat_count_is_subject`) and `$i` machinery remain repeat-specific, calling into the frag core. The rename touches shipped, crash-tested code — **gate it**: run `./build/bin/helix-tests "[repeat]"` (all cases) before and after the rename commit and confirm identical results before layering `<if>` on top.

### `<if>` reactive trigger + rebuild

For a subject-referencing `cond`:
1. `xml_frag_retain(...)` creates the record (capture with `else_split`, parent, `view_root`, scope snapshot) and attaches `xml_frag_instance_delete_cb` to the view root.
2. Compile the cond: `lv_xml_expr_t *ex = lv_xml_expr_compile(cond, frag_cond_resolver, &scope)`.
3. `lv_xml_expr_bind(ex, view_root, if_cond_changed_cb, record)`. The callback receives the new int value; it selects the body slice (`value != 0` → true-body range, else false-body range) and calls `xml_frag_rebuild(record, lo, hi, /*count=*/1)`. `lv_xml_expr_bind` fires once immediately → initial build via the same path.

### Numeric-attr / composition interplay

None. `<if>` bodies are ordinary widgets; `${expr}` composition (Feature 1) inside an `<if>` body works unchanged because expansion replays through the normal handlers.

## Cross-cutting risk to resolve in the plan (do not gloss)

**Observer cleanup at component-unregister while an instance is still alive.** `<repeat>`'s unregister sweep walks `repeat_ll` *before* `subjects_ll` teardown and detaches each manual observer, so a rebuild never fires on a freed subject. `<if>` uses `lv_xml_expr_bind`, whose observer detach is tied to the *owner's* `LV_EVENT_DELETE`, **not** to component unregister. If a component is unregistered while an instance is alive, the bound expr observers would remain on subjects that unregister is about to free → UAF. The plan MUST close this, by one of:
- (a) add `lv_xml_expr_unbind(expr)` (or have `_bind` return a handle) so the `xml_frag_record_t` stores the expr and the `frag_ll` unregister sweep detaches it before `subjects_ll` teardown — mirroring the `<repeat>` ordering; **or**
- (b) for `<if>`, do not use `lv_xml_expr_bind`; register the expression's subjects manually (via `lv_xml_expr_subject_count/at`) into the frag record and detach them in the sweep, exactly as `<repeat>` does for its single subject.

(a) is cleaner and keeps `lv_xml_expr_bind`'s ergonomics; it needs a small `lv_xml_expr` API addition. The **"GLOBAL observer removed on unregister (no UAF)"** and **"cond change AFTER instance delete"** tests below are what prove it — write them first (TDD) so the chosen approach is validated under ASAN.

## Testing

`tests/unit/test_xml_if_else.cpp`, mirroring `test_xml_repeat_subject_count.cpp`. Under ASAN via `make test-run`.

**Static:**
1. Static true cond → true-body present, false-body absent.
2. Static false cond → false-body present (the `<else/>` branch), true-body absent.
3. No `<else/>`, static false → nothing created; `<if>` still balances (component loads).

**Reactive (subject cond; `lv_subject_set_int` + `process_lvgl(50)` between):**
4. cond flips true→false→true → true-body created, torn down + false-body created, then reversed. Assert child identity/count each step.
5. Multi-subject cond (`a and b > c`) → changing any operand re-evaluates (proves `lv_xml_expr_bind` multi-subject).
6. Rapid churn coalesces to the final value (intermediate teardown no-UAF).

**Crash-class (the reason this feature is MAJOR — write these first):**
7. **cond change AFTER instance delete does not UAF** — create, delete instance, `lv_subject_set_int`, drain; ASAN-clean (observer detached by instance-delete cb).
8. **GLOBAL cond subject removed on unregister (no UAF)** — global subject outlives component; delete instance, unregister, then change the global; ASAN-clean. *This is the test that validates the cross-cutting-risk resolution above.*
9. **Re-instantiation does not accumulate stale observers** — create v1, delete v1, create v2, change cond; v1's record is gone (no teardown on freed roots).

**`<else/>` edges:**
10. Stray `<else/>` outside any `<if>` → warn + ignore, component still loads, no body from it.
11. Second `<else/>` in one `<if>` → warn, first split wins (assert the split boundary via which children land in true vs false).
12. **Both spellings equivalent** — a component using `<else></else>` produces the same true/false split as the identical component using `<else/>` (assert identical child sets in both branches).

**Regression (the rename gate):**
13. `./build/bin/helix-tests "[repeat]"` — all `<repeat>` cases pass unchanged after the `xml_frag_*` extraction.

## Docs (ship with the code)

- `docs/devel/LVGL9_XML_GUIDE.md` — new `<if>`/`<else/>` section: syntax, the inline-divider semantics, when to prefer `<if>` (expensive/structural conditional creation) vs `bind_flag`/`cond=` (cheap show/hide of light subtrees — *those stay*), the last-child / own-container **ordering ⚠️** (a reactively-rebuilt body with later same-parent siblings mis-orders on rebuild — put `<if>` last or in its own container, same constraint as `<repeat>`), and the static-vs-reactive note.
- `docs/devel/LVGL9_XML_ATTRIBUTES_REFERENCE.md` — `<if>` (`cond` attr) and `<else>` rows.
- `.claude/skills/helix-xml/references/xml-guide.md` + `xml-attributes.md` — mirror.
- CLAUDE.md § "CRITICAL RULES - Declarative UI" — extend row 2 (imperative visibility) or add a note: `<if>` is the structural sibling of `bind_flag hidden`.

## Tooling

- Register `<if>` and `<else>` in **both** `schema["widgets"]` and `special_elements` in `extract_schema.py` (`<if>`: `cond` string attr; `<else>`: no attrs), then regen the schema and commit the diff.
- Confirm the crossref linter treats `cond` on `<if>` like the existing `cond=` (expression, skip `UNKNOWN_SUBJECT_REF`); it already handles `cond=` on the bind_*_if tags — verify `<if cond>` is covered or teach it.

## Non-goals (deferred)

- **Nested `<if>` (an `<if>` inside an `<if>`/`<repeat>` body)** — same reason as nested `<repeat>` (buffered elements don't push `parent_ll`, so depth-based matching can't distinguish self-nesting). v1 non-goal; a later effort with an explicit construct-depth counter.
- **`<elif>` / multi-way** — a `<switch>`/`<case>` cousin is a possible later feature; this is if/else only.
- **Replacing `bind_flag`/`cond=` for cheap show/hide** — those stay; `<if>` is for expensive/structural conditional *creation*, not light visibility toggles.

## Risks

- **Rename regression** — the `xml_frag_*` extraction touches shipped crash-class code; the `[repeat]` suite before/after is the gate (test 13).
- **Unregister-while-alive observer cleanup** — the cross-cutting risk above; tests 7-8 prove it; likely needs a small `lv_xml_expr_unbind` API.
- **Ordering constraint** — a rebuilt `<if>` body with later same-parent siblings mis-orders (LVGL append); documented + a test asserting the last-child usage.
- **`<else/>` depth detection during capture** — buffered elements don't push `parent_ll`, so "the `<else/>` belonging to this `<if>`" is simply "an `<else/>` seen while this fragment is the active capture and not replaying"; unambiguous only because nested `<if>` is deferred — enforce/detect and warn if that assumption is violated.
