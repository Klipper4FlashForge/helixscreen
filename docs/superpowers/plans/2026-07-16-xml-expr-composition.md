# `${expr}` Integer-Expression Composition — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let `${…}` inside an XML attribute value evaluate an integer expression (`${i + 1}`, `${base * scale}`, `${cols * 2}`) and splice the result as text, while a bare `${name}` keeps today's string-substitution behavior unchanged.

**Architecture:** In `xml_compose_indexed` (the existing `${…}` handler in `lib/helix-xml/src/xml/lv_xml.c`), each extracted token is classified: a single bare identifier takes the legacy `xml_compose_lookup` path; anything else is compiled and evaluated by the existing `lv_xml_expr` integer evaluator via a compose-site resolver that maps `i` → loop index, numeric params → transient int subjects, and other names → scope subjects. The compiled expression is evaluated once and freed within the same compose call — **resolve-once, no observers, no async teardown**.

**Tech Stack:** Pure C (LVGL 9.5 XML engine, `lib/helix-xml/`, direct-edit, not clang-formatted). Catch2 unit tests via `LVGLTestFixture` (`tests/unit/*.cpp`, auto-globbed). Existing evaluator `lv_xml_expr.{c,h}`.

## Global Constraints

- **Pure C, no app layer.** `lib/helix-xml/` MUST NOT include or call app-layer C++ (`include/ui_utils.h`, `helix::ui::*`). LVGL C primitives only.
- **Not clang-formatted.** `lib/helix-xml/` is excluded from clang-format — match surrounding style by hand (4-space indent, brace style as in the file).
- **spdlog rule does not apply here** — this is LVGL C; use `LV_LOG_WARN`, matching the existing compose-path warnings.
- **Integer-only engine.** No floats anywhere.
- **Resolve-once contract.** A `${…}` expression is evaluated exactly once at creation. No observers, no reactivity, no instance-lifetime records. This is a hard requirement and is locked by a test.
- **Build:** `make test-run` builds and runs tests. Run a single tag with `./build/bin/helix-tests "[tag]"`. Check for a running build first (`pgrep -x cc1plus`); do not stack parallel builds.
- **Commit style:** double-quoted `-m`; scope `xml`; no session references in code comments.

## File Structure

- **Modify** `lib/helix-xml/src/xml/lv_xml.c` — add two small static helpers (`xml_token_is_bare_identifier`, `xml_str_is_integer`), a compose-site resolver + ctx (`xml_compose_expr_ctx_t` / `xml_compose_expr_resolver`), and route non-bare `${…}` tokens through the evaluator inside `xml_compose_indexed`. Ensure `lv_xml_expr.h` is included. Generalize the compose warn wording (tokens now appear outside `<repeat>`). Enlarge the token buffer so expressions longer than a subject name fit.
- **Create** `tests/unit/test_xml_expr_compose.cpp` — the new behavior's tests.
- **Modify** docs: `docs/devel/LVGL9_XML_GUIDE.md`, `docs/devel/LVGL9_XML_ATTRIBUTES_REFERENCE.md`, `.claude/skills/helix-xml/references/xml-attributes.md`, `.claude/skills/helix-xml/references/xml-guide.md`.

---

## Task 1: Bare-identifier sniff + integer-expression evaluation (index & subject operands)

Delivers the core: `${i + 1}` composes arithmetic into a name, `${i * 84}` computes a numeric attribute, subjects work as operands (resolve-once), malformed degrades to empty+warn, and every existing `${…}` case still works.

**Files:**
- Modify: `lib/helix-xml/src/xml/lv_xml.c` (helpers + resolver near `xml_compose_lookup` ~`:1648`; routing inside `xml_compose_indexed` ~`:1701-1713`; include near the top with the other `lv_xml_*` includes)
- Test: `tests/unit/test_xml_expr_compose.cpp` (create)

**Interfaces:**
- Consumes (existing, verified): `lv_xml_expr_compile(const char *, lv_xml_expr_resolver_t, void *)`, `int32_t lv_xml_expr_eval(const lv_xml_expr_t *)`, `void lv_xml_expr_free(lv_xml_expr_t *)` (`lv_xml_expr.h`); `lv_subject_t * lv_xml_get_subject(lv_xml_component_scope_t *, const char *)`; `const char * lv_xml_get_value_of(const char **, const char *)`; `static const char * get_param_default(lv_xml_component_scope_t *, const char *)` (`lv_xml.c:938`); `lv_xml_repeat_capture_t` with fields `replaying` and `current_index` (`lv_xml_parser.h:81-82`); the `XML_COMPOSE_APPEND(src, n)` macro local to `xml_compose_indexed`.
- Produces (used by Task 2): `xml_compose_expr_ctx_t` struct and `xml_compose_expr_resolver` — Task 2 inserts a numeric-param branch into the resolver and populates `param_subjects[]`/`param_count`.

