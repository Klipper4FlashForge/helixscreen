# Contributor Gotchas

A symptom-indexed reference for "my thing isn't working — what did I miss?"

HelixScreen's declarative-UI stack is designed so XML contributors don't have to know C++ internals. The tradeoff is that when something goes wrong, the failure mode is often **silent** — no crash, no stack trace, just a thing that doesn't render or doesn't react. This doc is the "if you see X, you forgot Y" lookup table.

Flip here when stuck. Search by symptom.

---

## XML & Layout

### My new XML component doesn't render — the `<my_component/>` tag is just nothing.

**Cause:** The component isn't registered. XML components must be registered in C++ before they can be instantiated.

**Fix:** Add a line to `register_xml_components()` in `src/xml_registration.cpp`:

```cpp
register_xml("my_component.xml");
```

Order matters if your component depends on a custom widget or another component — register dependencies first. The file is already grouped by category; find a similar component and put yours near it.

**Why silent?** The XML parser treats an unregistered component as a no-op. No log, no error — the container just ends up empty.

---

### My `bind_style` doesn't change the color / background / border.

**Cause:** Inline style attributes (`style_bg_color="..."`, `style_text_color="..."`, etc.) on the same widget win over `bind_style` in LVGL's style cascade. Inline is always higher priority.

**Fix:** For any property you want to change reactively, remove the inline attribute. Then either:

1. Use **two `<bind_style>` entries** — one per state — so both states have explicit styling, or
2. Put the default in a non-inline style sheet referenced by `bind_style`.

```xml
<!-- ✗ Broken: inline style_bg_color wins, bind_style does nothing -->
<lv_obj style_bg_color="#card_bg" bind_style="active_style" subject="is_active"/>

<!-- ✓ Works: two bind_styles, no inline conflict -->
<lv_obj>
    <bind_style name="inactive_style" subject="is_active" ref_value="0"/>
    <bind_style name="active_style" subject="is_active" ref_value="1"/>
</lv_obj>
```

Reference: lesson **L040**.

---

### My subject binding is stuck at the default value. Updates in C++ don't show up.

**Cause:** The XML `<subjects>` block declared a subject with the same name as a C++-registered subject. XML component-scoped subjects shadow global subjects — your bindings resolve to the local XML subject (default-initialized), not the C++ one that's actually getting updates.

**Fix:** If a value is owned by C++ (`UI_SUBJECT_INIT_AND_REGISTER_*` or explicit `lv_xml_register_subject`), **do not** also declare it in the XML `<subjects>` block. Let the C++ registration be the only source.

Reference: lesson **L046**.

---

### My button click does nothing. `<event_cb callback="foo">` looks right but doesn't fire.

**Cause:** The C++ callback isn't registered. XML names are symbolic — they resolve to C++ function pointers at parse time via `lv_xml_register_event_cb()`.

**Fix:** Register in one of two places:

- **Global / reusable callbacks:** `register_xml_components()` in `src/xml_registration.cpp`.
- **Panel / modal-specific callbacks:** In the class's `register_callbacks()` method or constructor. Example: `src/ui/ui_panel_macros.cpp:85`.

```cpp
lv_xml_register_event_cb(nullptr, "on_my_button_clicked", on_my_button_clicked);
```

**Double-check:** The string name in XML (`callback="on_my_button_clicked"`) must match the first argument here exactly. Typos are silent.

---

### My icon renders as tofu (□) or as a blank placeholder.

**Cause:** You added a codepoint to `include/icons/codepoints.h` but didn't regenerate the icon fonts.

**Fix:** Three steps, all required:

1. Add the icon name + codepoint to `codepoints.h`.
2. Add the icon to `scripts/regen_mdi_fonts.sh` (one line in the codepoints list).
3. Run `make regen-fonts`, then rebuild.

Skipping step 2 is the most common mistake — the codepoint is defined but the font file doesn't contain the glyph.

Reference: lesson **L009**.

---

