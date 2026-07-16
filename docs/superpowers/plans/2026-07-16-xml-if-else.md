# `<if cond>` / `<else/>` Structural Conditionals — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add an `<if cond="X"> …true… <else/> …false… </if>` reactive structural conditional to the helix-xml engine, reusing the `<repeat>` capture/replay/async-teardown machinery via a shared `xml_frag_*` core.

**Architecture:** `<if>` is `<repeat count="cond ? 1 : 0">` with a two-part body. The body-agnostic `<repeat>` internals (SAX capture buffer, range replay, off-tree async teardown, instance-lifetime record) are renamed to neutral `xml_frag_*` and consumed by both constructs. `<if>`'s reactive trigger is `lv_xml_expr_bind` (multi-subject), which needs a new `lv_xml_expr_unbind` so the unregister sweep can detach cond observers before subject teardown — parity with `<repeat>`'s `frag_ll` sweep. Static conds expand once at load with no observer.

**Tech Stack:** C (LVGL 9.5 fork, `lib/helix-xml/`, direct-edit — NOT a submodule, NOT clang-formatted), Catch2 C++ tests under ASAN (`make test-run`), Python schema extractor (`tools/xml-linter/`).

## Global Constraints

- **`lib/helix-xml/` is direct-edit, pure C, NOT clang-formatted.** Match the surrounding brace/indent style (4-space, braces on same line for `if`, K&R). Do not reformat untouched lines.
- **Crash-class threading rules (CLAUDE.md § Threading):** the reactive rebuild runs synchronously inside a UpdateQueue drain batch (the subject-set path). It MUST NOT synchronously delete widgets — teardown reparents roots to a `LV_LAYOUT_NONE` condemned container on `lv_layer_top()` then `lv_obj_delete_async()`. Never call `lv_obj_is_valid()` in teardown [L076].
- **Mandatory teardown order is delete-instance-then-unregister.** Observers tied to the instance view root are detached by its `LV_EVENT_DELETE`. The `frag_ll` unregister sweep is the fallback for the out-of-order (unregister-while-alive) case — which becomes a **frequent production path on ESP32** (component-unregister-to-reclaim-memory), so it must be correct and tested, not decorative.
- **Word-form operators only in `cond` house style** (`and/or/not/eq/ne/lt/le/gt/ge`) — same grammar as `<subject_expr>`/`cond=`. `&&`/`<` require XML escaping; author convention is word forms.
- **`<else/>` and `<else></else>` are byte-for-byte equivalent** — both skip only the marker's own open+close events; the false-body is every buffered event after the split.
- **No behavior change to `<repeat>`.** The `xml_frag_*` rename is gated: `./build/bin/helix-tests "[repeat]"` passes identically before and after Task 2.
- **SPDX header** on any new file: `// SPDX-License-Identifier: GPL-3.0-or-later`.
- **New XML tags require schema regen:** register in `extract_schema.py`, run `make regen-xml-schema`, commit `tools/xml-linter/schema/schema.json` [L089].

## File Structure

| File | Responsibility | Tasks |
|---|---|---|
| `lib/helix-xml/src/xml/lv_xml_expr.h` | Add `lv_xml_expr_bind_t` opaque handle; `_bind` returns it; declare `lv_xml_expr_unbind` | 1 |
| `lib/helix-xml/src/xml/lv_xml_expr.c` | Store `owner` in bind ctx; return handle; implement `_unbind` | 1 |
| `tests/unit/test_xml_expr_reactive.cpp` | `_unbind` unit tests (detach + no double-free) | 1 |
| `lib/helix-xml/src/xml/lv_xml_parser.h` | Rename capture struct → `xml_frag_capture_t`; add `else_split`/`has_else` | 2, 3 |
| `lib/helix-xml/src/xml/lv_xml_component_private.h` | Rename record → `xml_frag_record_t`; rename `repeat_ll`→`frag_ll`; add `bind` field | 2, 4 |
| `lib/helix-xml/src/xml/lv_xml_component.c` | Rename sweep symbols; `_unbind` in the frag sweep | 2, 4 |
| `lib/helix-xml/src/xml/lv_xml.c` | Rename `xml_repeat_*`→`xml_frag_*` core; range-based expand; split retain; `<if>` capture/static/reactive | 2, 3, 4 |
| `tests/unit/test_xml_if_else.cpp` | `<if>`/`<else>` behavior + crash-class tests | 3, 4 |
| `tools/xml-linter/schema/extract_schema.py` | Register `<if>`/`<else>` (widgets + special_elements) | 5 |
| `tools/xml-linter/schema/schema.json` | Regenerated | 5 |
| `docs/devel/LVGL9_XML_GUIDE.md`, `docs/devel/LVGL9_XML_ATTRIBUTES_REFERENCE.md`, `.claude/skills/helix-xml/references/{xml-guide,xml-attributes}.md`, `CLAUDE.md` | Docs | 6 |

---

### Task 1: `lv_xml_expr_unbind` — detachable reactive bind

Add the primitive that lets a caller tear down a `lv_xml_expr_bind` **before** the owner is deleted, so the unregister sweep can detach cond observers before their subjects are freed. `_bind` currently returns `void` and ties cleanup solely to the owner's `LV_EVENT_DELETE`; this is source-compatible for the three existing `cond=` callers (they ignore the return value and never call `_unbind`).

**Files:**
- Modify: `lib/helix-xml/src/xml/lv_xml_expr.h`
- Modify: `lib/helix-xml/src/xml/lv_xml_expr.c:219-256`
- Test: `tests/unit/test_xml_expr_reactive.cpp`

**Interfaces:**
- Produces: `typedef struct lv_xml_expr_bind_t lv_xml_expr_bind_t;` (opaque); `lv_xml_expr_bind_t * lv_xml_expr_bind(...)` (return type changed from `void`); `void lv_xml_expr_unbind(lv_xml_expr_bind_t * handle)`.
- `lv_xml_expr_unbind(h)`: removes every per-subject observer, removes the owner's delete cb (so a later owner delete does NOT double-free), frees the expr and the bind context. NULL-safe.

- [ ] **Step 1: Write the failing tests**

Append to `tests/unit/test_xml_expr_reactive.cpp` (mirror the existing bind test at line 32 — same helper `cb`/counter pattern already in that file; reuse its file-scope subject + `owner` setup convention):