- [ ] **Step 1: Write the failing tests**

Create `tests/unit/test_xml_expr_compose.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include "../catch_amalgamated.hpp"
#include "../lvgl_test_fixture.h"
#include <string>
extern "C" {
#include "helix-xml/src/xml/lv_xml.h"
}

// ${i + 1}: 1-based names inside a <repeat count='4'> -> slot_1_label .. slot_4_label.
static const char * COMP_EXPR_NAME =
  "<component>"
  "  <view>"
  "    <lv_obj name='root'>"
  "      <repeat count='4'>"
  "        <lv_label name='lbl' bind_text='slot_${i + 1}_label'/>"
  "      </repeat>"
  "    </lv_obj>"
  "  </view>"
  "</component>";

TEST_CASE_METHOD(LVGLTestFixture, "expr: ${i + 1} composes 1-based subject names", "[xml][expr_compose]") {
    static char b[4][32];
    static lv_subject_t s[4];
    for (int k = 0; k < 4; k++) {
        lv_subject_init_string(&s[k], b[k], nullptr, 32, k == 0 ? "one" : k == 1 ? "two" : k == 2 ? "three" : "four");
        lv_xml_register_subject(nullptr, ("slot_" + std::to_string(k + 1) + "_label").c_str(), &s[k]);
    }
    REQUIRE(lv_xml_register_component_from_data("t_expr1", COMP_EXPR_NAME) == LV_RESULT_OK);
    lv_obj_t * v = (lv_obj_t*)lv_xml_create(lv_screen_active(), "t_expr1", nullptr);
    lv_obj_t * root = lv_obj_find_by_name(v, "root");
    REQUIRE(lv_obj_get_child_count(root) == 4);
    REQUIRE(std::string(lv_label_get_text(lv_obj_get_child(root, 0))) == "one");
    REQUIRE(std::string(lv_label_get_text(lv_obj_get_child(root, 3))) == "four");
    lv_obj_delete(v);
    lv_xml_component_unregister("t_expr1");
}

// ${i * 84}: computed numeric attribute (style_translate_x) -> 0, 84, 168, 252.
static const char * COMP_EXPR_NUM =
  "<component>"
  "  <view>"
  "    <lv_obj name='root'>"
  "      <repeat count='4'>"
  "        <lv_obj name='cell' width='10' height='10' style_translate_x='${i * 84}'/>"
  "      </repeat>"
  "    </lv_obj>"
  "  </view>"
  "</component>";

TEST_CASE_METHOD(LVGLTestFixture, "expr: ${i * 84} computes a numeric attribute", "[xml][expr_compose]") {
    REQUIRE(lv_xml_register_component_from_data("t_expr2", COMP_EXPR_NUM) == LV_RESULT_OK);
    lv_obj_t * v = (lv_obj_t*)lv_xml_create(lv_screen_active(), "t_expr2", nullptr);
    lv_obj_t * root = lv_obj_find_by_name(v, "root");
    REQUIRE(lv_obj_get_child_count(root) == 4);
    REQUIRE(lv_obj_get_style_translate_x(lv_obj_get_child(root, 0), LV_PART_MAIN) == 0);
    REQUIRE(lv_obj_get_style_translate_x(lv_obj_get_child(root, 1), LV_PART_MAIN) == 84);
    REQUIRE(lv_obj_get_style_translate_x(lv_obj_get_child(root, 3), LV_PART_MAIN) == 252);
    lv_obj_delete(v);
    lv_xml_component_unregister("t_expr2");
}

// Equivalence: ${i} (legacy name path) and ${i + 0} (expr path) yield the same value.
static const char * COMP_EXPR_EQUIV =
  "<component>"
  "  <view>"
  "    <lv_obj name='root'>"
  "      <repeat count='3'>"
  "        <lv_obj name='a' width='5' height='5' style_translate_x='${i}'/>"
  "        <lv_obj name='b' width='5' height='5' style_translate_x='${i + 0}'/>"
  "      </repeat>"
  "    </lv_obj>"
  "  </view>"
  "</component>";

TEST_CASE_METHOD(LVGLTestFixture, "expr: ${i} equals ${i + 0}", "[xml][expr_compose]") {
    REQUIRE(lv_xml_register_component_from_data("t_expr3", COMP_EXPR_EQUIV) == LV_RESULT_OK);
    lv_obj_t * v = (lv_obj_t*)lv_xml_create(lv_screen_active(), "t_expr3", nullptr);
    lv_obj_t * root = lv_obj_find_by_name(v, "root");
    REQUIRE(lv_obj_get_child_count(root) == 6);  // 2 per iteration * 3
    for (int k = 0; k < 3; k++) {
        int32_t a = lv_obj_get_style_translate_x(lv_obj_get_child(root, k * 2), LV_PART_MAIN);
        int32_t b = lv_obj_get_style_translate_x(lv_obj_get_child(root, k * 2 + 1), LV_PART_MAIN);
        REQUIRE(a == b);
        REQUIRE(a == k);
    }
    lv_obj_delete(v);
    lv_xml_component_unregister("t_expr3");
}

// Subject operands, RESOLVE-ONCE: width = base * scale is evaluated once; a later
// subject change does NOT update the width. This test locks the resolve-once contract.
static const char * COMP_EXPR_SUBJ =
  "<component>"
  "  <view>"
  "    <lv_obj name='g' width='${base * scale}' height='10'/>"
  "  </view>"
  "</component>";

TEST_CASE_METHOD(LVGLTestFixture, "expr: subject operands are resolve-once", "[xml][expr_compose]") {
    static lv_subject_t base, scale;
    lv_subject_init_int(&base, 10);
    lv_subject_init_int(&scale, 3);
    lv_xml_register_subject(nullptr, "base", &base);
    lv_xml_register_subject(nullptr, "scale", &scale);

    REQUIRE(lv_xml_register_component_from_data("t_expr4", COMP_EXPR_SUBJ) == LV_RESULT_OK);
    lv_obj_t * v = (lv_obj_t*)lv_xml_create(lv_screen_active(), "t_expr4", nullptr);
    lv_obj_t * g = lv_obj_find_by_name(v, "g");
    process_lvgl(lv_display_get_default(), 20);
    REQUIRE(lv_obj_get_width(g) == 30);

    lv_subject_set_int(&base, 100);              // change an operand
    process_lvgl(lv_display_get_default(), 20);
    REQUIRE(lv_obj_get_width(g) == 30);          // resolve-once: width did NOT change

    lv_obj_delete(v);
    lv_xml_component_unregister("t_expr4");
}

// Malformed / unknown expressions splice empty and warn; no crash.
static const char * COMP_EXPR_BAD =
  "<component>"
  "  <view>"
  "    <lv_obj name='root'>"
  "      <lv_obj name='a' width='5' height='5' style_translate_x='${bogus_ident + 1}'/>"
  "      <lv_obj name='b' width='5' height='5' style_translate_x='${5 + }'/>"
  "    </lv_obj>"
  "  </view>"
  "</component>";

TEST_CASE_METHOD(LVGLTestFixture, "expr: malformed/unknown splices empty, no crash", "[xml][expr_compose]") {
    REQUIRE(lv_xml_register_component_from_data("t_expr5", COMP_EXPR_BAD) == LV_RESULT_OK);
    lv_obj_t * v = (lv_obj_t*)lv_xml_create(lv_screen_active(), "t_expr5", nullptr);
    lv_obj_t * root = lv_obj_find_by_name(v, "root");
    REQUIRE(lv_obj_get_child_count(root) == 2);  // widgets still created
    // empty translate_x string parses to 0 in the numeric apply path
    REQUIRE(lv_obj_get_style_translate_x(lv_obj_get_child(root, 0), LV_PART_MAIN) == 0);
    lv_obj_delete(v);
    lv_xml_component_unregister("t_expr5");
    SUCCEED("malformed expression did not crash");
}

// Backward compatibility: bare ${i} and bare ${grp} keep the legacy name-substitution path.
static const char * COMP_EXPR_COMPAT =
  "<component>"
  "  <api><prop name='grp' type='string'/></api>"
  "  <view>"
  "    <lv_obj name='root'>"
  "      <repeat count='2'>"
  "        <lv_label name='lbl' bind_text='status_${grp}_${i}_x'/>"
  "      </repeat>"
  "    </lv_obj>"
  "  </view>"
  "</component>";

TEST_CASE_METHOD(LVGLTestFixture, "expr: bare ${i}/${grp} unchanged (backward compat)", "[xml][expr_compose]") {
    static char pb0[32], pb1[32];
    static lv_subject_t p0, p1;
    lv_subject_init_string(&p0, pb0, nullptr, 32, "compat-zero");
    lv_subject_init_string(&p1, pb1, nullptr, 32, "compat-one");
    lv_xml_register_subject(nullptr, "status_fan_0_x", &p0);
    lv_xml_register_subject(nullptr, "status_fan_1_x", &p1);

    REQUIRE(lv_xml_register_component_from_data("t_expr6", COMP_EXPR_COMPAT) == LV_RESULT_OK);
    const char * attrs[] = {"grp", "fan", nullptr};
    lv_obj_t * v = (lv_obj_t*)lv_xml_create(lv_screen_active(), "t_expr6", attrs);
    lv_obj_t * root = lv_obj_find_by_name(v, "root");
    REQUIRE(lv_obj_get_child_count(root) == 2);
    REQUIRE(std::string(lv_label_get_text(lv_obj_get_child(root, 0))) == "compat-zero");
    REQUIRE(std::string(lv_label_get_text(lv_obj_get_child(root, 1))) == "compat-one");
    lv_obj_delete(v);
    lv_xml_component_unregister("t_expr6");
}
```