### The inspector says my widget has no text, but I set `text="Hello"`.

**Cause:** You're setting `text` on a widget that doesn't have a text role — e.g., a raw `<lv_obj>` or a container. Only widgets that inherit from `lv_label` or have explicit text support (`text_body`, `text_heading`, `text_small`, `ui_button`) render text from `text="..."`.

**Fix:** Put a `<text_body>` or `<text_heading>` child inside, or use a widget that already includes text.

---

### My XML edit doesn't show up. I rebuilt and restarted and nothing changed.

**Cause:** You **don't need to rebuild** for XML changes. But you also need to make sure you're editing the right file and that the binary is reading from the right location.

**Fix:**

- XML loads at runtime from `ui_xml/`. Just relaunch: `./build/bin/helix-screen --test -vv`.
- For live reload without relaunching: `HELIX_HOT_RELOAD=1 ./build/bin/helix-screen --test -vv` — edit XML, save, switch panels, see changes.
- If you truly see no change, confirm: (1) you saved the file, (2) it's in `ui_xml/` not a copy elsewhere, (3) the component is actually instantiated on the panel you're viewing.

Reference: lesson **L031**.

---

### I wrote `cond="a && b"` in an XML expression and it's rejected / broken.

**Cause:** `&&` and `<` are XML metacharacters. Written literally inside an attribute value, they either fail to parse as valid XML or get mangled before reaching the expression compiler.

**Fix:** Use the word-form operators instead — they need no escaping and tokenize identically to the symbolic forms: `and`/`or`/`not` for `&&`/`||`/`!`, and `eq`/`ne`/`lt`/`le`/`gt`/`ge` for `==`/`!=`/`<`/`<=`/`>`/`>=`. This is house style for `cond=` and `<subject_expr expr=>`:

```xml
<!-- ✗ Needs escaping, easy to get wrong -->
<bind_flag_if cond="demo_error &amp;&amp; demo_temp &gt; demo_threshold" flag="hidden"/>

<!-- ✓ House style: word forms, no escaping -->
<bind_flag_if cond="demo_error or demo_temp gt demo_threshold" flag="hidden" invert="true"/>
```

If you do need the symbolic form for some reason, escape it as XML entities (`&amp;&amp;`, `&lt;`, `&gt;`) — both forms compile to the same expression.

See `LVGL9_XML_GUIDE.md` § "Expression Conditionals".

---

### My new `cond=`/`<subject_expr>` XML fails CI with `UNKNOWN_WIDGET` or `UNKNOWN_ATTRIBUTE`.

**Cause:** The XML linter validates against a committed schema snapshot (`tools/xml-linter/schema/schema.json`), not against the C++ source directly. Adding a new tag or attribute (like `subject_expr`, `cond`, or any new custom widget) doesn't update that snapshot automatically.

**Fix:** Regenerate and commit the schema:

```bash
make regen-xml-schema
git add tools/xml-linter/schema/schema.json
```

`.github/workflows/lint-xml.yml` runs against the committed schema, so a stale one fails the first XML fixture that uses the new syntax — even though it works fine locally.

Reference: lesson **L089**.

---

## Design Tokens & Theming

### Review feedback: "please use design tokens instead of hardcoded colors."

**Cause:** You used `lv_color_hex(0xE0E0E0)` in C++ or `style_bg_color="#E0E0E0"` in XML.

**Fix:**

- **XML:** Use token names prefixed with `#` — e.g., `style_bg_color="#card_bg"`, `style_pad_all="#space_md"`.
- **C++:** Use `ui_theme_get_color("card_bg")` for semantic tokens. Use `ui_theme_parse_color("#RRGGBB")` only when you're given a literal hex string that can't be tokenized (e.g., user-picked colors).

Reference: lesson **L008**.

---

### I'm redundantly specifying properties that semantic widgets already have.

**Cause:** Semantic widgets like `ui_card`, `ui_button`, `divider_light` already apply their tokenized defaults. Re-specifying `style_radius` on `ui_card` or `button_height` on `ui_button` just duplicates the default and makes the XML noisier.

