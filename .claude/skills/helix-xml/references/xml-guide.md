# XML Guide — Widgets, Layouts, Styles, Responsive Design

## Layout

### lv_obj Defaults (HelixScreen Theme)

| Property | Default | Notes |
|----------|---------|-------|
| `width` | `content` | Shrinks to content |
| `height` | `content` | Shrinks to content |
| `border_width` | `0` | No border |
| `bg_opa` | `0` | Transparent |
| `pad_all` | `0` | No padding |

### Flex Layout

```xml
<lv_obj flex_flow="row"/>           <!-- row, column, row_wrap, column_wrap, *_reverse -->
```

Three alignment properties (all three needed to center):
- `style_flex_main_place` — main axis (like justify-content)
- `style_flex_cross_place` — cross axis (like align-items)
- `style_flex_track_place` — track alignment (needed even without wrap for centering explicit-width children)

Values: `start`, `end`, `center`, `space_evenly`, `space_around`, `space_between`

```xml
<lv_obj flex_flow="row" width="100%">
    <lv_label text="Left"/>
    <lv_obj flex_grow="1"/>          <!-- Expands to fill -->
    <lv_label text="Right"/>
</lv_obj>
```

**Parent MUST have explicit height for flex_grow to work.**

### Centering

```xml
<!-- Text centering -->
<lv_label text="Centered" style_text_align="center" width="100%"/>

<!-- Flex centering (all three!) -->
<lv_obj flex_flow="column" height="100%"
        style_flex_main_place="center" style_flex_cross_place="center" style_flex_track_place="center">
    <lv_label text="Centered"/>
</lv_obj>

<!-- Single child: use align -->
<lv_obj width="100%" height="100%">
    <lv_obj align="center">Centered</lv_obj>
</lv_obj>
```

## Widget Quick Reference

### Icon Component
```xml
<icon src="home" size="lg"/>         <!-- xs:16, sm:24, md:32, lg:48, xl:64 -->
<icon src="heater" size="lg" variant="accent"/>  <!-- primary, secondary, accent, disabled, warning -->
```

### Semantic Typography (ALWAYS use these, not raw lv_label)
```xml
<text_heading text="Title"/>         <!-- 20/26/28px responsive -->
<text_body text="Content"/>          <!-- 14/18/20px responsive -->
<text_small text="Caption"/>         <!-- 12/16/18px responsive -->
```

### ui_card
Container with card styling. Don't redundantly specify `style_radius`, `style_bg_color`.
```xml
<ui_card name="my_card" width="100%" height="200">
    <text_body text="Content"/>
</ui_card>
```

### ui_button
Semantic button with variant styling. Supports `text="@subject"` or `bind_text="subject"` for reactive text.
```xml
<ui_button variant="primary" text="Save"/>
<ui_button variant="ghost" icon="settings"/>
<!-- variants: primary, secondary, ghost, destructive -->
```

### ui_markdown
Renders markdown as native LVGL widgets. Supports `bind_text` for dynamic content.
```xml
<ui_markdown bind_text="release_notes" width="100%"/>
```

### dividers
```xml
<divider_vertical height="80%"/>
<divider_horizontal width="100%"/>
```

## Expression-Driven Conditions (`<subject_expr>`, `cond=`)

Every `bind_flag_if_*`/`bind_state_if_*`/`bind_style_if_*` element compares **one** subject against one `ref_value`. For compound conditions (`error OR temp > threshold`), use the expression evaluator instead of a hand-written C++ derived subject.

```xml
<subjects>
    <int name="demo_temp" value="50"/>
    <int name="demo_threshold" value="70"/>
    <int name="demo_error" value="0"/>
    <subject_expr name="demo_alarm" expr="demo_error or demo_temp gt demo_threshold"/>
</subjects>

<lv_obj>
    <bind_flag_if cond="demo_alarm" flag="hidden" invert="true"/>   <!-- reuses the derived subject -->
</lv_obj>
<lv_obj>
    <bind_flag_if cond="demo_temp gt demo_threshold" flag="hidden" invert="true"/>  <!-- inline, no subject_expr needed -->
</lv_obj>
<ui_button text="Action">
    <bind_state_if cond="demo_alarm" state="disabled"/>
</ui_button>
<ui_card>
    <bind_style_if name="demo_alarm_style" cond="demo_alarm"/>
</ui_card>
```