> Note: if `process_lvgl(lv_display_get_default(), 20)` is not the exact helper name/signature in `tests/lvgl_test_fixture.h`, use whatever the sibling reactive tests use (e.g. `test_xml_expr_reactive.cpp`) — grep it before running: `grep -rn "process_lvgl" tests/`.

- [ ] **Step 2: Run the tests to verify they fail**

First check no build is running: `pgrep -x cc1plus`. Then:

Run: `make test-run 2>&1 | tail -30` then `./build/bin/helix-tests "[expr_compose]"`
Expected: the `${i + 1}`, `${i * 84}`, `${i + 0}`, subject and malformed cases FAIL (today `${i + 1}` splices empty → `slot__label`, numeric exprs → translate 0). The backward-compat case should already PASS (legacy path). Confirm the failures are behavioral, not compile errors in the test file.

- [ ] **Step 3: Ensure the evaluator header is included**

In `lib/helix-xml/src/xml/lv_xml.c`, near the top with the other `#include "lv_xml_*.h"` lines, confirm/add:

```c
#include "lv_xml_expr.h"
```

(Grep first: `grep -n 'lv_xml_expr.h' lib/helix-xml/src/xml/lv_xml.c`. Add only if missing.)

- [ ] **Step 4: Add the bare-identifier helper**

