# Macro Panel Edit Mode Implementation Plan

> ⚠️ **Historical record (verified 2026-08-09) - not instructions.**
> **Status: SHIPPED.** The 48 `- [ ]` boxes were never ticked and do not mean the work is
> outstanding. Evidence: the hidden-macros setting and the panel edit mode, covered by
> `tests/unit/test_hidden_macros_settings.cpp` and
> `tests/unit/test_ui_panel_macros_edit_mode.cpp` - not the `test_settings_manager.cpp` /
> `test_xml_unregister_subject.cpp` files this plan names, which were never created.
>
> Code line numbers and file paths below may have drifted. Follow the **symbol**, not
> the number, and verify every predicate against current code before relying on
> anything here.

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Long-press a macro to enter an edit mode where every macro has a visibility checkbox; a header Save button persists the hidden set per-printer to `settings.json`; rows are rendered declaratively via reactive `<repeat>` over a reclaim-on-close indexed subject pool.

**Architecture:** Bottom-up. Land the reusable primitives first (an engine `lv_xml_unregister_subject`, then an `IndexedSubjectPool` that name-registers dynamically-sized subjects and reclaims them on close), then persistence, then the declarative XML row template, then the panel controller that ties them together. Each layer is independently tested before the next consumes it.

**Tech Stack:** C++17, LVGL 9.5, helix-xml engine (`lib/helix-xml/`, our own submodule), Catch2 tests, pure Makefile build.

## Global Constraints

- Spec: `docs/devel/specs/2026-07-24-macro-panel-edit-mode-design.md` (authoritative).
- Build the program: `make -j`. Build tests: `make test`. Run: `make test-run` or `./build/bin/helix-tests "[tag]"`. Never run `helix-tests` without `make test` first (stale binary).
- XML files load at runtime — no rebuild for XML-only edits; relaunch (or `HELIX_HOT_RELOAD=1`).
- `lib/helix-xml/` is our own submodule — edit in place, push from inside it, bump the pointer here. LVGL/libhv changes would need patches; not touched here.
- Declarative-UI rules apply: no `lv_obj_add_event_cb` (use XML `<event_cb>`), no `lv_label_set_text` on bound labels, design tokens only, no hardcoded colors/spacing.
- Threading: all pool populate/reclaim and subject sets run on the **main thread**.
- New XML components must be registered in `main.cpp` ([L014]); after adding/registering a widget run `make regen-xml-schema` and commit the schema ([L089]).
- New icon glyphs require `codepoints.h` + `make regen-fonts` + rebuild ([L009]); C++-built glyph labels must use the icon font, not body font ([L097]).
- Commit style: `feat(scope): thing` / `fix(scope): thing (prestonbrown/helixscreen#NNN)`. Subject + ~4-line body.
- Per-printer settings key uses `config->df()` (already ends in `/`).

---

## Task 1: Engine — `lv_xml_unregister_subject`

**Files:**
- Modify: `lib/helix-xml/src/xml/lv_xml.c` (add function near `lv_xml_get_subject`, ~line 637)
- Modify: `lib/helix-xml/src/xml/lv_xml.h` (declare near line 108, beside `lv_xml_register_subject`)
- Test: `tests/unit/test_xml_unregister_subject.cpp` (new)

**Interfaces:**
- Produces: `lv_result_t lv_xml_unregister_subject(lv_xml_component_scope_t * scope, const char * name);`
  Non-owning: unlinks the `lv_xml_subject_t` record from `scope->subjects_ll` and frees only the strdup'd name string. Does NOT `lv_subject_deinit` or free `record->subject`. `scope == NULL` resolves to the `"globals"` scope (mirror `lv_xml_register_subject`). Returns `LV_RESULT_OK` if removed, `LV_RESULT_INVALID` if not found / no scope.

- [ ] **Step 1: Read the surrounding code.** Read `lv_xml.c:607-660` (`lv_xml_register_subject` + `lv_xml_get_subject`) and `lv_xml_component_private.h:31-59` (`lv_xml_subject_t`, `subjects_ll`) so the new function matches list-walk + globals-fallback idioms exactly.