```cpp
TEST_CASE_METHOD(LVGLTestFixture, "expr_unbind: detaches observers; later change does not fire",
                 "[xml][expr]") {
    lv_subject_t s;
    lv_subject_init_int(&s, 1);
    lv_obj_t * owner = lv_obj_create(lv_screen_active());

    static int fires = 0; fires = 0;
    auto cb = [](void *, int32_t) { fires++; };

    lv_xml_expr_t * e = lv_xml_expr_compile("s gt 0", expr_reactive_test_resolver, &s);
    REQUIRE(e != nullptr);
    lv_xml_expr_bind_t * h = lv_xml_expr_bind(e, owner, cb, nullptr);
    REQUIRE(h != nullptr);
    REQUIRE(fires == 1);                 // immediate fire at bind

    lv_xml_expr_unbind(h);               // detach BEFORE any owner delete
    lv_subject_set_int(&s, 5);           // must not reach the freed bind
    process_lvgl(10);
    REQUIRE(fires == 1);                 // no further fire after unbind

    lv_obj_delete(owner);                // must NOT double-free (delete cb removed)
    process_lvgl(10);
    SUCCEED("unbind detached cleanly; owner delete was a no-op");
    lv_subject_deinit(&s);
}

TEST_CASE_METHOD(LVGLTestFixture, "expr_unbind: NULL handle is a no-op", "[xml][expr]") {
    lv_xml_expr_unbind(nullptr);
    SUCCEED("NULL unbind did not crash");
}
```

> The resolver `expr_reactive_test_resolver` already exists in this file (used by the line-32 test). If it is `static` with a different name, reuse whatever single-subject resolver that file defines — do not add a duplicate.

- [ ] **Step 2: Run to verify they fail**

Run: `make test && ./build/bin/helix-tests "expr_unbind"`
Expected: FAIL — `lv_xml_expr_unbind` undeclared / `lv_xml_expr_bind` returns void (no `h`).

- [ ] **Step 3: Update the header**

In `lib/helix-xml/src/xml/lv_xml_expr.h`, replace the `_bind` declaration (lines 47-51) with:

```c
/* Opaque handle for a reactive bind; pass to lv_xml_expr_unbind to detach early. */
typedef struct lv_xml_expr_bind_t lv_xml_expr_bind_t;

/* Observe every referenced subject; call cb(user_data, eval(expr)) on any change and once
 * immediately. Frees `expr` AND detaches observers when `owner` is deleted (LV_EVENT_DELETE).
 * Takes ownership of `expr`. Returns a handle (or NULL on OOM) usable with lv_xml_expr_unbind. */
lv_xml_expr_bind_t * lv_xml_expr_bind(lv_xml_expr_t * expr, lv_obj_t * owner,
                                      void (*cb)(void * user_data, int32_t value), void * user_data);

/* Detach a bind created by lv_xml_expr_bind BEFORE its owner is deleted: removes every
 * per-subject observer and the owner's delete hook (so a later owner delete does not
 * double-free), then frees the expr and the bind context. NULL-safe. */
void lv_xml_expr_unbind(lv_xml_expr_bind_t * handle);
```

- [ ] **Step 4: Update the implementation**

In `lib/helix-xml/src/xml/lv_xml_expr.c`, name the bind struct and add `owner`, change `_bind` to return the handle, and add `_unbind`. Replace lines 219-256:

```c
struct lv_xml_expr_bind_t { lv_xml_expr_t * expr; void (*cb)(void *, int32_t); void * user_data;
                            lv_obj_t * owner; bool registering; };
typedef struct lv_xml_expr_bind_t expr_bind_t;

static void expr_bind_observer_cb(lv_observer_t * obs, lv_subject_t * subject){
    LV_UNUSED(subject);
    expr_bind_t * b = obs->user_data;
    if(b->registering) return;   /* suppress the add-time fire; see explicit fire below */
    b->cb(b->user_data, lv_xml_expr_eval(b->expr));
}

static void expr_bind_delete_cb(lv_event_t * e){
    expr_bind_t * b = lv_event_get_user_data(e);
    lv_xml_expr_free(b->expr);
    lv_free(b);
}

lv_xml_expr_bind_t * lv_xml_expr_bind(lv_xml_expr_t * expr, lv_obj_t * owner,
                      void (*cb)(void * user_data, int32_t value), void * user_data){
    expr_bind_t * b = lv_malloc(sizeof(expr_bind_t));
    if(b == NULL) { lv_xml_expr_free(expr); return NULL; }
    b->expr = expr; b->cb = cb; b->user_data = user_data; b->owner = owner; b->registering = true;

    /* One observer per distinct subject, tied to `owner`; do NOT auto-free the
     * shared context (freed once by expr_bind_delete_cb or lv_xml_expr_unbind). */
    size_t n = lv_xml_expr_subject_count(expr);
    for(size_t i=0;i<n;i++){
        lv_observer_t * o = lv_subject_add_observer_obj(lv_xml_expr_subject_at(expr,i),
                                                        expr_bind_observer_cb, owner, b);
        if(o) o->auto_free_user_data = 0;
    }
    b->registering = false;

    lv_obj_add_event_cb(owner, expr_bind_delete_cb, LV_EVENT_DELETE, b);

    cb(user_data, lv_xml_expr_eval(expr));   /* initial value, once */
    return b;
}

void lv_xml_expr_unbind(lv_xml_expr_bind_t * b){
    if(b == NULL) return;
    /* Detach every per-subject observer whose user_data is this bind ctx. The observers
     * were registered on `owner`, so removing them by (cb, user_data) is exact. */
    size_t n = lv_xml_expr_subject_count(b->expr);
    for(size_t i=0;i<n;i++){
        lv_subject_t * s = lv_xml_expr_subject_at(b->expr, i);
        if(s) lv_subject_remove_obj_from_subject(s, b->owner, /*not needed*/ NULL); /* see note */
    }
    /* Remove the owner's delete hook so a later owner delete does not double-free. */
    lv_obj_remove_event_cb_with_user_data(b->owner, expr_bind_delete_cb, b);
    lv_xml_expr_free(b->expr);
    lv_free(b);
}
```