**Fix:** Only override what you actually need to change. See `docs/devel/LVGL9_XML_GUIDE.md` "Custom Semantic Widgets" for each widget's built-in defaults.

---

## Translations

### `lv_tr()` on a product name is generating a translation key that shouldn't exist.

**Cause:** Product names, URLs, technical abbreviations used as standalone labels, and universal terms must **not** be wrapped in `lv_tr()`.

**Fix:** Leave these untranslated:

- **Product names:** HelixScreen, Klipper, Moonraker, Spoolman, Mainsail, Fluidd, OrcaSlicer.
- **URLs / domains:** `https://helixscreen.org`, `github.com/prestonbrown/helixscreen`.
- **Technical abbreviations as standalone labels:** AMS, QGL, ADXL, PID, IFS, CFS.
- **Material codes:** PLA, PETG, ABS, TPU, PA. (Also: no `translation_tag` on these in XML.)
- **Universal terms:** OK, WiFi.

Add a comment when you skip translation: `// i18n: do not translate (product name)`.

**But:** Sentences *containing* product names *are* translatable. "Restarting HelixScreen..." is fine because "Restarting" needs to translate.

Reference: lesson **L070**.

---

### All my translation strings compile but don't show in other languages.

**Cause:** Translation artifacts weren't regenerated or weren't committed.

**Fix:** After editing YAML translation files, rebuild — the build regenerates `src/generated/lv_i18n_translations.{c,h}` and `ui_xml/translations/translations.xml`. These are **tracked in git** (not gitignored), so you must `git add` them explicitly before committing.

Reference: lesson **L064**.

---

## Subjects & Lifecycle

### The app crashes on reconnect, or on panel rebuild, in an observer callback.

**Cause:** You're observing a **dynamic** subject (per-fan, per-sensor, per-extruder) without a `SubjectLifetime` token — or with a local `SubjectLifetime` that dies before the observer.

**Fix:**