- [ ] **Step 2: Write the failing test** in `tests/unit/test_xml_unregister_subject.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <catch2/catch_test_macros.hpp>
#include "lv_xml.h"
#include "helix_test_fixture.h"   // pulls LVGL init

TEST_CASE_METHOD(HelixTestFixture, "lv_xml_unregister_subject removes a global name", "[xml][subject][unregister]") {
    lv_subject_t s;
    lv_subject_init_int(&s, 7);
    REQUIRE(lv_xml_register_subject(nullptr, "unreg_test_a", &s) == LV_RESULT_OK);
    REQUIRE(lv_xml_get_subject(nullptr, "unreg_test_a") == &s);

    REQUIRE(lv_xml_unregister_subject(nullptr, "unreg_test_a") == LV_RESULT_OK);
    REQUIRE(lv_xml_get_subject(nullptr, "unreg_test_a") == nullptr);   // gone from registry

    // Non-owning: the subject is still usable (not freed/deinited by unregister)
    REQUIRE(lv_subject_get_int(&s) == 7);
    lv_subject_deinit(&s);   // caller owns teardown
}

TEST_CASE_METHOD(HelixTestFixture, "lv_xml_unregister_subject on absent name is invalid", "[xml][subject][unregister]") {
    REQUIRE(lv_xml_unregister_subject(nullptr, "no_such_name_zzz") == LV_RESULT_INVALID);
}
```

- [ ] **Step 3: Run it, expect FAIL** (undeclared function): `make test && ./build/bin/helix-tests "[unregister]"` → link/compile error or FAIL.

- [ ] **Step 4: Implement** in `lv_xml.c` (adapt exact types/macros to what Step 1 shows — `LV_LL_READ`, `lv_ll_remove`, `lv_free`, globals-scope lookup):

```c
lv_result_t lv_xml_unregister_subject(lv_xml_component_scope_t * scope, const char * name)
{
    if(scope == NULL) scope = lv_xml_component_get_scope("globals");
    if(scope == NULL) return LV_RESULT_INVALID;

    lv_xml_subject_t * s;
    LV_LL_READ(&scope->subjects_ll, s) {
        if(lv_streq(s->name, name)) {
            lv_free((char *)s->name);          /* free the name copy only */
            lv_ll_remove(&scope->subjects_ll, s);
            lv_free(s);                        /* free the record, NOT s->subject */
            return LV_RESULT_OK;
        }
    }
    return LV_RESULT_INVALID;
}
```

Declare in `lv_xml.h` beside `lv_xml_register_subject`, with a doc comment stating the non-owning contract.

- [ ] **Step 5: Run, expect PASS**: `make test && ./build/bin/helix-tests "[unregister]"` → all pass.

- [ ] **Step 6: Commit** (explicit paths):

`lib/helix-xml` is a separate repo, so the engine change and the test commit separately —
the engine inside the submodule, then the bumped pointer plus the test here:

```bash
# 1. the engine change, inside the submodule
cd lib/helix-xml
git add src/xml/lv_xml.c src/xml/lv_xml.h
git commit -m "Add lv_xml_unregister_subject (non-owning registry removal)"
git push
cd ../..

# 2. the bumped pointer and the test, in helixscreen
git add lib/helix-xml tests/unit/test_xml_unregister_subject.cpp
git commit -m "feat(helix-xml): add lv_xml_unregister_subject (non-owning registry removal)"
```

---

## Task 2: `IndexedSubjectPool` helper

**Files:**
- Create: `include/helix/xml/indexed_subject_pool.h`
- Create: `src/util/indexed_subject_pool.cpp` (unless the team prefers header-only; impl keeps LVGL includes out of headers)
- Test: `tests/unit/test_indexed_subject_pool.cpp`

**Interfaces:**
- Consumes: `lv_xml_register_subject`, `lv_xml_get_subject`, `lv_xml_unregister_subject` (Task 1).
- Produces:
```cpp
namespace helix::xml {
class IndexedSubjectPool {
public:
    enum class Type { Int, String };
    IndexedSubjectPool(std::string prefix, Type type, size_t string_cap = 64);
    ~IndexedSubjectPool();                 // calls reclaim()
    IndexedSubjectPool(const IndexedSubjectPool&) = delete;
    IndexedSubjectPool& operator=(const IndexedSubjectPool&) = delete;

    void ensure_size(size_t n);            // grow-only; inits + name-registers new slots
    void set_int(size_t i, int v);         // Type::Int only
    void set_string(size_t i, const std::string& v);  // Type::String only
    lv_subject_t* at(size_t i);
    size_t size() const;
    void reclaim();                        // unregister + deinit + free every slot; idempotent
};
}
```

Naming: slot `i` is registered under `"<prefix>_<i>"`. String subjects get a fixed `string_cap` heap buffer per slot (LVGL string subjects need stable backing). Grow-only: `ensure_size` never shrinks; the pool's high-water mark lives until `reclaim()`.

- [ ] **Step 1: Read references.** `printer_fan_state.cpp:442` (`make_unique<lv_subject_t>` per item), `ams_state.cpp:1114-1131` (deinit protocol), and how string subjects are initialized elsewhere (`lv_subject_init_string(&s, buf, NULL, cap, initial)` — confirm the exact signature in `lib/lvgl/src/core/lv_observer.h`).