> **Observer-removal API note:** the exact call to detach a subject observer registered via `lv_subject_add_observer_obj(subject, cb, owner, b)` must match this fork's observer API. Prefer removing by the observer object if `_bind` retains them. If a `(subject, obj)` or `(subject, cb, user_data)` removal helper does not exist, retain the `lv_observer_t *` array in the bind ctx at registration and call `lv_observer_remove(o)` for each in `_unbind` (this is the same pattern `xml_frag_record_free_heap` uses for the single repeat observer). **Grep `lv_observer_remove`, `lv_subject_remove_observer`, and `lv_obj_remove_event_cb_with_user_data` in `lib/helix-xml/` and pick the removal primitive that already exists; adjust the loop above to use it. Do not invent an API.** The retained-array form is the safe default.

- [ ] **Step 5: Run tests to verify they pass (ASAN)**

Run: `make test-run 2>&1 | tail -30` then `./build/bin/helix-tests "[expr]"`
Expected: PASS, ASAN-clean. The three existing `cond=` callers still compile (return value ignored) and their tests (`test_xml_cond_binds`) still pass.

- [ ] **Step 6: Commit**

```bash
git add lib/helix-xml/src/xml/lv_xml_expr.h lib/helix-xml/src/xml/lv_xml_expr.c tests/unit/test_xml_expr_reactive.cpp
git commit -m "feat(xml-expr): lv_xml_expr_unbind for early reactive-bind teardown"
```

---

### Task 2: Extract `xml_frag_*` shared core from `<repeat>` (mechanical rename, `[repeat]`-gated)

Rename the body-agnostic `<repeat>` internals to neutral `xml_frag_*`, generalize `expand` to a `[lo,hi)` event range, and split `retain` so the caller wires the trigger. **No behavior change** — the `[repeat]` suite is the gate.

**Files:**
- Modify: `lib/helix-xml/src/xml/lv_xml_parser.h` (capture struct)
- Modify: `lib/helix-xml/src/xml/lv_xml_component_private.h` (record struct, `frag_ll`, prototype)
- Modify: `lib/helix-xml/src/xml/lv_xml_component.c` (init + unregister sweep)
- Modify: `lib/helix-xml/src/xml/lv_xml.c` (all `xml_repeat_*` core + call sites)

**Interfaces:**
- Consumes: nothing new.
- Produces: `xml_frag_capture_t`, `xml_frag_record_t`, `scope->frag_ll`, `lv_xml_frag_record_free()`, and `static void xml_frag_expand(state, cap, uint32_t lo, uint32_t hi, int32_t count, out_roots, out_root_count)` + `static void xml_frag_rebuild(xml_frag_record_t * r, uint32_t lo, uint32_t hi, int32_t count)`. Repeat-specific `$i`, `count_raw`, `xml_repeat_resolve_count`, `xml_repeat_count_is_subject` stay (they may keep `repeat` in their names — they are repeat-only).

- [ ] **Step 1: Gate — capture the baseline**

Run: `make test && ./build/bin/helix-tests "[repeat]" 2>&1 | tail -15`
Expected: record the PASS count (e.g. "6 test cases, N assertions — all passed"). This is the before-image the rename must reproduce.

- [ ] **Step 2: Rename the type + field names**

Apply this exact symbol map across `lib/helix-xml/src/xml/{lv_xml.c,lv_xml_parser.h,lv_xml_component_private.h,lv_xml_component.c}`. These are the **body-agnostic** names only:

