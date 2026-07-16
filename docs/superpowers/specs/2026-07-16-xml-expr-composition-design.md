# `${expr}`: Integer-Expression Composition & Numeric Attributes

**Date:** 2026-07-16
**Status:** Design — approved in brainstorming; ready for implementation plan.
**Scope:** MAJOR (parser capability in `lib/helix-xml/`). Feature 1 of the three in
`docs/superpowers/specs/2026-07-16-xml-parser-enhancements-design.md`. Independent; ships on its own.
**Branch / worktree:** `feature/xml-expr-composition` → `.worktrees/xml-expr-composition`.

## Summary

Today `${name}` inside an attribute value is pure string substitution: `${i}` splices the
`<repeat>` loop index, `${param}` splices a component-param string. There is no way to compute
a value inline — `${i + 1}` currently splices empty and warns.

This feature upgrades `${…}` so that a token containing an expression is evaluated as an
**integer expression** (via the existing `lv_xml_expr` evaluator) and its result spliced as text.
A single bare identifier keeps today's string-substitution behavior unchanged, so the change is
fully backward compatible.

```xml
<repeat count="4">
  <slot_card bind_text="slot_${i + 1}_label"   <!-- slot_1_label … slot_4_label -->
             style_translate_x="${i * 84}"/>    <!-- 0, 84, 168, 252 -->
</repeat>

<gauge width="${base * scale}"/>                 <!-- subjects: evaluated ONCE at creation -->
<grid_cell x="${cols * 2}"/>                      <!-- numeric param: cols="4" -> 8 -->
```

## Decisions (locked in brainstorming)

1. **Bare-name syntax inside `${…}`.** Expressions reference the index and subjects by bare name:
   `${i + 1}`, `${base * scale}`. No inner `$` sigil. Consistent with the existing `${i}` / `${param}`.
2. **Resolve-once, not reactive.** A `${…}` expression is evaluated exactly once, at widget-creation
   time, and the integer result is spliced as text. Subject operands are read once; if a subject
   changes later, the composed value does **not** update on its own (a `<repeat count="subject">`
   rebuild re-runs composition and refreshes it; a standalone attr does not). Reactive computed
   numeric attributes are a deliberately deferred, separately-shaped follow-up — see Non-goals.
   **This contract must be documented prominently and locked by a test.**
3. **Param operands included.** A numeric component-param / parent-attr string (e.g. `cols="4"`)
   is a valid expression operand: `${cols * 2}` → `8`. A non-numeric param used in arithmetic
   (`${grp * 2}` where `grp="fan"`) fails to compile → warn + splice empty (correct user-error behavior).

## Engine facts this design rests on (verified against the code)

All line refs are `lib/helix-xml/src/xml/`.

- **`${…}` is handled in `xml_compose_indexed` (`lv_xml.c:1666`)** via per-token `xml_compose_lookup`
  (`lv_xml.c:1648`). Composed strings are `lv_malloc`'d, tracked on `state->composed_strings`, and
  freed once per parse / per reactive rebuild in `xml_state_free_composed` (`lv_xml.c:1740`). The
  `${…}` compose branch in `resolve_params` (`lv_xml.c:962`) is the **only allocating branch**.
- **`xml_value_has_compose` (`lv_xml.c:1634`)** already gates on `$` immediately followed by `{`, so a
  bare `$i` / `$name` never enters the compose path — no change needed there.
- **Compose runs before every widget `apply_cb`.** `resolve_params` at `lv_xml.c:1831` → `apply_cb`
  at `lv_xml.c:1848`. Numeric attributes are parsed per-attribute inside each widget parser
  (`lv_xml_to_size` / `lv_xml_atoi`, e.g. `lv_xml_obj_parser.c:95-102`) from the already-composed
  string. **So composing `${i * 84}` to the literal text `"336"` is sufficient — no setter or
  choke-point change is required for numeric attributes.**
- **The evaluator (`lv_xml_expr.{c,h}`)** resolves each identifier to an `lv_subject_t*` via its
  resolver typedef `lv_subject_t *(*)(void *ctx, const char *name)` (`lv_xml_expr.h:17`); the int is
  read at eval via `lv_subject_get_int`. It supports word forms (`and/or/not/eq/ne/lt/le/gt/ge`),
  degrades div/mod-by-zero to `0`+warn, and returns NULL from `_compile` on unknown identifier /
  malformed input. Cap: `LV_XML_EXPR_MAX_TOKENS = 128`, ≤ 32 distinct subjects.
- **The loop index `i` is a plain `int32_t`** (`current_index` on `lv_xml_repeat_capture_t`, hung off
  `state->context` during replay; `lv_xml_parser.h:81`). It is **not** a subject, so the evaluator
  cannot see it directly — the resolver must bridge it (see below).