- `<subject_expr name="X" expr="EXPR"/>` — sibling of `<subject>`/`<int>` in a `<subjects>` block; creates a derived int subject that recomputes whenever any subject in `EXPR` changes. Every subject referenced must be declared earlier (forward references silently fail to register).
- `cond="EXPR"` works inline on `bind_flag_if`, `bind_state_if`, `bind_style_if` — no `subject_expr` needed for a one-off condition.
- Grammar (integer-only, nonzero = truthy): comparison `eq ne lt le gt ge` (or `== != < <= > >=`), boolean `and or not` (or `&& || !`), arithmetic `+ - * / %` (div/mod by zero → `0`).
- **House style: word forms** (`and`/`or`/`gt`/...). Symbolic `&&`/`<` are XML metacharacters and need `&amp;&amp;`/`&lt;` escaping — word forms don't.
- Live testbed: `ui_xml/test_panel.xml` (`helix-screen ctl navigate test`), all four constructs wired to sliders/switch.

## Repeating Fragments (`<repeat>`)

Expands a body N times at load time — replaces a C++ create-and-wire loop.

```xml
<repeat count="4">
    <lv_label text="$i"/>              <!-- bare $i: whole-value index, 0/1/2/3 -->
</repeat>

<repeat count="row_count">             <!-- subject name: REACTIVE, rebuilds on change -->
    <lv_label bind_text="slot_${i}_label"/>  <!-- ${i}: composes into a larger string -->
</repeat>
```

- `count`: literal, `#const`, or a subject name (subject-bound = reactive rebuild via async off-tree teardown, not a synchronous delete).
- `$i` bare = whole-value substitution only (`text="$i"` works, `text="slot_$i"` does not splice).
- `${…}` = embedded composition **or** integer expression. A single bare name (`${i}`, `${prop}`) splices into a larger string, e.g. `bind_text="slot_${i}_label"` self-wires each repeated widget to its own indexed subject (C++ must register `slot_0_label`..`slot_N_label`). A token with operators is evaluated as an integer and the result spliced: `${i + 1}`, `${i * 84}` (numeric attrs), `${base * scale}` (subjects), `${cols * 2}` (numeric prop). Operands: `i`, integer literals, numeric props, subjects; grammar as in the expression evaluator. A literal `${...}` anywhere in a value is always resolved.
- ⚠️ **A subject-bound `<repeat>` must be its parent's last child, or the sole child of a dedicated wrapper.** LVGL always appends freshly-created children to the tail of the parent's list, so on rebuild, items land after any static siblings that follow the `<repeat>` in the document — silently reordering the layout.
- **Resolve-once**: a `${expr}` is evaluated once at widget creation; subject operands do not update reactively (use `bind_*` for live values). Not yet supported: float expressions, reactive computed numeric attributes, nested `<repeat>`.

## Structural Conditionals (`<if>` / `<else>`)

Creates **only** the matching branch — the other is never built. Different from `bind_flag hidden`/`cond=`, which build both branches and toggle visibility: cheap for light subtrees, wasteful for an expensive one. Use `<if>` for expensive/structural conditional *creation* (a whole card, an alternate layout); keep `bind_flag`/`cond=` for cheap show/hide.

```xml
<subjects><subject name="c" type="int" value="1"/></subjects>
<lv_obj name="root">
  <if cond="c gt 0">
    <lv_obj name="t"/>
    <else/>
    <lv_obj name="f"/>
  </if>
</lv_obj>
<!-- c > 0: root's only child is "t". c <= 0: root's only child is "f". -->
```
(adapted from `tests/unit/test_xml_if_else.cpp`)