In `lib/helix-xml/src/xml/lv_xml.c`, immediately **above** `xml_compose_lookup` (~`:1644`), add:

```c
/** True if `s` is a single bare identifier: matches ^[A-Za-z_][A-Za-z0-9_]*$ with no
 *  operators, spaces, or leading digit. A bare-identifier token keeps the legacy
 *  name-substitution path (`${i}`, `${grp}`); anything else is an integer expression. */
static bool xml_token_is_bare_identifier(const char * s)
{
    if(s == NULL || *s == '\0') return false;
    char c0 = *s;
    if(!((c0 >= 'A' && c0 <= 'Z') || (c0 >= 'a' && c0 <= 'z') || c0 == '_')) return false;
    for(const char * p = s + 1; *p; p++) {
        char c = *p;
        if(!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
             (c >= '0' && c <= '9') || c == '_')) return false;
    }
    return true;
}
```

- [ ] **Step 5: Add the compose-site expression resolver + ctx**

In `lib/helix-xml/src/xml/lv_xml.c`, immediately **below** `xml_compose_lookup` (after `:1661`), add. The numeric-param branch is filled in by Task 2; for now the resolver handles the loop index and scope subjects:

```c
/** Max distinct operands an inline `${expr}` may reference — matches the evaluator's
 *  internal subject cap. */
#define XML_COMPOSE_EXPR_MAX_OPERANDS 32

/** Resolver context for an inline `${expr}`. Lives on the stack for exactly one
 *  compose call (compile+eval+free), so index_subject / param_subjects are
 *  stack-lifetime and deinit'd right after eval. No observers are ever attached. */
typedef struct {
    lv_xml_parser_state_t * state;
    lv_xml_repeat_capture_t * rc;                 /* NULL unless in a replaying <repeat> */
    lv_subject_t index_subject;                   /* seeded to rc->current_index for `i` */
    lv_subject_t param_subjects[XML_COMPOSE_EXPR_MAX_OPERANDS]; /* numeric-param operands (Task 2) */
    uint32_t param_count;
} xml_compose_expr_ctx_t;

/** Maps an identifier in a `${expr}` to a subject: `i` -> the loop index; a numeric
 *  component param -> a transient int subject (Task 2); otherwise a real scope subject.
 *  Returns NULL for an unresolvable name, which makes lv_xml_expr_compile fail. */
static lv_subject_t * xml_compose_expr_resolver(void * vctx, const char * name)
{
    xml_compose_expr_ctx_t * c = (xml_compose_expr_ctx_t *)vctx;

    /* 1. loop index */
    if(lv_streq(name, "i"))
        return (c->rc && c->rc->replaying) ? &c->index_subject : NULL;

    /* 2. numeric component-param operand — added in Task 2 */

    /* 3. real scope subject (also finds globally registered subjects) */
    return lv_xml_get_subject(&c->state->scope, name);
}
```