- **Existing resolver patterns to mirror:** `subject_expr_resolver` (`lv_xml_component.c:604`),
  `cond_flag_scope_resolver` (`lv_xml_obj_parser.c:797`) — both `return lv_xml_get_subject(scope, name)`.

## Design

### Disambiguation: name-substitution vs expression

In `xml_compose_indexed`, for each extracted `${TOKEN}`:

- **`TOKEN` is a single bare identifier** — matches `^[A-Za-z_][A-Za-z0-9_]*$` (no spaces, no
  operators, no digits-leading). Route to the **existing** `xml_compose_lookup` path, unchanged:
  `i` → loop index; else parent-attr / param-default string. This preserves `${i}`, `${grp}`,
  `demo_${i}_v`, `${prefix}_label` exactly.
- **Anything else** — route to the **integer-expression path**: compile `TOKEN` with
  `lv_xml_expr_compile`, evaluate, `lv_snprintf("%d", …)` the `int32_t` result into a scratch buffer,
  and splice it via the same `XML_COMPOSE_APPEND` machinery.

The test is purely syntactic — a small helper `xml_token_is_bare_identifier(const char *)`. No
"try-eval-then-fall-back-to-string" ambiguity: a plain name is always a name; a token with operators
is always math. `${5}` (a bare integer literal, not an identifier) routes to the evaluator and yields
`5`.

### Bridging `i` and numeric params into the evaluator

Introduce a compose-site resolver plus a small ctx struct, both local to `lv_xml.c`:

```c
typedef struct {
    lv_xml_parser_state_t * state;        /* for scope + parent_attrs / param defaults */
    lv_xml_repeat_capture_t * rc;         /* NULL when not inside a replaying <repeat> */
    lv_subject_t index_subject;           /* int-subject seeded to rc->current_index */
    lv_subject_t param_subjects[LV_XML_EXPR_MAX_SUBJECTS]; /* transient numeric-param operands */
    uint32_t param_count;
} xml_compose_expr_ctx_t;

static lv_subject_t * xml_compose_expr_resolver(void * vctx, const char * name)
{
    xml_compose_expr_ctx_t * c = vctx;
    /* 1. loop index */
    if(lv_streq(name, "i")) return (c->rc && c->rc->replaying) ? &c->index_subject : NULL;
    /* 2. numeric component-param / parent-attr -> transient int subject */
    const char * pv = /* parent_attrs lookup, then get_param_default, mirroring xml_compose_lookup */;
    if(pv && xml_str_is_integer(pv)) {
        if(c->param_count >= LV_XML_EXPR_MAX_SUBJECTS) return NULL;
        lv_subject_t * s = &c->param_subjects[c->param_count++];
        lv_subject_init_int(s, (int32_t)lv_xml_strtol(pv, ...));
        return s;
    }
    /* 3. real scope subject */
    return lv_xml_get_subject(&c->state->scope, name);
    /* else NULL -> compile fails -> warn + splice empty */
}
```

Lifetime is trivial because of the resolve-once contract: **compile → eval → free all happen inside
the single compose call.** `index_subject` and the `param_subjects[]` are stack-lifetime; seed
`index_subject` to `rc->current_index` before compiling, init each param-subject as the resolver
encounters it, then after `lv_xml_expr_eval` returns: `lv_xml_expr_free(expr)`, `lv_subject_deinit`
every used param-subject (and `index_subject`). **No observer, no instance-lifetime record, no
capture-struct change, no exposure to the `<repeat>` reactive-teardown crash class.**

`LV_XML_EXPR_MAX_SUBJECTS` is the evaluator's existing 32 cap (reuse or name a shared constant).

### Numeric attributes — no change

Confirmed above: composing to text upstream fully covers numeric attributes. `width`, `height`,
`x`, `y`, `style_translate_x`, `flex_grow`, etc. all parse the already-composed string in their own
`apply_cb`. Nothing to touch.

### Error handling

Reuse the evaluator's existing degrade path. Unknown identifier / malformed expression /
non-numeric param operand → `lv_xml_expr_compile` returns NULL → warn + splice empty, matching
today's behavior for an unresolved `${name}`. Div/mod-by-zero → `0` + warn (evaluator-internal).
No aborts, no crashes. Generalize the existing `LV_LOG_WARN` wording away from the `<repeat>`-only
phrasing (these tokens now appear outside repeats too), e.g. `"${%s} could not be evaluated in
'%s'; splicing empty"`.

## Testing