- [ ] **Step 2: Write failing tests** in `tests/unit/test_indexed_subject_pool.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <catch2/catch_test_macros.hpp>
#include "helix/xml/indexed_subject_pool.h"
#include "lv_xml.h"
#include "helix_test_fixture.h"

using helix::xml::IndexedSubjectPool;

TEST_CASE_METHOD(HelixTestFixture, "IndexedSubjectPool registers resolvable indexed names", "[xml][pool]") {
    IndexedSubjectPool names("pooltest_name", IndexedSubjectPool::Type::String);
    names.ensure_size(3);
    names.set_string(0, "alpha");
    names.set_string(2, "gamma");

    REQUIRE(names.size() == 3);
    REQUIRE(lv_xml_get_subject(nullptr, "pooltest_name_0") == names.at(0));
    REQUIRE(lv_xml_get_subject(nullptr, "pooltest_name_2") == names.at(2));
    REQUIRE(std::string(lv_subject_get_string(names.at(0))) == "alpha");
}

TEST_CASE_METHOD(HelixTestFixture, "IndexedSubjectPool grow preserves earlier slots", "[xml][pool]") {
    IndexedSubjectPool vis("pooltest_vis", IndexedSubjectPool::Type::Int);
    vis.ensure_size(2);
    vis.set_int(1, 42);
    lv_subject_t* slot1 = vis.at(1);
    vis.ensure_size(5);                         // grow
    REQUIRE(vis.at(1) == slot1);                // stable address (unique_ptr)
    REQUIRE(lv_subject_get_int(vis.at(1)) == 42);
    REQUIRE(lv_xml_get_subject(nullptr, "pooltest_vis_4") == vis.at(4));
}

TEST_CASE_METHOD(HelixTestFixture, "IndexedSubjectPool reclaim unregisters all names and is idempotent", "[xml][pool]") {
    auto pool = std::make_unique<IndexedSubjectPool>("pooltest_rc", IndexedSubjectPool::Type::Int);
    pool->ensure_size(3);
    REQUIRE(lv_xml_get_subject(nullptr, "pooltest_rc_1") != nullptr);
    pool->reclaim();
    REQUIRE(lv_xml_get_subject(nullptr, "pooltest_rc_0") == nullptr);
    REQUIRE(lv_xml_get_subject(nullptr, "pooltest_rc_2") == nullptr);
    pool->reclaim();                            // idempotent — no crash / double free
    pool.reset();                               // dtor reclaim on empty — safe
}
```

- [ ] **Step 3: Run, expect FAIL**: `make test && ./build/bin/helix-tests "[pool]"`.

- [ ] **Step 4: Implement.** Header declares the class; cpp implements:
  - Storage: `std::vector<std::unique_ptr<lv_subject_t>> subjects_;` and, for strings, `std::vector<std::unique_ptr<std::array<char, ...>>>` OR `std::vector<std::string>` reserved to `string_cap` with stable buffers — use `std::unique_ptr<char[]>` per slot to guarantee a stable, fixed-capacity buffer.
  - `ensure_size(n)`: for `i` in `[size(), n)`: allocate subject; `Int` → `lv_subject_init_int(ptr, 0)`; `String` → allocate `char[string_cap]` buffer, `lv_subject_init_string(ptr, buf, nullptr, string_cap, "")`; then `lv_xml_register_subject(nullptr, name_i.c_str(), ptr)` where `name_i = prefix_ + "_" + std::to_string(i)`.
  - `set_int`/`set_string`: assert type; `lv_subject_set_int` / `lv_subject_copy_string`.
  - `reclaim()`: for each existing slot: `lv_xml_unregister_subject(nullptr, name_i.c_str())`; `lv_subject_deinit(ptr)`; then clear vectors. Guard against double-run (clear makes it idempotent).
  - `~IndexedSubjectPool()` calls `reclaim()`.
  - Add `src/util/indexed_subject_pool.cpp` to the build (Makefile globs `src/**`? confirm; if explicit source lists exist, add it).

- [ ] **Step 5: Run, expect PASS**: `./build/bin/helix-tests "[pool]"`.

- [ ] **Step 6: Valgrind the pool test** (native harness) to prove no leak/UAF across grow + reclaim. Command per `reference_native_valgrind_harness` (e.g. `make valgrind-tests` or the documented harness) filtered to `[pool]`. Expected: 0 errors, 0 leaks from the pool.

- [ ] **Step 7: Commit**:

```bash
git add include/helix/xml/indexed_subject_pool.h src/util/indexed_subject_pool.cpp tests/unit/test_indexed_subject_pool.cpp
git commit -m "feat(xml): add IndexedSubjectPool — reclaim-on-close dynamic name-registered subjects"
```