- [ ] **Step 6: Enlarge the token buffer for expressions**

In `xml_compose_indexed` (`:1697`), the token is copied into `char nbuf[64]`. Expressions can exceed a subject-name length, so widen it and update the too-long warning. Change:

```c
            char nbuf[64];
            if(nlen >= sizeof(nbuf)) {
                LV_LOG_WARN("<repeat> ${...} name too long in '%s'; splicing empty", raw);
            }
```

to:

```c
            char nbuf[256];
            if(nlen >= sizeof(nbuf)) {
                LV_LOG_WARN("${...} token too long in '%s'; splicing empty", raw);
            }
```

- [ ] **Step 7: Route non-bare tokens to the evaluator**

In `xml_compose_indexed`, replace the legacy `else` block (`:1701-1713`, the block that does `lv_memcpy(nbuf,...)` through the `xml_compose_lookup` splice) with:

```c
            else {
                lv_memcpy(nbuf, p + 2, nlen);
                nbuf[nlen] = '\0';

                if(xml_token_is_bare_identifier(nbuf)) {
                    /*Legacy name substitution: ${i}, ${grp}, ${prop}.*/
                    char scratch[16];
                    const char * rep = xml_compose_lookup(state, nbuf, scratch, sizeof(scratch));
                    if(rep == NULL) {
                        LV_LOG_WARN("${%s} could not be resolved in '%s'; splicing empty", nbuf, raw);
                    }
                    else {
                        XML_COMPOSE_APPEND(rep, lv_strlen(rep));
                    }
                }
                else {
                    /*Integer expression: evaluate once and splice the result as text.
                     *Resolve-once — compile/eval/free all happen here; no observers.*/
                    xml_compose_expr_ctx_t ectx;
                    ectx.state = state;
                    ectx.rc = state ? (lv_xml_repeat_capture_t *)state->context : NULL;
                    ectx.param_count = 0;
                    lv_subject_init_int(&ectx.index_subject,
                                        (ectx.rc && ectx.rc->replaying) ? (int32_t)ectx.rc->current_index : 0);

                    lv_xml_expr_t * ex = lv_xml_expr_compile(nbuf, xml_compose_expr_resolver, &ectx);
                    if(ex == NULL) {
                        LV_LOG_WARN("${%s} could not be evaluated in '%s'; splicing empty", nbuf, raw);
                    }
                    else {
                        char exprbuf[16];
                        lv_snprintf(exprbuf, sizeof(exprbuf), "%d", (int)lv_xml_expr_eval(ex));
                        XML_COMPOSE_APPEND(exprbuf, lv_strlen(exprbuf));
                        lv_xml_expr_free(ex);
                    }
                    for(uint32_t k = 0; k < ectx.param_count; k++)
                        lv_subject_deinit(&ectx.param_subjects[k]);
                    lv_subject_deinit(&ectx.index_subject);
                }
            }
```

(The surrounding `if(nlen >= sizeof(nbuf)) { … } else { … }` structure and the `p = close + 1;` after it are unchanged — you are only replacing the body of the existing `else`.)

- [ ] **Step 8: Build and run the tests**

Check `pgrep -x cc1plus` first. Run: `make test-run 2>&1 | tail -30` then `./build/bin/helix-tests "[expr_compose]"`
Expected: all `[expr_compose]` cases PASS. Then run the legacy suite to confirm no regression:
`./build/bin/helix-tests "[indexed]" "[xml_expr]"` → all PASS.

- [ ] **Step 9: Commit**

```bash
git add lib/helix-xml/src/xml/lv_xml.c tests/unit/test_xml_expr_compose.cpp
git commit -m "feat(xml): evaluate integer expressions in \${...} composition"
```

---