- Pair your member `ObserverGuard` with a **member** `SubjectLifetime`. Never use a local.
- When clearing, reset the lifetime **before** the observer (observer's `weak_ptr` only expires if the `shared_ptr` is destroyed first).
- For per-item collections (carousels, slot lists), use parallel vectors: `std::vector<ObserverGuard>` and `std::vector<SubjectLifetime>`, kept aligned.

```cpp
// Header
ObserverGuard temp_observer_;
SubjectLifetime temp_lifetime_;

// Clear
temp_lifetime_.reset();    // FIRST
temp_observer_.reset();    // SECOND

// Rebind
auto* s = tsm.get_temp_subject(name, temp_lifetime_);
temp_observer_ = observe_int_sync(s, ..., temp_lifetime_);
```

Reference: lessons **L077**, **L084**, and `include/ui_observer_guard.h`.

---

### Two instances of my panel widget exist, but only one gets updates.

**Cause:** You declared a per-instance subject (one per widget) but registered it globally, or declared a single subject and expected both instances to share it but the XML subjects scope is per-component-instantiation.

**Fix:** Decide which you want:

- **Shared subject across all instances:** Declare `static inline` at class scope, register into the component's scope using `lv_xml_register_subject(lv_xml_component_get_scope("my_component"), "subject_name", &subject_)`.
- **Per-instance subject:** Register per-instance and ensure the XML scope resolves to the right one (rarely what you want — usually you'd just use a shared subject filtered by an instance ID).

---

### I'm using `lifetime_.defer()` from a background thread and crashing.

**Cause:** `lifetime_.defer()` reads `this->lifetime_`, which is a TOCTOU race from a background thread — `this` can be destroyed between the check and the deref.

**Fix:** Use `tok.defer()` instead. The token holds its own `shared_ptr`, so it's safe from background threads. Only use `lifetime_.defer()` on the main thread.

```cpp
auto tok = lifetime_.token();
api->fetch([this, tok]() {
    if (tok.expired()) return;
    tok.defer([this]() { update_ui(); });   // Safe
    // NOT: lifetime_.defer([this]() {...}); // TOCTOU race from BG thread
});
```

Reference: CLAUDE.md § "Async callback safety", issue #707.

---

## C++ Threading & Deletion

### My fire-and-forget `std::thread([...]{}).detach()` crashes on the K1/AD5M/CC1.

**Cause:** `pthread_create` returns `EAGAIN` under thread exhaustion on small-memory ARM devices. The `std::thread` constructor then throws, and the throw propagating through an LVGL C event-dispatch frame aborts the process.

**Fix:** Use a managed pool. For HTTP work, use `helix::http::HttpExecutor::fast()` or `::slow()`. For sd-bus/BlueZ, use `helix::bluetooth::BusThread::run_sync()`. For the narrow cases where you genuinely need a one-shot thread (device discovery, QR decode, USB print), wrap in `try { std::thread([...]{}).detach(); } catch (const std::system_error&) { /* toast + error callback */ }`.

Reference: CLAUDE.md § "Threading & Lifecycle", lesson **L083**.

---

### I called `lv_obj_delete()` inside a queued callback and got a SIGSEGV.

**Cause:** Multiple synchronous deletions inside the same `UpdateQueue::process_pending()` batch corrupt LVGL's global event linked list (#776, #190, #80). `lifetime_.defer()` and `tok.defer()` don't escape the batch — they fire in the next tick of the same queue.

**Fix:** Use the deferred variants that route through LVGL's own async list:

| In queued / async callback, instead of: | Use: |
|---|---|
| `safe_delete(ptr)` | `safe_delete_deferred(ptr)` |
| `lv_obj_delete(obj)` | `lv_obj_delete_async(obj)` |
| `lv_obj_clean(container)` | `helix::ui::safe_clean_children(container)` |

Reference: CLAUDE.md § "No sync widget deletion in queued callbacks", **L081**.

---

## Before You Submit

Run through this before opening a PR:

- [ ] **XML-only change?** Confirm hot reload or plain relaunch shows your changes. No rebuild needed.
- [ ] **Added an icon?** Ran `make regen-fonts` and rebuilt.
- [ ] **Added a new XML component?** Registered in `src/xml_registration.cpp`.
- [ ] **Added an event callback?** Registered with `lv_xml_register_event_cb()`.
- [ ] **Any hardcoded colors or pixel values?** Swap for design tokens.
- [ ] **Any new user-visible strings?** Wrapped in `lv_tr()` — *except* product names, URLs, material codes.
- [ ] **Modified translation YAML?** Rebuild, then `git add` the regenerated `src/generated/lv_i18n_translations.*` and `ui_xml/translations/translations.xml`.
- [ ] **Added an observer on a dynamic subject?** Paired with a member `SubjectLifetime`, not a local.
- [ ] **Tested at multiple sizes?** At minimum: `480x320`, `800x480`, `1024x600`. See `docs/devel/UI_CONTRIBUTOR_GUIDE.md` § Screen Breakpoints.
- [ ] **`make test-run` passes.**

---

## When This Doc Doesn't Help

If your symptom isn't here, the next places to look:

- `docs/devel/UI_CONTRIBUTOR_GUIDE.md` — layout, breakpoints, theming
- `docs/devel/LVGL9_XML_GUIDE.md` — XML syntax reference, widget attributes
- `docs/devel/DEVELOPER_QUICK_REFERENCE.md` — code patterns
- `docs/devel/MODAL_SYSTEM.md` — modal-specific patterns
- `docs/devel/TRANSLATION_SYSTEM.md` — i18n architecture
- `CLAUDE.md` (repo root) — the full rules reference; `CTRL-F` for your symptom

And if you've hit something that took you more than an hour to diagnose, **it probably belongs in this doc** — send a PR adding it. Future contributors will thank you.