---

## Task 3: SettingsManager — hidden macros persistence

**Files:**
- Modify: `include/settings_manager.h` (declare getters/setters near other list settings)
- Modify: `src/system/settings_manager.cpp` (implement, mirror `console_filter_user_add` at :538-570)
- Test: `tests/unit/test_settings_manager.cpp` (add cases) or a focused new file `tests/unit/test_hidden_macros_settings.cpp`

**Interfaces:**
- Produces:
```cpp
std::vector<std::string> SettingsManager::get_hidden_macros() const;                 // key df()+"macros/hidden"
void SettingsManager::set_hidden_macros(const std::vector<std::string>& names);      // set + config->save()
bool SettingsManager::hidden_macros_key_exists() const;                              // for first-run seed decision
```

- [ ] **Step 1: Read** `settings_manager.cpp:538-570` and `config.h` (`df()`, `get<T>`, `set<T>`, `exists`, `save`).

- [ ] **Step 2: Write failing test** (drive Config through its test path — check how other settings tests instantiate/point Config at a temp file):

```cpp
TEST_CASE_METHOD(HelixTestFixture, "hidden macros round-trip per printer", "[settings][macros]") {
    auto& sm = SettingsManager::instance();
    REQUIRE(sm.get_hidden_macros().empty());
    REQUIRE_FALSE(sm.hidden_macros_key_exists());

    sm.set_hidden_macros({"_HOME_Z", "_CALIBRATE"});
    REQUIRE(sm.hidden_macros_key_exists());
    auto got = sm.get_hidden_macros();
    REQUIRE(got.size() == 2);
    REQUIRE(std::find(got.begin(), got.end(), "_HOME_Z") != got.end());
}
```

- [ ] **Step 3: Run, expect FAIL**: `make test && ./build/bin/helix-tests "[settings][macros]"`.

- [ ] **Step 4: Implement** mirroring `console_filter_user_add`, keyed `Config::get_instance()->df() + "macros/hidden"`, with try/catch → warn + `{}` on malformed JSON. `hidden_macros_key_exists()` → `config->exists(df()+"macros/hidden")`. `set_*` calls `config->set<std::vector<std::string>>(...)` then `config->save()`.

- [ ] **Step 5: Run, expect PASS.**

- [ ] **Step 6: Commit**:

```bash
git add include/settings_manager.h src/system/settings_manager.cpp tests/unit/test_hidden_macros_settings.cpp
git commit -m "feat(settings): persist per-printer hidden macro set"
```

---

## Task 4: Checkbox glyph (icon font)

**Files:**
- Modify: `assets/.../codepoints.h` (add `check_box` + `check_box_outline_blank` if not present)
- Run: `make regen-fonts`, then rebuild.
- Verify: a throwaway XML/panel or existing icon test renders the new glyphs (not tofu).

**Interfaces:**
- Produces: icon names usable in XML `<icon src="check_box">` / `<icon src="check_box_outline_blank">` and via `ui_icon::lookup_codepoint(...)`.

- [ ] **Step 1: Check** whether `check_box`/`check_box_outline_blank` (or equivalents) already exist in `codepoints.h` and the icon set (`favorite_macro_config_modal.cpp:37` lists a curated set incl. `check`). If a suitable filled/empty pair already exists, skip to Task 5 and use those names.

- [ ] **Step 2:** If missing, add the two codepoints to `codepoints.h` following the existing entry format (Material Symbols codepoints for `check_box` = e834, `check_box_outline_blank` = e835 — confirm against the font in use).

- [ ] **Step 3:** `make regen-fonts && make -j`. Expected: build succeeds, font blobs regenerated.

- [ ] **Step 4: Verify glyphs render** — launch with a scratch usage or add both icons to `test_panel.xml` temporarily, screenshot, confirm boxes render (not tofu). Per [L097], C++-built glyph labels must resolve the icon font; here we use XML `<icon>` which already does.

- [ ] **Step 5: Commit** (glyphs + regenerated font artifacts):

```bash
git add assets/ ui_xml/  # exact regenerated paths from make regen-fonts output
git commit -m "feat(icons): add check_box / check_box_outline_blank glyphs"
```

---

## Task 5: `macro_card.xml` — declarative row with checkbox

**Files:**
- Modify: `ui_xml/macro_card.xml`
- (No rebuild — XML loads at runtime. Verify via relaunch.)