## Task 2: Numeric component-param operands

Delivers `${cols * 2}` where `cols` is a component param string `"4"` → `8`, and the correct degrade for a non-numeric param in arithmetic.

**Files:**
- Modify: `lib/helix-xml/src/xml/lv_xml.c` (`xml_str_is_integer` helper; the numeric-param branch in `xml_compose_expr_resolver`)
- Test: `tests/unit/test_xml_expr_compose.cpp` (append cases)

**Interfaces:**
- Consumes: `xml_compose_expr_ctx_t` and `xml_compose_expr_resolver` from Task 1; `const char * lv_xml_get_value_of(const char **, const char *)`; `get_param_default(&state->scope, name)`; `int32_t lv_xml_atoi(const char *)` (declared via the `lv_xml_*` utils header already included in `lv_xml.c`).
- Produces: nothing new for later tasks.

- [ ] **Step 1: Write the failing tests**

Append to `tests/unit/test_xml_expr_compose.cpp`:

```cpp
// Numeric component param as an operand: cols="4" -> ${cols * 2} = 8.
static const char * COMP_EXPR_PARAM =
  "<component>"
  "  <api><prop name='cols' type='string'/></api>"
  "  <view>"
  "    <lv_obj name='g' width='${cols * 2}' height='10'/>"
  "  </view>"
  "</component>";

TEST_CASE_METHOD(LVGLTestFixture, "expr: numeric param is a valid operand", "[xml][expr_compose]") {
    REQUIRE(lv_xml_register_component_from_data("t_expr7", COMP_EXPR_PARAM) == LV_RESULT_OK);
    const char * attrs[] = {"cols", "4", nullptr};
    lv_obj_t * v = (lv_obj_t*)lv_xml_create(lv_screen_active(), "t_expr7", attrs);
    lv_obj_t * g = lv_obj_find_by_name(v, "g");
    process_lvgl(lv_display_get_default(), 20);
    REQUIRE(lv_obj_get_width(g) == 8);
    lv_obj_delete(v);
    lv_xml_component_unregister("t_expr7");
}

// Non-numeric param in arithmetic: grp="fan" -> ${grp * 2} fails to compile,
// splices empty (numeric apply -> width 0), no crash.
static const char * COMP_EXPR_PARAM_BAD =
  "<component>"
  "  <api><prop name='grp' type='string'/></api>"
  "  <view>"
  "    <lv_obj name='g' width='${grp * 2}' height='10'/>"
  "  </view>"
  "</component>";

TEST_CASE_METHOD(LVGLTestFixture, "expr: non-numeric param in arithmetic degrades", "[xml][expr_compose]") {
    REQUIRE(lv_xml_register_component_from_data("t_expr8", COMP_EXPR_PARAM_BAD) == LV_RESULT_OK);
    const char * attrs[] = {"grp", "fan", nullptr};
    lv_obj_t * v = (lv_obj_t*)lv_xml_create(lv_screen_active(), "t_expr8", attrs);
    lv_obj_t * g = lv_obj_find_by_name(v, "g");
    process_lvgl(lv_display_get_default(), 20);
    REQUIRE(lv_obj_get_width(g) == 0);   // empty width string -> 0
    lv_obj_delete(v);
    lv_xml_component_unregister("t_expr8");
    SUCCEED("non-numeric param in arithmetic did not crash");
}
```

- [ ] **Step 2: Run to verify failure**

Run: `./build/bin/helix-tests "[expr_compose]"` (after `make test-run`).
Expected: the `numeric param` case FAILS — today the resolver's step-3 subject lookup for `cols` returns NULL (no subject named `cols`), so compile fails and width is 0 (not 8). The non-numeric case may already pass (also degrades to 0), but keep it as a regression guard.

- [ ] **Step 3: Add the integer-check helper**

In `lib/helix-xml/src/xml/lv_xml.c`, directly above `xml_token_is_bare_identifier` (from Task 1), add:

```c
/** True if `s` parses fully as a base-10 integer: optional surrounding whitespace,
 *  optional sign, at least one digit, nothing else. Decides whether a component
 *  param may serve as a numeric `${expr}` operand. */
static bool xml_str_is_integer(const char * s)
{
    if(s == NULL) return false;
    while(*s == ' ' || *s == '\t') s++;
    if(*s == '+' || *s == '-') s++;
    if(!(*s >= '0' && *s <= '9')) return false;    /* require at least one digit */
    while(*s >= '0' && *s <= '9') s++;
    while(*s == ' ' || *s == '\t') s++;
    return *s == '\0';
}
```