| From | To |
|---|---|
| `lv_xml_repeat_capture_t` | `xml_frag_capture_t` |
| `lv_xml_repeat_event_t` | `xml_frag_event_t` |
| `lv_xml_repeat_t` (record) | `xml_frag_record_t` |
| `repeat_ll` (scope field) | `frag_ll` |
| `xml_repeat_copy_attrs` | `xml_frag_copy_attrs` |
| `xml_repeat_buffer_event` | `xml_frag_buffer_event` |
| `xml_repeat_capture_free` | `xml_frag_capture_free` |
| `xml_repeat_expand` | `xml_frag_expand` |
| `xml_repeat_teardown_expansion` | `xml_frag_teardown` |
| `xml_repeat_rebuild_cb` | `xml_frag_rebuild_cb` (repeat's observer wrapper — see Step 4) |
| `xml_repeat_record_free_heap` | `xml_frag_record_free_heap` |
| `xml_repeat_instance_delete_cb` | `xml_frag_instance_delete_cb` |
| `lv_xml_repeat_record_free` | `lv_xml_frag_record_free` |

**Keep** (repeat-specific, do NOT rename): `xml_repeat_index_string`, `xml_repeat_resolve_count`, `xml_repeat_count_is_subject`, `xml_repeat_retain` (renamed/split in Step 5), and all `$i`/`idx_*`/`current_index`/`count_raw`/`count_subject` fields.

Update the doc comment at `lv_xml_component.c:323-336` and `lv_xml_component_private.h:74-92` to say `frag` where it said `repeat`, keeping the meaning.

- [ ] **Step 3: Verify the rename compiles & `[repeat]` still passes**

Run: `make test && ./build/bin/helix-tests "[repeat]" 2>&1 | tail -15`
Expected: identical PASS count to Step 1.

- [ ] **Step 4: Generalize `xml_frag_expand` to a `[lo,hi)` event range**

Change the signature and the two loop bounds in `lv_xml.c` (was `xml_repeat_expand`, lines ~1345-1418):

```c
static void xml_frag_expand(lv_xml_parser_state_t * state, xml_frag_capture_t * cap,
                            uint32_t lo, uint32_t hi, int32_t count,
                            lv_obj_t *** out_roots, uint32_t * out_root_count)
{
    if(count < 0) count = 0;
    if(hi > cap->event_count) hi = cap->event_count;
    ...
    cap->replaying = true;
    for(int32_t i = 0; i < count; i++) {
        cap->current_index = i;
        int nest = 0;
        for(uint32_t e = lo; e < hi; e++) {   /* was: e = 0; e < cap->event_count */
            xml_frag_event_t * ev = &cap->events[e];
            ...
        }
    }
    ...
}
```

Update both existing call sites in the `</repeat>` close path to pass the full range:
- `lv_xml.c` literal path (was line 2027): `xml_frag_expand(state, cap, 0, cap->event_count, count, NULL, NULL);`
- inside `xml_frag_rebuild` (see Step 5): pass `0, cap->event_count` for repeat.

- [ ] **Step 5: Split `retain` and extract `xml_frag_rebuild`**

Introduce the shared rebuild core (teardown + reconstruct tmp state + `xml_frag_expand(lo,hi,count)`), and make repeat's observer callback a thin wrapper. In `lv_xml.c`:

```c
/* Shared rebuild core: async-teardown the prior expansion, then replay events
 * [lo,hi) `count` times into r->parent via a reconstructed parser state. Used by
 * both <repeat> (lo=0, hi=event_count, count=subject value) and <if> (count=1,
 * lo/hi = the selected true/false slice). */
static void xml_frag_rebuild(xml_frag_record_t * r, uint32_t lo, uint32_t hi, int32_t count)
{
    if(r == NULL || r->in_rebuild) return;
    r->in_rebuild = true;

    xml_frag_teardown(r);   /* async off-tree; frees r->roots array */

    lv_xml_parser_state_t tmp_state;
    lv_xml_parser_state_init(&tmp_state);
    tmp_state.scope = r->scope;
    tmp_state.parent = r->parent;
    tmp_state.parent_attrs = (const char **)r->parent_attrs;
    tmp_state.parent_scope = r->parent_scope;
    tmp_state.context = r->capture;

    lv_obj_t ** pnode = lv_ll_ins_head(&tmp_state.parent_ll);
    if(pnode) *pnode = r->parent;

    xml_frag_expand(&tmp_state, (xml_frag_capture_t *)r->capture, lo, hi, count,
                    &r->roots, &r->root_count);

    xml_state_free_composed(&tmp_state);
    lv_ll_clear(&tmp_state.parent_ll);
    free_pcdata_ll(&tmp_state);

    r->in_rebuild = false;
}

/* <repeat> count-subject observer: clamp count, rebuild the full body once per count. */
static void xml_frag_rebuild_cb(lv_observer_t * observer, lv_subject_t * subject)
{
    LV_UNUSED(subject);
    xml_frag_record_t * r = (xml_frag_record_t *)lv_observer_get_user_data(observer);
    if(r == NULL) return;
    int32_t count = r->count_subject ? lv_subject_get_int(r->count_subject) : 0;
    if(count < 0) count = 0;
    if(count > 256) { LV_LOG_WARN("<repeat> count subject resolved to %d, clamping to 256", (int)count); count = 256; }
    xml_frag_rebuild(r, 0, ((xml_frag_capture_t *)r->capture)->event_count, count);
}
```

Split `xml_repeat_retain` into (a) a generic `xml_frag_record_t * xml_frag_retain(state, cap)` that creates the record in the registered scope's `frag_ll`, snapshots parent/view_root/scope/parent_attrs, attaches `xml_frag_instance_delete_cb`, clears `state->context`, and returns the record **without wiring any trigger**; and (b) keep the repeat-specific wiring at the `</repeat>` call site:

```c
/* Generic: move a captured body into a retained frag record (no trigger wired). */
static xml_frag_record_t * xml_frag_retain(lv_xml_parser_state_t * state, xml_frag_capture_t * cap)
{
    lv_xml_component_scope_t * reg = lv_xml_component_get_scope(state->scope.name);
    xml_frag_record_t * r = reg ? lv_ll_ins_tail(&reg->frag_ll) : NULL;
    if(r == NULL) {
        if(reg == NULL) LV_LOG_WARN("<if>/<repeat> subject-bound in an unnamed scope; reactivity disabled");
        else LV_LOG_ERROR("OOM: failed to retain frag record; reactivity disabled");
        return NULL;   /* caller falls back to a one-shot expansion + capture_free */
    }
    lv_memzero(r, sizeof(*r));
    r->capture = cap;
    lv_obj_t ** tail = lv_ll_get_tail(&state->parent_ll);
    r->parent = tail ? *tail : state->parent;
    r->view_root = state->view;
    r->scope = state->scope;
    r->parent_scope = state->parent_scope;
    r->parent_attrs = state->parent_attrs ? xml_frag_copy_attrs(state->parent_attrs) : NULL;
    state->context = NULL;   /* record owns the capture now */
    if(r->view_root) lv_obj_add_event_cb(r->view_root, xml_frag_instance_delete_cb, LV_EVENT_DELETE, r);
    else LV_LOG_WARN("frag subject-bound with no view root; lifetime falls back to unregister");
    return r;
}
```

The `</repeat>` close path (was line 2019-2024) becomes:

```c
if(xml_repeat_count_is_subject(cap->count_raw)) {
    lv_subject_t * cs = lv_xml_get_subject(&state->scope, cap->count_raw);
    if(cs) {
        xml_frag_record_t * r = xml_frag_retain(state, cap);
        if(r) { r->count_subject = cs; r->observer = lv_subject_add_observer(cs, xml_frag_rebuild_cb, r); return; }
        /* retain failed: fall through to one-shot below (state->context still == cap) */
    }
}
int32_t count = xml_repeat_resolve_count(state, cap->count_raw);
xml_frag_expand(state, cap, 0, cap->event_count, count, NULL, NULL);
xml_frag_capture_free(cap);
state->context = NULL;
```

> `xml_frag_record_free_heap` already detaches `r->observer` via `lv_observer_remove`; keep that. Task 4 adds the `r->bind` teardown for `<if>`.

- [ ] **Step 6: Verify `[repeat]` unchanged (ASAN)**

Run: `make test-run 2>&1 | tail -20` then `./build/bin/helix-tests "[repeat]" 2>&1 | tail -15`
Expected: identical PASS count to Step 1, ASAN-clean. Any diff means the extraction changed behavior — fix before committing.

- [ ] **Step 7: Commit**

```bash
git add lib/helix-xml/src/xml/lv_xml.c lib/helix-xml/src/xml/lv_xml_parser.h lib/helix-xml/src/xml/lv_xml_component_private.h lib/helix-xml/src/xml/lv_xml_component.c
git commit -m "refactor(xml): extract xml_frag_* shared core from <repeat> (no behavior change)"
```

---

### Task 3: `<if>` tag recognition, `<else>` split capture, static expansion

Recognize `<if>` in the two view handlers; buffer its body like `<repeat>`; split at `<else>` (both spellings); on `</if>` evaluate a **static** cond and expand the selected slice once. No reactivity yet.

**Files:**
- Modify: `lib/helix-xml/src/xml/lv_xml_parser.h` (add `else_split`/`has_else` to `xml_frag_capture_t`)
- Modify: `lib/helix-xml/src/xml/lv_xml.c` (`view_start_element_handler`, `view_end_element_handler`)
- Test: `tests/unit/test_xml_if_else.cpp` (create)

**Interfaces:**
- Consumes: `xml_frag_capture_t`, `xml_frag_expand`, `xml_frag_capture_free`, `lv_xml_expr_compile`/`_eval`/`_free`, `lv_xml_get_subject`.
- Produces: `<if>` static behavior. A new resolver `static lv_subject_t * frag_cond_resolver(void * ctx, const char * name)` = `lv_xml_get_subject((lv_xml_component_scope_t *)ctx, name)` (mirror `cond_flag_scope_resolver`, `lv_xml_obj_parser.c:797`).

- [ ] **Step 1: Write the failing static tests**

Create `tests/unit/test_xml_if_else.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include "../catch_amalgamated.hpp"
#include "../lvgl_test_fixture.h"
extern "C" {
#include "helix-xml/src/xml/lv_xml.h"
}

static const char * COMP_STATIC_TRUE =
  "<component><subjects><subject name='c' type='int' value='1'/></subjects>"
  "  <view><lv_obj name='root'>"
  "    <if cond='c gt 0'><lv_obj name='t'/><else/><lv_obj name='f'/></if>"
  "  </lv_obj></view></component>";

TEST_CASE_METHOD(LVGLTestFixture, "if: static true -> true-body only", "[xml][if]") {
    REQUIRE(lv_xml_register_component_from_data("if_st", COMP_STATIC_TRUE) == LV_RESULT_OK);
    lv_obj_t * v = (lv_obj_t *)lv_xml_create(lv_screen_active(), "if_st", nullptr);
    lv_obj_t * root = lv_obj_find_by_name(v, "root");
    REQUIRE(root != nullptr);
    REQUIRE(lv_obj_find_by_name(v, "t") != nullptr);
    REQUIRE(lv_obj_find_by_name(v, "f") == nullptr);
    lv_obj_delete(v);
    lv_xml_component_unregister("if_st");
}
```

Add two more components/cases in the same file:
- `if: static false -> false-body only` (value=`0`): `t` absent, `f` present.
- `if: no else, static false -> nothing, component still loads` (`<if cond='c gt 0'><lv_obj name='t'/></if>`, value=`0`): `t` absent, `root` present, child count 0.

- [ ] **Step 2: Run to verify they fail**

Run: `make test && ./build/bin/helix-tests "[if]"`
Expected: FAIL — `<if>`/`<else>` are unregistered tags (loud ERROR + no bodies), so `t`/`f` lookups behave wrong.

- [ ] **Step 3: Add `else_split`/`has_else` to the capture struct**

In `lib/helix-xml/src/xml/lv_xml_parser.h`, inside `xml_frag_capture_t` (after `event_cap`):

```c
    /* <if> only: index into events[] where the false-body begins (the <else> split).
     * has_else=false => the whole buffer is the true-body; false-body is empty. */
    uint32_t else_split;
    bool     has_else;
    bool     is_if;              /* distinguishes an <if> capture from a <repeat> capture */
    char *   cond_raw;           /* <if> cond attr (owned); NULL for <repeat> */
```

- [ ] **Step 4: Recognize `<if>` start + `<else>` split in `view_start_element_handler`**

In `lv_xml.c`, alongside the `<repeat>` start block (line ~1862), BEFORE the "buffer body events while capturing" block:

```c
    if(lv_streq(name, "if") && (cap == NULL || !cap->replaying)) {
        const char * cnd = lv_xml_get_value_of(attrs, "cond");
        if(cnd == NULL) LV_LOG_WARN("<if> is missing the required 'cond' attribute; treated as false");
        cap = lv_zalloc(sizeof(xml_frag_capture_t));
        if(cap == NULL) { LV_LOG_ERROR("OOM: <if> capture; tree may be corrupt"); return; }
        cap->active = true;
        cap->is_if = true;
        cap->base_depth = (uint32_t)lv_ll_get_len(&state->parent_ll);
        if(cnd) { size_t l = lv_strlen(cnd); cap->cond_raw = lv_malloc(l + 1);
                  if(cap->cond_raw) lv_memcpy(cap->cond_raw, cnd, l + 1); }
        state->context = cap;
        return;                          /* <if> creates no object, no stack push */
    }
```

Then, inside the existing `if(cap && cap->active && !cap->replaying)` buffering block (line ~1888), intercept `<else>` at the fragment's own child level BEFORE buffering:

```c
    if(cap && cap->active && !cap->replaying) {
        if(cap->is_if && lv_streq(name, "else")) {
            if(cap->has_else) {
                LV_LOG_WARN("<if> has more than one <else>; the first split wins");
            } else {
                cap->else_split = cap->event_count;   /* false-body starts here */
                cap->has_else = true;
            }
            return;                      /* skip the marker's OWN start event */
        }
        xml_frag_buffer_event(cap, /*kind=*/0, name, attrs);
        return;
    }
```

> **Stray `<else>` outside any `<if>`:** if control reaches the normal element path with `name=="else"` (no active `<if>` capture), warn and create nothing. Add right after the buffering block, before the pcdata push:
> ```c
> if(lv_streq(name, "else")) { LV_LOG_WARN("<else> outside <if>; ignored"); return; }
> ```

- [ ] **Step 5: Handle `<else>` end + `</if>` finalize in `view_end_element_handler`**

In `lv_xml.c`, inside the `if(cap && cap->active && !cap->replaying)` block (line ~2011), add the `<else>` end skip and the `</if>` finalize BEFORE the `<repeat>` close:

```c
        if(cap->is_if && lv_streq(name, "else")) {
            return;                      /* skip the marker's OWN end event; split already taken */
        }
        if(cap->is_if && lv_streq(name, "if") && depth == cap->base_depth) {
            /* Evaluate cond. Static path only in this task: expand the selected slice once. */
            lv_xml_expr_t * ex = cap->cond_raw
                ? lv_xml_expr_compile(cap->cond_raw, frag_cond_resolver, &state->scope) : NULL;
            int32_t v = ex ? lv_xml_expr_eval(ex) : 0;
            uint32_t lo, hi;
            if(v != 0) { lo = 0; hi = cap->has_else ? cap->else_split : cap->event_count; }
            else       { lo = cap->has_else ? cap->else_split : cap->event_count; hi = cap->event_count; }
            xml_frag_expand(state, cap, lo, hi, /*count=*/1, NULL, NULL);
            if(ex) lv_xml_expr_free(ex);
            xml_frag_capture_free(cap);
            state->context = NULL;
            return;
        }
```

> Task 4 replaces the "static path only" branch with the reactive split (retain + bind when the cond references subjects).
> Add `xml_frag_capture_free` handling of the new `cond_raw` field: `if(cap->cond_raw) lv_free(cap->cond_raw);` inside `xml_frag_capture_free`.
> Add the resolver near the other resolvers in `lv_xml.c`:
> ```c
> static lv_subject_t * frag_cond_resolver(void * ctx, const char * name) {
>     return lv_xml_get_subject((lv_xml_component_scope_t *)ctx, name);
> }
> ```

- [ ] **Step 6: Add both-spellings + edge tests**

Append to `test_xml_if_else.cpp`:
- `if: <else/> and <else></else> produce identical split` — two components differing only in the else spelling, both with a false value; assert `t` absent and `f` present in both, and with a true value assert `t` present and `f` absent in both.
- `if: second <else> — first split wins` — `<if cond='c gt 0'><lv_obj name='t'/><else/><lv_obj name='f1'/><else/><lv_obj name='f2'/></if>` with value 0: assert `f1` AND `f2` both present (both are after the first split), `t` absent.
- `if: stray <else/> outside any <if> — warn + ignore, component loads` — a component with a bare `<else/>` under root; assert the component registers and creates, root present, no crash.

- [ ] **Step 7: Run tests (ASAN)**

Run: `make test-run 2>&1 | tail -20` then `./build/bin/helix-tests "[if]"`
Expected: PASS, ASAN-clean.

- [ ] **Step 8: Commit**

```bash
git add lib/helix-xml/src/xml/lv_xml.c lib/helix-xml/src/xml/lv_xml_parser.h tests/unit/test_xml_if_else.cpp
git commit -m "feat(xml): <if>/<else> tag recognition, split capture, static expansion"
```

---

### Task 4: `<if>` reactive trigger + crash-class lifetime (the MAJOR core)

Wire a subject-referencing `cond` to `lv_xml_expr_bind`, rebuilding the selected slice on any operand change; integrate the bind into the `frag_ll` unregister sweep via `lv_xml_expr_unbind` so it is safe when a component is unregistered while an instance is alive (ESP32 reclaim path).

**Files:**
- Modify: `lib/helix-xml/src/xml/lv_xml_component_private.h` (add `void * bind;` to `xml_frag_record_t`)
- Modify: `lib/helix-xml/src/xml/lv_xml.c` (reactive branch in `</if>`; `if_cond_changed_cb`; `xml_frag_record_free_heap`/`lv_xml_frag_record_free` detach the bind)
- Modify: `lib/helix-xml/src/xml/lv_xml_component.c` (no change if the sweep already calls `lv_xml_frag_record_free`; verify)
- Test: `tests/unit/test_xml_if_else.cpp`

**Interfaces:**
- Consumes: `lv_xml_expr_bind`/`lv_xml_expr_unbind` (Task 1), `xml_frag_retain`/`xml_frag_rebuild` (Task 2), `frag_cond_resolver` (Task 3).
- Produces: reactive `<if>`. `if_cond_changed_cb(void * record, int32_t value)` selects the slice and calls `xml_frag_rebuild(r, lo, hi, 1)`.

- [ ] **Step 1: Write the failing reactive + crash-class tests**

Append to `test_xml_if_else.cpp`. Mirror `test_xml_repeat_subject_count.cpp` conventions (`process_lvgl(50)` between changes, global subject in a namespace, delete-then-unregister). Cases:

```cpp
static const char * COMP_REACTIVE =
  "<component><subjects><subject name='c' type='int' value='1'/></subjects>"
  "  <view><lv_obj name='root'>"
  "    <if cond='c gt 0'><lv_obj name='t'/><else/><lv_obj name='f'/></if>"
  "  </lv_obj></view></component>";

TEST_CASE_METHOD(LVGLTestFixture, "if: reactive flip true->false->true", "[xml][if]") {
    REQUIRE(lv_xml_register_component_from_data("if_rx", COMP_REACTIVE) == LV_RESULT_OK);
    lv_xml_component_scope_t * scope = lv_xml_component_get_scope("if_rx");
    lv_obj_t * v = (lv_obj_t *)lv_xml_create(lv_screen_active(), "if_rx", nullptr);
    lv_subject_t * c = lv_xml_get_subject(scope, "c");
    REQUIRE(lv_obj_find_by_name(v, "t") != nullptr);
    REQUIRE(lv_obj_find_by_name(v, "f") == nullptr);

    lv_subject_set_int(c, 0); process_lvgl(50);
    REQUIRE(lv_obj_find_by_name(v, "t") == nullptr);
    REQUIRE(lv_obj_find_by_name(v, "f") != nullptr);

    lv_subject_set_int(c, 1); process_lvgl(50);
    REQUIRE(lv_obj_find_by_name(v, "t") != nullptr);
    REQUIRE(lv_obj_find_by_name(v, "f") == nullptr);

    lv_obj_delete(v);
    lv_xml_component_unregister("if_rx");
}
```

Plus:
- `if: multi-subject cond re-evaluates on any operand` — `cond='a and b gt c'` with three subjects; change each and assert the body flips as expected (proves `_bind` multi-subject).
- `if: rapid churn coalesces` — three `lv_subject_set_int` before one drain; final state correct, ASAN-clean.
- `if: cond change AFTER instance delete does not UAF` — GLOBAL cond subject, component stays registered; `lv_obj_delete(v)`, drain, then `lv_subject_set_int` on the global, drain; `SUCCEED` (observer detached by instance-delete → bind delete cb).
- `if: GLOBAL cond subject removed on unregister (no UAF)` — GLOBAL subject; delete instance, drain, unregister, then change the global; ASAN-clean.
- **`if: unregister WHILE instance alive detaches cond observers (no UAF)`** — the strengthened test that actually exercises approach (a): GLOBAL cond subject; create instance; **do NOT delete it**; `lv_xml_component_unregister(...)` while `v` is still alive; then `lv_subject_set_int` on the global + drain; ASAN-clean. Then `lv_obj_delete(v)` must not double-free. This is the test the spec's original test 8 did not cover.
- `if: re-instantiation does not accumulate stale observers` — create v1, delete v1, create v2, change cond; only v2 flips; ASAN-clean.

- [ ] **Step 2: Run to verify they fail**

Run: `make test && ./build/bin/helix-tests "[if]"`
Expected: reactive cases FAIL — the `</if>` path currently only does the static one-shot; changing the subject does nothing.

- [ ] **Step 3: Add the `bind` field to the record**

In `lv_xml_component_private.h`, `xml_frag_record_t` (after `parent_attrs`):

```c
    void * bind;   /* <if> only: lv_xml_expr_bind_t* handle; detached in the frag sweep. NULL for <repeat>. */
```

- [ ] **Step 4: Replace the `</if>` static branch with static-vs-reactive**

In `lv_xml.c`, the `</if>` finalize (Task 3 Step 5). Compile once, then branch on whether the expr references subjects:

```c
        if(cap->is_if && lv_streq(name, "if") && depth == cap->base_depth) {
            lv_xml_expr_t * ex = cap->cond_raw
                ? lv_xml_expr_compile(cap->cond_raw, frag_cond_resolver, &state->scope) : NULL;

            if(ex && lv_xml_expr_subject_count(ex) > 0) {
                /* Reactive: retain the capture and bind the cond to the instance root. */
                xml_frag_record_t * r = xml_frag_retain(state, cap);   /* clears state->context */
                if(r) {
                    r->bind = lv_xml_expr_bind(ex, r->view_root, if_cond_changed_cb, r);
                    /* _bind's immediate fire performs the initial build via if_cond_changed_cb. */
                    return;
                }
                /* retain failed: fall through to a one-shot at the current value */
                LV_LOG_WARN("<if> reactive retain failed; expanding once, non-reactive");
            }

            int32_t v = ex ? lv_xml_expr_eval(ex) : 0;
            uint32_t lo, hi;
            if(v != 0) { lo = 0; hi = cap->has_else ? cap->else_split : cap->event_count; }
            else       { lo = cap->has_else ? cap->else_split : cap->event_count; hi = cap->event_count; }
            xml_frag_expand(state, cap, lo, hi, 1, NULL, NULL);
            if(ex) lv_xml_expr_free(ex);
            xml_frag_capture_free(cap);
            state->context = NULL;
            return;
        }
```

> **Ownership note:** in the reactive branch `lv_xml_expr_bind` takes ownership of `ex` (frees it on owner delete / unbind) — do NOT `lv_xml_expr_free(ex)` there. In the static branch we own `ex` and free it. `xml_frag_retain` moved the capture into the record, so the reactive branch must NOT `xml_frag_capture_free(cap)`.

Add the callback near `xml_frag_rebuild`:

```c
/* <if> reactive trigger: pick the true/false slice from the new cond value and
 * rebuild it (count=1). Runs on the main thread inside a UpdateQueue drain
 * (subject-set path) and via _bind's immediate fire for the initial build. */
static void if_cond_changed_cb(void * record, int32_t value)
{
    xml_frag_record_t * r = (xml_frag_record_t *)record;
    xml_frag_capture_t * cap = (xml_frag_capture_t *)r->capture;
    uint32_t lo, hi;
    if(value != 0) { lo = 0; hi = cap->has_else ? cap->else_split : cap->event_count; }
    else           { lo = cap->has_else ? cap->else_split : cap->event_count; hi = cap->event_count; }
    xml_frag_rebuild(r, lo, hi, 1);
}
```

- [ ] **Step 5: Detach the bind in the record teardown paths**

The two teardown paths must handle `r->bind`:

- **Instance-delete path** (`xml_frag_instance_delete_cb` → `xml_frag_record_free_heap`): when the instance view root is deleted, `_bind`'s own `expr_bind_delete_cb` (also on view_root LV_EVENT_DELETE) frees the bind. So `xml_frag_record_free_heap` must **NOT** call `_unbind` (double-free). Set `r->bind = NULL` there without unbinding — the bind cb owns that path. Add to `xml_frag_record_free_heap`:
  ```c
      r->bind = NULL;   /* on instance delete, expr_bind_delete_cb frees the bind itself */
  ```
- **Unregister sweep path** (`lv_xml_frag_record_free`, called from the `frag_ll` walk BEFORE `subjects_ll` teardown): the instance is still alive, so `_bind`'s delete cb has NOT fired and the observers still sit on subjects about to be freed. Detach here. In `lv_xml_frag_record_free`, BEFORE `xml_frag_record_free_heap(r)`:
  ```c
      if(r->bind) { lv_xml_expr_unbind((lv_xml_expr_bind_t *)r->bind); r->bind = NULL; }
  ```
  (`_unbind` removes the observers AND the `expr_bind_delete_cb` from `r->view_root`, so the later `lv_obj_delete(v)` does not double-free. `lv_xml_frag_record_free` already removes `xml_frag_instance_delete_cb` from the view root.)

> Include `lv_xml_expr.h` in `lv_xml.c` and `lv_xml_component.c` if not already (grep first).

- [ ] **Step 6: Run tests (ASAN) — the crash-class gate**

Run: `make test-run 2>&1 | tail -30` then `./build/bin/helix-tests "[if]" -s`
Expected: ALL `[if]` cases PASS, ASAN-clean — especially the unregister-while-alive test. Re-run `[repeat]` to confirm the shared core still passes: `./build/bin/helix-tests "[repeat]"`.

- [ ] **Step 7: Commit**

```bash
git add lib/helix-xml/src/xml/lv_xml.c lib/helix-xml/src/xml/lv_xml_component_private.h tests/unit/test_xml_if_else.cpp
git commit -m "feat(xml): <if> reactive rebuild via lv_xml_expr_bind + unregister-safe cleanup"
```

---

### Task 5: Schema + linter registration

Register `<if>`/`<else>` so the XML linter accepts them and treats `cond` as a free-form expression.

**Files:**
- Modify: `tools/xml-linter/schema/extract_schema.py:865-883`
- Regenerate: `tools/xml-linter/schema/schema.json`

- [ ] **Step 1: Register both tags**

In `extract_schema.py`, next to the `<repeat>` registration (line 868), add:

```python
    # <if cond="expr">...<else/>...</if> -- load-time conditional handled in
    # lv_xml.c view handlers (capture/replay), not via lv_xml_register_widget().
    schema["widgets"]["if"] = {
        "attributes": {
            "cond": {"type": "string"},
        },
    }
    schema["widgets"]["else"] = {
        "attributes": {},
    }
```

And extend the special_elements loop (line 881):

```python
    for tag in ("subject_expr", "repeat", "if", "else"):
        if tag not in schema["special_elements"]:
            schema["special_elements"].append(tag)
```

- [ ] **Step 2: Regenerate and inspect the diff**

Run: `make regen-xml-schema && git diff --stat tools/xml-linter/schema/schema.json`
Expected: `schema.json` gains `if`/`else` widget entries + special_elements members.

- [ ] **Step 3: Verify the crossref linter accepts `cond` on `<if>`**

Run the linter's own tests and lint a sample:
```bash
cd tools/xml-linter && python -m pytest -q && cd -
```
Write a throwaway `ui_xml/_iftest.xml` component using `<if cond='some_subject gt 0'>…<else/>…</if>` and run the project's XML lint target on it (grep the Makefile for the lint target, e.g. `make xml-lint`). Expected: no `UNKNOWN_SUBJECT_REF` on the `cond` expression (it is expression-validated like the existing `cond=` on `bind_flag_if`), no "unknown widget type" for `<if>`/`<else>`. Delete `_iftest.xml` after.

> If the crossref linter flags `cond` on `<if>` as an unknown subject (it currently special-cases `cond=` on the `bind_*_if` tags), teach it to treat `<if>`'s `cond` as an expression the same way. Grep `cond` in `tools/xml-linter/` and mirror the existing handling.

- [ ] **Step 4: Commit**

```bash
git add tools/xml-linter/schema/extract_schema.py tools/xml-linter/schema/schema.json
git commit -m "chore(xml-linter): register <if>/<else> tags + cond expression attr"
```

---

### Task 6: Documentation

Ship docs with the code.

**Files:**
- Modify: `docs/devel/LVGL9_XML_GUIDE.md`
- Modify: `docs/devel/LVGL9_XML_ATTRIBUTES_REFERENCE.md`
- Modify: `.claude/skills/helix-xml/references/xml-guide.md`, `.claude/skills/helix-xml/references/xml-attributes.md`
- Modify: `CLAUDE.md` (§ CRITICAL RULES - Declarative UI)

- [ ] **Step 1: `LVGL9_XML_GUIDE.md` — new `<if>`/`<else>` section**

Add a section modeled on the existing `<repeat>` section. Cover: syntax with both `<else/>` and `<else></else>`; inline-divider semantics (everything before `<else>` is true-body, everything after is false-body, `</if>` terminates); **when to prefer `<if>`** (expensive/structural conditional *creation*) **vs `bind_flag`/`cond=`** (cheap show/hide of light subtrees — those stay); the **ordering ⚠️** (a reactively-rebuilt body with later same-parent siblings mis-orders on rebuild — put `<if>` last or in its own container, same as `<repeat>`); static-vs-reactive (a cond with no subjects expands once at load, no observer). Pull the example from `test_xml_if_else.cpp` so it is real.

- [ ] **Step 2: `LVGL9_XML_ATTRIBUTES_REFERENCE.md` — rows**

Add an `<if>` row (`cond` — expression string, word-form operators) and an `<else>` row (no attributes, inline divider).

- [ ] **Step 3: Mirror into the skill references**

Copy the same `<if>`/`<else>` guide section and attribute rows into `.claude/skills/helix-xml/references/xml-guide.md` and `xml-attributes.md` (these mirror the devel docs).

- [ ] **Step 4: `CLAUDE.md` — extend the imperative-visibility rule**

In § "CRITICAL RULES - Declarative UI", extend row 2 (or add a note under it): `<if>` is the structural sibling of `bind_flag hidden` — use `<if>` for expensive/structural conditional *creation*, `bind_flag`/`cond=` for cheap show/hide.

- [ ] **Step 5: Commit**

```bash
git add docs/devel/LVGL9_XML_GUIDE.md docs/devel/LVGL9_XML_ATTRIBUTES_REFERENCE.md .claude/skills/helix-xml/references/xml-guide.md .claude/skills/helix-xml/references/xml-attributes.md CLAUDE.md
git commit -m "docs(xml): document <if>/<else> structural conditionals"
```

---

## Self-Review

- **Spec coverage:** tag recognition + capture (T3), `xml_frag_*` core (T2), reactive trigger (T4), numeric-attr interplay (none needed — bodies replay through normal handlers, T3/T4), cross-cutting risk approach (a) (T1 + T4), all 13 spec tests (static 1-3 → T3; reactive 4-6 → T4; crash-class 7-9 → T4 incl. the strengthened unregister-while-alive test; else edges 10-12 → T3; regression gate 13 → T2 Steps 1/6), schema/linter (T5), docs (T6). Covered.
- **Type consistency:** `xml_frag_capture_t`/`xml_frag_record_t`/`frag_ll`/`xml_frag_expand(state,cap,lo,hi,count,out,outn)`/`xml_frag_rebuild(r,lo,hi,count)`/`lv_xml_frag_record_free`/`lv_xml_expr_bind_t`/`if_cond_changed_cb(void*,int32_t)`/`frag_cond_resolver` used consistently across tasks.
- **Known unknowns flagged for the implementer, not left as placeholders:** (1) the exact observer-removal primitive in `_unbind` (Task 1 Step 4 note — retained-array form is the safe default; grep before choosing); (2) whether the crossref linter needs teaching for `<if cond>` (Task 5 Step 3 — verified empirically, taught if needed). Both are "verify against the code" instructions with a concrete fallback, not vague TODOs.

## Execution Handoff

Plan complete. Recommended execution: **Subagent-Driven** (superpowers:subagent-driven-development) — fresh implementer per task, task review after each, broad review at the end. Task 2 (rename) and Task 4 (crash-class) are the ones to review hardest.