- `<else/>` is an inline divider *inside* the one `<if>…</if>` — everything before it is the true-body, everything after (up to `</if>`) is the false-body. `<else/>` and `<else></else>` are identical. Optional — no `<else>` means "create nothing" on false, component still loads.
- `cond`: same word-form grammar as `cond=`/`<subject_expr>` above — subject names, int literals, `and`/`or`/`not`, `eq`/`ne`/`lt`/`le`/`gt`/`ge`, arithmetic.
- **Static vs reactive**: no subject operands = static, evaluated once at load, no observer, losing branch never created. One or more subject operands = reactive, rebuilds (tears down current branch, builds the other) on any operand change.
- ⚠️ **A reactively-rebuilt `<if>` must be its parent's last child, or the sole child of a dedicated wrapper** — same ordering constraint as `<repeat>` above (LVGL appends the rebuilt body after any static siblings that follow it in the document, silently reordering the layout).
- Second `<else/>` in one `<if>` → warns, first split wins. Stray `<else/>` outside any `<if>` → warns, ignored, component still loads. Nested `<if>` not yet supported (same as nested `<repeat>`).

## Styles

### Defining (NO style_ prefix inside <styles>)
```xml
<styles>
    <style name="btn" bg_color="0x2196f3" radius="8" pad_all="12"/>
</styles>
```

### Applying
```xml
<lv_button>
    <style name="btn"/>                        <!-- Default -->
    <style name="btn_pressed" selector="pressed"/>  <!-- State selector -->
</lv_button>
<!-- Inline (WITH style_ prefix) -->
<lv_button style_bg_color="0x111" style_radius="8"/>
```

### Part Selectors
Style widget parts: `main`, `indicator`, `knob`, `items`, `scrollbar`
```xml
<lv_slider style_bg_color="#333" style_bg_color:indicator="#primary" style_bg_color:knob="#fff"/>
```

### State Selectors
`default`, `pressed`, `checked`, `focused`, `disabled`. Combine: `selector="indicator:pressed"`.

## Responsive Design

- **Breakpoints** (height-based): TINY (≤390), SMALL (391-460), MEDIUM (461-550), LARGE (551-700), XLARGE (>700)
- **Spacing tokens**: `#space_xxs` through `#space_2xl` — always use tokens, never pixels
- **Fonts**: Use semantic components (`<text_heading>`, `<text_body>`, `<text_small>`)
- **Colors**: Use token names (`#card_bg`, `#primary_color`)

## Implementation Workflow

1. **Create XML layout** in `ui_xml/<panel_name>.xml`
2. **Create C++ wrapper** with `init_subjects()` (subjects + callbacks) and `create()` (calls `lv_xml_create`)
3. **Register** component in `xml_registration.cpp`
4. **Register subjects BEFORE creating XML**
5. **Update via subjects** — UI reacts automatically

```cpp
// C++ wrapper pattern
void MyPanel::init_subjects() {
    lv_subject_init_string(&status_subject, buf, NULL, sizeof(buf), "Ready");
    lv_xml_register_subject(NULL, "status", &status_subject);
    lv_xml_register_event_cb(nullptr, "on_click", [](lv_event_t* e) { /*...*/ });
}

lv_obj_t* MyPanel::create(lv_obj_t* parent) {
    return lv_xml_create(parent, "my_panel", nullptr);
}

void MyPanel::update(const char* msg) {
    lv_subject_copy_string(&status_subject, msg);
}
```

## Theme Colors (C++ API)
```cpp
lv_color_t bg = ui_theme_get_color("card_bg");       // Token lookup
lv_color_t ok = ui_theme_parse_color("#FF4444");     // Literal hex only
```

## Widget Lookup
```cpp
lv_obj_t* w = lv_obj_find_by_name(parent, "widget_name");  // ✅ Name-based
lv_obj_t* w = lv_obj_get_child(parent, 3);                  // ❌ Index-based (fragile)
```