- [ ] **Step 4: Fill in the numeric-param branch of the resolver**

In `xml_compose_expr_resolver`, replace the placeholder comment `/* 2. numeric component-param operand — added in Task 2 */` with:

```c
    /* 2. numeric component-param / parent-attr -> transient int operand.
     *    Mirrors xml_compose_lookup's param resolution (parent attrs, then default). */
    {
        const char * pv = lv_xml_get_value_of(c->state->parent_attrs, name);
        if(pv == NULL || pv[0] == '$') pv = get_param_default(&c->state->scope, name);
        if(pv && xml_str_is_integer(pv)) {
            if(c->param_count >= XML_COMPOSE_EXPR_MAX_OPERANDS) return NULL;
            lv_subject_t * s = &c->param_subjects[c->param_count++];
            lv_subject_init_int(s, lv_xml_atoi(pv));
            return s;
        }
    }
```

- [ ] **Step 5: Build and run**

Check `pgrep -x cc1plus`. Run: `make test-run 2>&1 | tail -30` then `./build/bin/helix-tests "[expr_compose]"`
Expected: all `[expr_compose]` cases PASS, including the new numeric-param and non-numeric-param cases.

- [ ] **Step 6: Commit**

```bash
git add lib/helix-xml/src/xml/lv_xml.c tests/unit/test_xml_expr_compose.cpp
git commit -m "feat(xml): allow numeric component params as \${expr} operands"
```

---

## Task 3: Docs + tooling verification

Ship the guide/attr-ref/skill upgrades and confirm the linter/schema need no changes. No code change.

**Files:**
- Modify: `docs/devel/LVGL9_XML_GUIDE.md`, `docs/devel/LVGL9_XML_ATTRIBUTES_REFERENCE.md`, `.claude/skills/helix-xml/references/xml-attributes.md`, `.claude/skills/helix-xml/references/xml-guide.md`

- [ ] **Step 1: Update the guide sigil table + self-wiring section**

In `docs/devel/LVGL9_XML_GUIDE.md`:

Replace line 233 (the `${name}` table row) with:

```markdown
| `${expr}` | Embedded composition / integer expression | `bind_text="slot_${i + 1}_label"`, `style_translate_x="${i * 84}"` | Splices a bare name (`${i}`, `${grp}`) or evaluates an integer expression and splices the result. See [Repeating fragments](#repeating-fragments-with-repeat) |
```

Replace line 596 (`Index **arithmetic** … not supported yet.`) with:

```markdown
`${…}` also evaluates **integer expressions** and splices the result as text: `${i + 1}` (1-based names), `${i * 84}` (computed numeric attributes like `style_translate_x`), `${base * scale}` (subject operands), `${cols * 2}` (a numeric component prop). A single bare name (`${i}`, `${grp}`) still means name-substitution; a token containing operators is evaluated. Operands: the loop index `i`, integer literals, numeric props, and subjects; the grammar and word forms are the same as [expression conditionals](#expression-conditionals). Division/modulo by zero and any unresolvable or malformed expression splice empty and log a warning.

> ⚠️ **Resolve-once.** A `${expr}` is evaluated **once, when the widget is created** — subject operands are read at that moment and the composed value does **not** update if the subject changes later. A `<repeat count="subject">` rebuild re-runs composition; a standalone attribute does not. For a value that must track a subject live, use a `bind_*` binding, not composition.
```

- [ ] **Step 2: Update the attribute reference**

In `docs/devel/LVGL9_XML_ATTRIBUTES_REFERENCE.md`, replace line 108 with:

```markdown
| `$i` / `${…}` | Zero-based iteration index inside a `<repeat>` body. Bare `$i` is a whole-value substitution (`text="$i"`); `${i}` / `${prop}` splices a name into a larger string (`bind_text="slot_${i}_label"`). `${…}` also evaluates an **integer expression** and splices the result (`${i + 1}`, `${i * 84}`, `${base * scale}`, `${cols * 2}`) — operands are `i`, integer literals, numeric props, and subjects. **Resolve-once**: subject operands are read at creation and do not update reactively (use `bind_*` for live values). |
```

- [ ] **Step 3: Update the helix-xml skill (attributes reference)**

In `.claude/skills/helix-xml/references/xml-attributes.md`, replace line 85 with:

