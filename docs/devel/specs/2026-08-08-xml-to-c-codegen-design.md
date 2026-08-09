# XML to C Code Generation (build-time component compiler) - Design Spec

**Date:** 2026-08-08
**Status:** Design, pre-implementation
**Author:** Preston Brown (with Claude)
**Component:** `lib/helix-xml` (the generator ships in the fork's own repo, not here)

## Summary

Today every HelixScreen panel is built by re-parsing XML text with expat at runtime,
every time it is created. This spec describes a build-time compiler that turns
`ui_xml/*.xml` into C translation units, so a release build creates the same widget
tree with no parser, no markup in flash, and no per-navigation parse cost. The runtime
parser stays for development builds and hot reload; both paths are generated from the
same XML files and a differential test proves they produce identical trees.

## Why

Measured against the tree as it stands:

| Fact | Number |
|------|--------|
| XML files under `ui_xml/` | 338 |
| Bytes of markup | 1.58 MB |
| Components registered in `xml_registration.cpp` | 300 |
| Widgets registered from C++ via `lv_xml_register_widget` | 37 calls across 31 files |
| Vendored expat source | 435 KB |

Three separate costs fall out of that:

1. **Flash.** All 1.58 MB ships on the device. On the K-Touch ESP32-S3 port it lands in
   the frogfs image alongside assets.
2. **RAM, permanently.** `lv_xml_register_component_from_file()` keeps the `<view>` body
   as a heap copy for process lifetime (`scope->view_def`, `lv_xml_component.c:218`),
   for all 300 components.
3. **CPU, repeatedly.** `lv_xml_create_in_scope()` runs a full `XML_Parse()` over
   `scope->view_def` on *every* instantiation (`lv_xml.c:348`). Navigating to a panel
   ten times parses it ten times.

Codegen removes all three at once for release builds.

## The seam this targets

The important structural fact is that the XML engine already funnels every element
through a two-callback interface:

```c
typedef void * (*lv_xml_widget_create_cb_t)(lv_xml_parser_state_t *, const char ** attrs);
typedef void   (*lv_xml_widget_apply_cb_t) (lv_xml_parser_state_t *, const char ** attrs);
```

`view_start_element_handler()` (`lv_xml.c:2043`) does a fixed sequence per element:
push pcdata, resolve the parent, `resolve_params()`, `resolve_consts()`, look up the
processor by name, `create_cb()`, `apply_cb()`, push the new parent.

**Codegen targets that sequence, not the individual setters.** The generated code hands
the same `(name, attrs)` pairs to the same processors. This is what makes the project
tractable: all 25 widget parsers and all 31 C++-registered custom widgets keep working
untouched, because their `apply_cb` still receives the string attribute array it expects.
No per-widget migration, no attribute coverage matrix, no semantic drift.

What disappears is expat, the SAX state machine, the buffered-fragment replay, and the
markup itself. What stays is attribute interpretation, which is where all the widget
specific behavior lives.

## Generated output

### Element tree

For `ui_xml/components/foo.xml`:

```xml
<component>
  <api><prop name="title" type="string" default="Hi"/></api>
  <view extends="lv_obj" width="100">
    <lv_label text="${title}"/>
  </view>
</component>
```

the generator emits:

```c
/* GENERATED from ui_xml/components/foo.xml - do not edit */
static const char * const A0[] = { "width", "100", NULL };
static const char * const A1[] = { "text", "${title}", NULL };

static void * gen_foo_view(lv_xml_parser_state_t * st, lv_obj_t * parent)
{
    lv_obj_t * v  = lv_xml_emit_element(st, "lv_obj",   A0, parent, /*is_view=*/true);
    (void)          lv_xml_emit_element(st, "lv_label", A1, v,      false);
    return v;
}
```

`lv_xml_emit_element()` is a new, small runtime helper holding exactly the body of
`view_start_element_handler` from `state->tag_name = name` onward. Step one of the
implementation is to refactor the existing handler to call it, so the runtime path and
the generated path share one implementation and cannot diverge.

Note that `A0`/`A1` are `const` and shared by every instantiation, which today's
resolver cannot accept. See "Attribute resolution" below, it is a prerequisite.

### Component metadata

`<api>`, `<consts>`, `<styles>`, `<subjects>`, `<subject_expr>`, `<fonts>`, `<images>`,
`<gradients>`, `<timelines>` are parsed at registration time by
`start_metadata_handler`. The generator emits a scope initializer that calls the same
registration functions directly:

```c
static void gen_foo_register(void)
{
    lv_xml_component_scope_t * s = lv_xml_scope_create("foo");
    lv_xml_register_const(s, "row_h", "40");
    lv_xml_param_add(s, "title", "string", "Hi");
    lv_xml_scope_set_builder(s, gen_foo_view, "lv_obj");
}
```

With both the metadata parse and the view parse generated, `scope->view_def` is never
allocated and expat has no remaining caller.

### Reactive fragments

`<repeat count="subject">` and `<if cond="...">` currently buffer raw SAX events into
`xml_frag_capture_t` and replay them on subject change. Generated code replaces the
event buffer with a builder function pointer plus the index to inject, which is both
smaller and faster:

```c
static void gen_foo_rep3(lv_xml_parser_state_t * st, lv_obj_t * parent, int32_t i);
```

`xml_frag_record_t` keeps its observer, teardown, and reentrancy guard exactly as they
are; only the `capture` field changes representation. A `<repeat count="4">` with a
literal count and an `<if>` over a constant expression are unrolled or resolved at
generate time and cost nothing at runtime.

## Attribute resolution (prerequisite refactor)

Codegen cannot sit on top of the resolver as it stands, and the reason is worth fixing
properly rather than working around.

`resolve_params()` (`lv_xml.c:1015`) and `resolve_consts()` (`:1138`) **mutate the
caller's attribute array in place**, repointing value slots at parameter or const
storage. Generated arrays are `const` and shared across every instantiation of a
component, so the mutation is not merely unsafe, it would corrupt the template on the
first create and every subsequent create would see the resolved values of the first
caller. A scratch copy per element would hide the problem; the array itself is the
problem.

Four other things fall out of the same design:

- **Three ownership regimes coexist in one array** with nothing marking which is which:
  borrowed repoints (params, consts), owned allocations in `state->composed_strings`,
  and per-expansion transients in `idx_strings`. The defensive comments at
  `lv_xml_parser.h:117-127` exist to hold this together by hand.
- **"Drop this attribute" is encoded by poisoning both slots with `""`** (`:1111`,
  `:1167`), so every downstream `apply_cb` iterates over dead entries.
- **`#` is ambiguous**, meaning both "const reference" and "hex color", disambiguated by
  `is_hex_color()` counting six hex digits (`:1123`). A const named `ABCDEF` is
  unreachable.
- **`$prop|ref` packs two values into one attribute string** (`:1048-1070`) because only
  whole-value substitution exists, and the obj parser splits on the pipe downstream.

### The change

Resolution becomes a pure function: immutable template in, resolved attributes out.

```c
lv_xml_attrs_t * lv_xml_resolve(const char * const * tmpl,
                                const lv_xml_resolve_ctx_t * ctx);
```

`lv_xml_attrs_t` carries per-entry `name` / `value` / a flag for borrowed versus
arena-owned, and a `dropped` bit in place of the `""` poisoning. One arena, allocated
per create and freed at the end of it, replaces all three lifetime regimes.
`lv_xml_resolve_ctx_t` bundles what resolution actually reads: item scope, parent scope,
parent attrs, and the active repeat index.

Both callers use it identically, `view_start_element_handler` for the runtime path and
`lv_xml_emit_element` for the generated path, so there is one resolver and no way for
the two paths to disagree.

Worth doing on its own terms: the resolver becomes testable as a pure function with no
LVGL, no expat, and no widget tree, which is the one part of this system that currently
has no direct test coverage.

### Deliberately out of scope

The `#` ambiguity and the `|` packing are real defects, but fixing either changes the
XML dialect and means touching some of the 338 files. Keep them exactly as they are
here. The refactor changes the plumbing, not the language.

### Const folding is not on the table

The tempting next step, resolving `#const` references at generate time, does not work.
Consts are mutated after registration: `src/xml_registration.cpp:200` calls
`lv_xml_update_const()` on `grid_swatch_size` / `grid_gap` / `grid_width`, and `:106-109`
registers consts computed from measured layout at runtime. `#const` must stay a runtime
lookup. `$param`, `${...}` composition, and `$i` are caller-dependent and were never
foldable either, so essentially all resolution remains at runtime by design. The win
from codegen is the removal of parsing, not of resolution.

## The generator

A **host tool**, built and run on the build machine, never cross-compiled.

It needs expat, the component metadata parser, and a tree emitter. It does **not** need
LVGL, widgets, or `apply_cb`, because attribute values are never interpreted at generate
time, for the reasons in "Const folding is not on the table" above. The generator moves
markup into C; it does not evaluate it.

That keeps the tool small and low-risk: it walks the same XML with the same parser
sources and prints C instead of building widgets.

Output: one `.c` per XML file plus one registry `.c` replacing the 300 `register_xml()`
calls in `src/xml_registration.cpp`.

String tables are deduplicated across all 338 files. Attribute names in particular
repeat heavily, so the emitted table is far smaller than the 1.58 MB of source markup.

## Build integration

**Native (`make`):** a pattern rule `ui_xml/%.xml -> build/gen/%.c`, with the generator
built as a host binary. Generated sources are build artifacts, gitignored, never
committed. Regeneration is driven by mtime like any other target.

**ESP-IDF (`firmware/helixscreen-esp32`):** the `helixcore` component already globs
`${REPO_ROOT}/lib/helix-xml/src/xml/*.c` (`components/helixcore/CMakeLists.txt:27`), so
this adds an `add_custom_command` for the generator plus the generated directory in
`SRCS`. When codegen is on, `stage_assets.py` drops `ui_xml/` from the frogfs image
entirely.

**Selecting the path:** `LV_USE_XML_RUNTIME_PARSER` in `lv_conf.h`. Off means expat,
`lv_xml_parser.c`, and the metadata handlers compile away, and
`lv_xml_register_component_from_file()` is not available. Dev builds leave it on.

## Development flow is unchanged

Hot reload (`HELIX_HOT_RELOAD`) depends on re-registering components from files at
runtime, which is exactly what codegen removes. So:

- **Dev builds:** runtime parser, hot reload, XML edited live with no rebuild. Unchanged.
- **Release builds:** generated C, no parser, no markup.

Same XML source either way. The XML stays the authoring format, which preserves the
declarative-UI rules in `CLAUDE.md` untouched. Nobody writes or reads generated code.

## Verification

The acceptance gate is a **differential test**, not eyeballing.

A desktop test binary compiles both paths, then for each of the 300 components builds
the tree twice, once through `lv_xml_create()` and once through the generated builder,
and walks both trees comparing:

- widget class and child count at every node
- `lv_obj_get_name()`
- computed coordinates after a forced layout
- a fixed set of style properties per node
- observer count on bound subjects

Any mismatch is a generator bug and fails the build. This is cheap to run, covers every
component including the 31 custom C++ widgets, and it is the only way to be confident
that 338 files came through unchanged. `lv_xml_test.c` already exists in the engine and
is the natural home for the harness.

Beyond that: the existing suite must stay green with `LV_USE_XML_RUNTIME_PARSER=0`, and
the ESP32 build has to boot to the home panel on the K-Touch.

## Phasing

0. **Measure the baseline first.** Boot time to first panel, per-navigation parse time,
   RSS, flash. Without numbers there is no way to tell whether this was worth doing.
1. **Attribute resolution refactor.** `lv_xml_resolve()` as a pure function, arena
   ownership, `dropped` bit, with unit tests on the resolver in isolation. Then extract
   `lv_xml_emit_element()` from `view_start_element_handler` on top of it. Both steps
   are pure refactors: the existing suite must be green with no test changes, and a
   screenshot pass over the main panels should be pixel-identical.
2. Host generator: element tree plus component metadata. No build integration yet.
3. Differential test over all 300 components. Iterate until clean.
4. `<repeat>` and `<if>` builder functions.
5. Build integration on both native and ESP-IDF, add `LV_USE_XML_RUNTIME_PARSER=0`,
   re-measure against phase 0.
6. Optional: direct setter calls for widgets whose parsers are pure attribute tables,
   skipping `apply_cb` string dispatch. Per-widget opt-in with fallback, and only if
   phase 5's numbers say the remaining dispatch cost is worth it.

Phases 1 through 3 are independently useful and can land without any build change.
Phase 1 in particular stands on its own: if codegen is abandoned after it, the resolver
is still better than what is there now.

## Risks and non-goals

- **Error messages get worse.** Today a bad component logs an expat line number. The
  generator must emit the source file and line into each element record so runtime
  warnings stay traceable. This is a requirement, not a nicety.
- **Phase 1 touches the hottest path in the engine** and every component depends on it.
  A resolver bug will not look like a resolver bug, it will look like one attribute
  quietly not applying somewhere in 338 files, which is the exact failure mode the
  `""` poisoning already produces today. The resolver unit tests have to land with the
  refactor, not after it.
- **Compile time goes up.** 338 generated translation units is not free. Measure at
  phase 5, and if it hurts, emit one TU per directory rather than per file.
- **A stale generated tree is a new failure mode**, the same class as the existing
  "stale binary, unregistered widget" error. The dependency rules have to be right, and
  CI should build both paths on every PR.
- **Not a goal:** replacing XML as the authoring format, changing the XML dialect, or
  exposing generated code as an API. It is an internal build artifact.
- **Not a goal:** an editor, a preview tool, or anything visual.

## Clean-room note

C export from a UI description is a paid feature of the LVGL Editor. Per the fork's
clean-room rule in `lib/helix-xml/README.md`, nothing here may be derived from LVGL Pro
source or a decompiled Editor. In practice this design has no exposure: it is a codegen
backend over our own parser sources, targeting a callback seam that exists in the MIT
9.4 code, and none of it requires knowing how anyone else did it.