`tests/unit/`, auto-globbed, `TEST_CASE_METHOD(LVGLTestFixture, …)` +
`lv_xml_register_component_from_data`; model on `test_xml_indexed_subject.cpp` and `test_xml_expr.cpp`.
New file `tests/unit/test_xml_expr_compose.cpp`:

1. **Index arithmetic in a name** — `bind_text="slot_${i + 1}_label"` in a `<repeat count="4">`
   yields `slot_1_label … slot_4_label` (assert per-child label).
2. **Computed numeric attr** — `style_translate_x="${i * 84}"` yields `0, 84, 168, 252`
   (assert `lv_obj_get_style_translate_x`).
3. **Equivalence** — `${i}` (string path) and `${i + 0}` (expr path) produce identical output.
4. **Subject operand, resolve-once contract** — register subjects `base`, `scale`; component uses
   `width="${base * scale}"`; assert the spliced value; then `lv_subject_set_int` a new value,
   `process_lvgl(...)`, and **assert the width did NOT change**. This test *is* the resolve-once
   guarantee — it must fail if anyone later makes it reactive without a spec change.
5. **Numeric param operand** — component param `cols="4"`, body uses `${cols * 2}` → `8`.
6. **Non-numeric param in arithmetic** — `grp="fan"`, body uses `${grp * 2}` → empty + warn, no crash.
7. **Malformed / unknown** — `${i + }`, `${bogus_ident}` → empty + warn, no crash.
8. **Backward compatibility** — bare `${i}` still splices the index; bare `${grp}` still splices the
   param string; `demo_${i}_v` still resolves (the existing `test_xml_indexed_subject.cpp` cases
   must continue to pass unchanged).

No `[slow]`/thread or ASAN-teardown tests are required — this feature adds **no observers and no
async teardown**. (That is the payoff of resolve-once and is worth calling out in the plan so no one
adds crash-class scaffolding this feature doesn't need.)

## Docs (ship with the code, not after)

- `docs/devel/LVGL9_XML_GUIDE.md` — upgrade the `${name}` sigil section from "index only" to
  "integer expression"; add the syntax (`${i + 1}`, `${base * scale}`, `${cols * 2}`); operand
  rules (i / integer literals / numeric params / subjects; word forms); and a **prominent
  resolve-once ⚠️ callout** (subject operands read once at creation, not reactive; for a live value
  use `bind_*` or a `<repeat count="subject">` rebuild).
- `docs/devel/LVGL9_XML_ATTRIBUTES_REFERENCE.md` — same sigil-row upgrade; remove any
  "arithmetic is a follow-up" caveat.
- `.claude/skills/helix-xml/` — mirror the guide upgrade + resolve-once note (per the parent spec's
  "ship docs with the code, including the skill").

## Tooling

- **No new tag or attribute** — `${…}` is a value-level construct, so `extract_schema.py` needs no
  new `widgets` / `special_elements` entry and no schema regen (verify with a schema diff that it is
  in fact empty).
- Verify the crossref linter (`scripts/`) still skips `${…}` contents and does not false-warn on the
  expression operators/spaces now legal inside them (per the parent spec it already skips `${…}`;
  confirm with the new expression forms).

## Non-goals (deferred, and why)

- **Float expressions** — the engine is integer-only.
- **Reactive computed numeric attributes** — `width="${base * scale}"` staying live on subject change.
  The observer half is cheap (the evaluator already binds subjects), but re-applying a numeric attr
  has no generic setter in the XML layer, so it would mean a per-attr setter dispatch (effectively new
  `bind_width`/`bind_style_*` numeric bindings) plus instance-lifetime observers and the associated
  crash-class tests — a separate, differently-shaped feature. Deferred; revisit as its own spec if a
  concrete case appears.
- **Lone bare-subject splice** — `${base}` alone remains a *name* lookup (index/param), not a subject
  read. To use a subject numerically, reference it inside an expression (e.g. `${base * 1}`) or use a
  dedicated binding. Deferred with the reactive item above.
- **Nested `<repeat>` / a second index (`j`)** — already deferred upstream; `i` is the only index.

## Risks

- **`resolve_params` ordering / the only allocating branch.** The change is confined to the compose
  branch; preserve compose-first ordering and the `xml_state_free_composed` free-at-parse-end
  discipline. The transient param-subjects are deinit'd inside the compose call (not tracked on
  `state`), so they add no new free-path obligation.
- **Backward-compat regression.** The bare-identifier fast path must be exercised by the existing
  `test_xml_indexed_subject.cpp` cases and the new backward-compat test — a mis-scoped sniff that
  routes `${grp}` (a string param) to the evaluator would break existing components.
- **Resolve-once drift.** Test #4 exists precisely to prevent a future well-meaning change from
  silently making composition reactive.