```markdown
| `$i` / `${…}` | Zero-based iteration index inside a `<repeat>` body. Bare `$i` is whole-value only (`text="$i"`); `${i}` / `${prop}` splices a name (`bind_text="slot_${i}_label"`). `${…}` also evaluates an **integer expression** and splices the result: `${i + 1}`, `${i * 84}`, `${base * scale}`, `${cols * 2}` (operands: `i`, literals, numeric props, subjects). **Resolve-once** — subject operands read at creation, not reactive; use `bind_*` for live values. |
```

- [ ] **Step 4: Update the helix-xml skill (guide)**

In `.claude/skills/helix-xml/references/xml-guide.md`:

Replace line 147 with:

```markdown
- `${…}` = embedded composition **or** integer expression. A single bare name (`${i}`, `${prop}`) splices into a larger string, e.g. `bind_text="slot_${i}_label"` self-wires each repeated widget to its own indexed subject (C++ must register `slot_0_label`..`slot_N_label`). A token with operators is evaluated as an integer and the result spliced: `${i + 1}`, `${i * 84}` (numeric attrs), `${base * scale}` (subjects), `${cols * 2}` (numeric prop). Operands: `i`, integer literals, numeric props, subjects; grammar as in the expression evaluator. A literal `${...}` anywhere in a value is always resolved.
```

Replace line 149 (`Not yet supported: ${i + 1} arithmetic, nested <repeat>.`) with:

```markdown
- **Resolve-once**: a `${expr}` is evaluated once at widget creation; subject operands do not update reactively (use `bind_*` for live values). Not yet supported: float expressions, reactive computed numeric attributes, nested `<repeat>`.
```

- [ ] **Step 5: Verify tooling needs no change**

Run the schema regen and confirm it produces no diff (no new tag/attr was added):

```bash
grep -rn "extract_schema" mk/ scripts/ Makefile | head    # find the regen target
make regen-schema 2>/dev/null || python3 scripts/extract_schema.py   # use whichever exists
git status --short assets/ | head                          # expect: no schema changes
```

Expected: no modification to the generated schema file. If a schema file *does* change, stop — that means an assumption was wrong; investigate before proceeding.

Then confirm the crossref linter is clean on the new expression forms:

```bash
grep -rln "crossref\|check_xml\|lint" scripts/ | head
# run the XML lint the repo uses (e.g. the bats lint suite):
make test-run 2>&1 | tail -5   # or: ./tests/shell/*.bats if that's the lint entrypoint
```

Expected: no `UNKNOWN_SUBJECT_REF` / lint warning caused by operators or spaces inside `${…}`. (Per the parent spec the crossref already skips `${…}` contents — this step confirms it with the new syntax.)

- [ ] **Step 6: Commit**

```bash
git add docs/devel/LVGL9_XML_GUIDE.md docs/devel/LVGL9_XML_ATTRIBUTES_REFERENCE.md \
        .claude/skills/helix-xml/references/xml-attributes.md \
        .claude/skills/helix-xml/references/xml-guide.md
git commit -m "docs(xml): document \${expr} integer-expression composition"
```

---

## Final verification (after all tasks)

- [ ] Full test suite green: `make test-run 2>&1 | tail -20` then `./build/bin/helix-tests` (all tags).
- [ ] Legacy composition unaffected: `./build/bin/helix-tests "[indexed]"` all PASS.
- [ ] Evaluator suite unaffected: `./build/bin/helix-tests "[xml_expr]"` all PASS.
- [ ] `git log --oneline` shows the three feature commits on `feature/xml-expr-composition`.
- [ ] Skim the diff: only `lv_xml.c`, the new test file, and the four doc files changed. No app-layer includes leaked into `lib/helix-xml/`.

## Self-review notes (author)

- **Spec coverage:** disambiguation rule → T1/S4,S7; `i` bridging → T1/S5,S7; subject operands + resolve-once → T1 test #4; numeric params → T2; error degrade → T1 malformed test + T2 non-numeric; numeric-attr path unchanged (no task needed, verified in spec); docs → T3/S1-4; tooling verify → T3/S5. All spec sections mapped.
- **Type consistency:** `xml_compose_expr_ctx_t` fields (`state`, `rc`, `index_subject`, `param_subjects`, `param_count`) and `xml_compose_expr_resolver` are defined in T1/S5 and extended (not renamed) in T2/S4. `XML_COMPOSE_EXPR_MAX_OPERANDS` defined once (T1/S5), used in T2/S4.
- **Open verification points flagged for the implementer:** exact `process_lvgl` helper name (grep sibling tests); presence of `#include "lv_xml_expr.h"` (grep before adding); exact schema-regen and lint entrypoints (grep before running). These are environment lookups, not design gaps.