**Interfaces:**
- Consumes: global subject `macro_edit_mode` (int; Task 7 registers it) and per-row subjects named by the props below (Task 7 pool-registers them).
- Produces: `macro_card` component accepting props (all `type="string"`, default empty — they carry SUBJECT NAMES, spliced per-row by `<repeat>`):
  - `name_subject` → `bind_text` for the name label.
  - `desc_subject` → `bind_text` for the description label (preserves today's description row).
  - `visible_subject` (int subject) → checkbox checked state.
  - `desc_hidden_subject` (int subject) → hides the description label per-row (C++ folds "has description" + "edit mode" into it).
  - `chevron_hidden_subject` (int subject) → hides the chevron per-row (C++ folds "no params" + "edit mode" into it).
  - `row_index` → `user_data` on the row for click identity.
  Retains existing look (icon + name/description column + chevron). Checkbox shown only in edit mode (driven by the global `macro_edit_mode` subject). Description and chevron visibility are per-row ints the controller computes per mode (both forced hidden in edit mode for a compact toggle list).

- [ ] **Step 1: Read** current `macro_card.xml` and note its `<view extends="lv_button">`, existing props (`macro_name`, `macro_description`, `hide_description`, `hide_chevron`), and the existing `<event_cb trigger="clicked" callback="on_macro_card_clicked"/>`.

- [ ] **Step 2: Add props** in `<api>`: `name_subject`, `desc_subject`, `visible_subject`, `desc_hidden_subject`, `chevron_hidden_subject`, `row_index` — all `type="string"`, default empty. (The `*_subject` props carry subject NAMES; `bind_*`/`$prop` resolve them.)

- [ ] **Step 3: Add the checkbox** as the leading child (before the icon). Use the new glyphs, bound to the row's visible subject and gated on edit mode. Because `<if>` can't nest in the (future) `<repeat>`, use reactive flags. Use a single exclusive-visibility binding per widget to avoid the multi-`bind_flag_if_eq` race ([L042]/`reference_bind_flag_multi_eq_conflict`):

```xml
<!-- checkbox: visible only in edit mode -->
<lv_obj name="check_wrap" width="content" height="content" clickable="false" event_bubble="true">
  <bind_flag_if_eq subject="macro_edit_mode" flag="hidden" ref_value="0"/>
  <!-- checked box: shown when visible_subject == 1 -->
  <icon name="chk_on" src="check_box" clickable="false" event_bubble="true">
    <bind_flag_if_eq subject="$visible_subject" flag="hidden" ref_value="0"/>
  </icon>
  <!-- empty box: shown when visible_subject == 0 -->
  <icon name="chk_off" src="check_box_outline_blank" clickable="false" event_bubble="true">
    <bind_flag_if_eq subject="$visible_subject" flag="hidden" ref_value="1"/>
  </icon>
</lv_obj>
```

(Two stacked icons toggled by inverse ref_values on *different* widgets — no conflict. `clickable="false" event_bubble="true"` so taps pass through to the row per [L071].)

- [ ] **Step 4: Bind name + description** — name label `bind_text="$name_subject"` (replacing the static `macro_name` prop). Description label `bind_text="$desc_subject"` and its own single hidden binding `<bind_flag_if_eq subject="$desc_hidden_subject" flag="hidden" ref_value="1"/>` (replacing the old `hidden="$hide_description"`). Keep the `text_small` styling (`#text_muted`, `long_mode="dots"`, `width="100%"`).

- [ ] **Step 5: Chevron hidden per-row** — on the existing chevron, replace `hidden="$hide_chevron"` with its own single binding `<bind_flag_if_eq subject="$chevron_hidden_subject" flag="hidden" ref_value="1"/>`. (The controller folds both "macro has no params" and "edit mode" into this per-row int, so the chevron correctly disappears in edit mode AND for param-less macros in normal mode — one widget, one binding, no [L042] conflict.)

- [ ] **Step 6: Row identity** — set `user_data="$row_index"` on the root `<view>` and change the existing `<event_cb trigger="clicked" ...>` callback to `on_macro_row_clicked` with `user_data="$row_index"`. (NOTE: supersedes any "keep `on_macro_card_clicked`" wording — Task 7 registers `on_macro_row_clicked`, which branches on edit mode. Use the new name so the two tasks agree.)

- [ ] **Step 7: Verify** by relaunch (Task 7 wires data). Defer runtime verification to Task 8 when the panel provides subjects. Commit the XML now:

```bash
git add ui_xml/macro_card.xml
git commit -m "feat(macros): declarative macro_card row with edit-mode checkbox"
```

---

## Task 6: `macro_panel.xml` — repeat container + Save button

**Files:**
- Modify: `ui_xml/macro_panel.xml`

**Interfaces:**
- Consumes: global subjects `macro_row_count` (int), `macros_edit_save_hidden` (int), and pool subjects `macro_name_${i}` / `macro_desc_${i}` / `macro_visible_${i}` / `macro_desc_hidden_${i}` / `macro_chevron_hidden_${i}` (Task 7).
- Produces: a `rows_container` holding a reactive `<repeat>` of `macro_card`, and a header Save button bound to `macros_edit_save_hidden`.

- [ ] **Step 1: Read** current `macro_panel.xml` — note `<view ... extends="overlay_panel">`, the `macro_list` container, `empty_state`, and that no action-button props are currently set.

- [ ] **Step 2: Add Save button props** to the `<view extends="overlay_panel">` opening tag (mirror `ams_edit_overlay.xml:28-35`):

```xml
hide_action_button="false"
action_button_text="Save"
action_button_callback="on_macros_edit_save"
action_button_hidden_subject="macros_edit_save_hidden"
```

- [ ] **Step 3: Replace the C++-populated list** with a dedicated container whose only child is the reactive repeat (the repeat MUST be the only/last child — rebuilt items append):

```xml
<lv_obj name="rows_container" width="100%" height="content"
        flex_flow="column" style_pad_row="#space_xs">
  <repeat count="macro_row_count">
    <macro_card name_subject="macro_name_${i}"
                desc_subject="macro_desc_${i}"
                visible_subject="macro_visible_${i}"
                desc_hidden_subject="macro_desc_hidden_${i}"
                chevron_hidden_subject="macro_chevron_hidden_${i}"
                row_index="${i}"/>
  </repeat>
</lv_obj>
```

Keep `empty_state` as a sibling gated on count (e.g. `<bind_flag_if_eq subject="macro_row_count" flag="hidden" ref_value="0"/>` inverse for empty). Confirm the container lives inside the panel's existing scroll area.

- [ ] **Step 4: Commit**:

```bash
git add ui_xml/macro_panel.xml
git commit -m "feat(macros): reactive <repeat> row list + header Save button"
```

---

## Task 7: `MacrosPanel` controller rewrite

**Files:**
- Modify: `include/ui_panel_macros.h` (state + method decls)
- Modify: `src/ui/ui_panel_macros.cpp` (model, modes, pools, handlers, long-press)
- Modify: `src/xml_registration.cpp` or wherever subjects init — register `macro_row_count`, `macro_edit_mode`, `macros_edit_save_hidden` via `UI_MANAGED_SUBJECT_INT` + `StaticSubjectRegistry::register_deinit`.
- Test: `tests/unit/test_ui_panel_macros_edit_mode.cpp` (new; unit-level on the model methods)

**Interfaces:**
- Consumes: `IndexedSubjectPool` (Task 2), `SettingsManager::get/set_hidden_macros`, `hidden_macros_key_exists` (Task 3), `api->hardware().macros()`, home-panel long-press guards.
- Produces: XML callbacks `on_macro_row_clicked`, `on_macro_card_long_press`, `on_macros_edit_save`; internal model methods (below). Preserve `DEFINE_GLOBAL_PANEL` singleton + deferred-rebuild lifecycle.

**Model methods to add (header):**
```cpp
void enter_edit_mode();
void exit_edit_mode(bool save);         // save=true persists pending_hidden_
void toggle_row(size_t display_index);  // edit-mode row tap
void rebuild_rows();                    // compute displayed_, size pools, set count
std::set<std::string> seed_default_hidden() const;  // _-prefixed when key absent
// state:
std::vector<std::string> all_macros_;
std::set<std::string>    pending_hidden_;
std::vector<std::string> displayed_;
bool edit_mode_ = false;
helix::xml::IndexedSubjectPool name_pool_{"macro_name", helix::xml::IndexedSubjectPool::Type::String};
helix::xml::IndexedSubjectPool desc_pool_{"macro_desc", helix::xml::IndexedSubjectPool::Type::String, 256};  // descriptions can exceed the 64-char default
helix::xml::IndexedSubjectPool visible_pool_{"macro_visible", helix::xml::IndexedSubjectPool::Type::Int};
helix::xml::IndexedSubjectPool desc_hidden_pool_{"macro_desc_hidden", helix::xml::IndexedSubjectPool::Type::Int};
helix::xml::IndexedSubjectPool chevron_hidden_pool_{"macro_chevron_hidden", helix::xml::IndexedSubjectPool::Type::Int};
```
All five pools grow-only within a panel session and are `reclaim()`ed together in `on_ui_destroyed` (reclaim-on-close).

- [ ] **Step 1: Read** the full `ui_panel_macros.cpp` + `.h`. Map: `populate_macro_list` (:176), `create_macro_card` (:236), `on_macro_card_clicked` (:411, card-pointer identity), `clear_macro_list` (:164, `safe_clean_children`), `on_activate` (:127 deferred rebuild), `on_ui_destroyed` (:149). Note the dead `system_toggle_` lookup (:108) and `show_system_macros_` to remove.

- [ ] **Step 2: Write failing model tests** (drive the panel via its test-access friend or public model methods; mirror how other panel tests instantiate with a mock `api`/`PrinterState`). Cover: seed hides `_`-macros; enter edit shows all with correct checks; `toggle_row` flips `pending_hidden_` + `visible_pool_` int; exit(save=false) leaves saved set unchanged; exit(save=true) writes through SettingsManager. Drain the UpdateQueue before asserting ([L048]). Example:

```cpp
TEST_CASE_METHOD(MacrosPanelTestFixture, "toggle then save persists hidden set", "[macros][editmode]") {
    set_mock_macros({"CLEAN_NOZZLE", "LOAD", "_HOME_Z"});   // helper on fixture
    auto& p = MacrosPanel::instance();
    p.enter_edit_mode();
    // displayed_ == all 3, sorted; find index of CLEAN_NOZZLE and hide it
    p.toggle_row(index_of(p, "CLEAN_NOZZLE"));
    p.exit_edit_mode(/*save=*/true);
    UpdateQueue::instance().drain_queue_for_testing();
    auto hidden = SettingsManager::instance().get_hidden_macros();
    REQUIRE(std::find(hidden.begin(), hidden.end(), "CLEAN_NOZZLE") != hidden.end());
    REQUIRE(std::find(hidden.begin(), hidden.end(), "_HOME_Z") != hidden.end());   // seed kept
}
```

If a safe test-only accessor is needed, use a `MacrosPanelTestAccess` friend per [L088] (no `_for_testing` methods in prod headers).

- [ ] **Step 3: Run, expect FAIL.**

- [ ] **Step 4: Implement.**
  - Register the 3 int subjects (`macro_row_count`=0, `macro_edit_mode`=0, `macros_edit_save_hidden`=1) in `init_subjects()` with `register_deinit`.
  - `rebuild_rows()`: `all_macros_` = sorted `api->hardware().macros()` (include `_`-prefixed). `displayed_` = edit_mode_ ? all_macros_ : filter(not in saved hidden set). Grow ALL FIVE pools to `displayed_.size()` (`name_pool_`, `desc_pool_`, `visible_pool_`, `desc_hidden_pool_`, `chevron_hidden_pool_`). For each row `i` with macro `m = displayed_[i]`, look up its display name via `prettify_macro_name(m)` and its description/param knowledge via `helix::MacroParamCache::instance().get(m)` (see current `create_macro_card`, ui_panel_macros.cpp:236), then set:
    - `name_pool_.set_string(i, display_name)`
    - `desc_pool_.set_string(i, cached.description)`  // empty if none
    - `visible_pool_.set_int(i, edit_mode_ ? !pending_hidden_.count(m) : 1)`
    - `desc_hidden_pool_.set_int(i, (edit_mode_ || cached.description.empty()) ? 1 : 0)`  // no desc row in edit mode, or when macro has none
    - `chevron_hidden_pool_.set_int(i, (edit_mode_ || no_params(cached)) ? 1 : 0)`  // matches old `hide_chevron = no_params`, plus always-hidden in edit mode

    Set all values **before** `lv_subject_set_int(&macro_row_count_, displayed_.size())` (populate-then-count avoids a first-frame flash). `no_params(cached)` mirrors the current `cached.knowledge == helix::MacroParamKnowledge::KNOWN_NO_PARAMS`.
  - `seed_default_hidden()`: if `!SettingsManager::hidden_macros_key_exists()`, return `_`-prefixed names of all_macros_; else return saved set as `std::set`.
  - `enter_edit_mode()`: `pending_hidden_` = current effective hidden set (saved, or seed if absent); `edit_mode_=true`; `lv_subject_set_int(&macro_edit_mode_,1)`; `lv_subject_set_int(&macros_edit_save_hidden_,0)`; `rebuild_rows()`.
  - `exit_edit_mode(save)`: if save, `SettingsManager::set_hidden_macros({pending_hidden_.begin(), end()})` (seed-write on first save); `edit_mode_=false`; `macro_edit_mode_`=0; `macros_edit_save_hidden_`=1; `rebuild_rows()`.
  - `toggle_row(i)`: `m=displayed_[i]`; flip membership in `pending_hidden_`; `visible_pool_.set_int(i, !pending_hidden_.count(m))`. (No rebuild — reactive.)
  - `on_macro_row_clicked`: `i = atoi(user_data)`; if `edit_mode_` → `toggle_row(i)` else run `displayed_[i]` (existing run path).
  - `on_macro_card_long_press`: reuse home guards (`should_suppress_edit_mode`/`finger_drifted_since_press` — extract to a shared helper or copy with attribution); if not suppressed → `enter_edit_mode()`.
  - `on_macros_edit_save`: `exit_edit_mode(true)`.
  - `on_ui_destroyed`: null cached ptrs (existing), then reclaim ALL FIVE pools: `name_pool_.reclaim(); desc_pool_.reclaim(); visible_pool_.reclaim(); desc_hidden_pool_.reclaim(); chevron_hidden_pool_.reclaim();`.
  - Delete `create_macro_card`, `macro_entries_`, card-pointer identity, dead `system_toggle_` lookup, `show_system_macros_`.
  - Register the callbacks (`lv_xml_register_event_cb`) and the `macro_card` component if not already ([L014]).

- [ ] **Step 5: Run model tests, expect PASS**: `make test && ./build/bin/helix-tests "[macros][editmode]"`.

- [ ] **Step 6: Commit**:

```bash
git add include/ui_panel_macros.h src/ui/ui_panel_macros.cpp src/xml_registration.cpp tests/unit/test_ui_panel_macros_edit_mode.cpp
git commit -m "feat(macros): edit mode — long-press, per-macro visibility, Save, reclaim-on-close pools"
```

---

## Task 8: Integration — schema, build, runtime + screenshot verify

**Files:**
- Modify: `tools/xml-linter/schema/schema.json` (regen)
- Verify only; no new source.

- [ ] **Step 1: Regen XML schema** (new/changed components): `make regen-xml-schema`. Commit the schema ([L089]).

- [ ] **Step 2: Full build + tests**: `make -j && make test-run`. Expected: build clean, all tests pass. Re-run any flaky suites in isolation before blaming the diff (`reference_flaky_*`).

- [ ] **Step 3: Runtime verify** ([L060] pattern) — launch headless with mock printer, background task, tee to log:

```bash
./build/bin/helix-screen --test -vv -p macro_panel 2>&1 | tee /tmp/macro_edit.log
```

Then interact (long-press a macro → edit mode; toggle a checkbox; Save). Confirm via log grep (no errors/warnings about unresolved `macro_name_*` subjects, no L081/UAF telemetry) and a screenshot showing checkboxes. Ask the user to drive the long-press since it's interactive.

- [ ] **Step 4: Screenshot** normal list and edit mode (checkboxes visible, Save in header): `./scripts/screenshot.sh helix-screen macro-edit-mode macro_panel`. View the PNG and confirm rendering (real checkboxes, not tofu; alignment correct).

- [ ] **Step 5: Valgrind the panel path** if feasible (native harness) — open panel, enter edit, save, exit, close panel → prove reclaim leaves zero leaks / no UAF on reopen.

- [ ] **Step 6: Final commit** (schema + any verify fixes):

```bash
git add tools/xml-linter/schema/schema.json
git commit -m "chore(macros): regen XML schema for edit-mode components"
```

---

## Self-Review (completed against spec)

- **Reusable pattern** (spec §1): Tasks 1–2 (engine unregister + IndexedSubjectPool) ✓
- **Declarative rows** (spec §2): Tasks 5–6 (`macro_card` + `<repeat>`) ✓
- **C++ model & flow** (spec §3): Task 7 ✓
- **Persistence + first-run seed** (spec §4 + behavior): Task 3 + Task 7 `seed_default_hidden` ✓
- **Header Save button** (spec §5): Task 6 + Task 7 subjects ✓
- **Checkbox control** (behavior): Task 4 (glyphs) + Task 5 ✓
- **Retire system filter / dead lookup** (behavior): Task 7 Step 4 ✓
- **Lifetime safety** (spec crux): Task 2 reclaim protocol + Task 7 grow-only-within-session + reclaim on destroy; Valgrind gates in Tasks 2 & 8 ✓
- **Discard on back**: covered by `exit_edit_mode(false)` path + test (Task 7) — NOTE: wire the panel's back/nav to call `exit_edit_mode(false)` when `edit_mode_` (add to `on_deactivate`/back handler in Task 7 Step 4).
- **Type consistency**: pool API (`ensure_size`/`set_int`/`set_string`/`at`/`reclaim`), subject names (`macro_name_${i}`, `macro_visible_${i}`, `macro_edit_mode`, `macro_row_count`, `macros_edit_save_hidden`), callbacks (`on_macro_row_clicked`, `on_macro_card_long_press`, `on_macros_edit_save`) consistent across Tasks 2/5/6/7 ✓

**Testing tags:** `[unregister]`, `[pool]`, `[settings][macros]`, `[macros][editmode]`.
