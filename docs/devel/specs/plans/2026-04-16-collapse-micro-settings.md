# Collapse Micro XML Settings Variants — Implementation Plan

> ⚠️ **Historical record (verified 2026-08-09) - not instructions.**
> **Status: SHIPPED.** The 41 `- [ ]` boxes were never ticked and do not mean the work is
> outstanding. All three prescribed attributes live in
> `lib/helix-xml/src/xml/parsers/lv_xml_obj_parser.c`, and the seven `ui_xml/micro/setting*.xml`
> variants this plan deletes are already gone. `ui_xml/test_hidden_if.xml` was never created.
>
> Code line numbers and file paths below may have drifted. Follow the **symbol**, not
> the number, and verify every predicate against current code before relying on
> anything here.

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Eliminate 7 duplicate `ui_xml/micro/` settings files by making setting row components responsive and adding an info icon for on-demand help text on small screens.

**Architecture:** Three layers of change — (1) XML engine gets `hidden_if_empty`/`hidden_if_prop_eq`/`hidden_if_prop_not_eq` attributes for parse-time string comparison, (2) setting row components gain `bind_style` for responsive padding and `bind_flag` for breakpoint-aware description/info-icon visibility, (3) overlay/panel XMLs drop micro-specific props and 7 micro/ files are deleted.

**Tech Stack:** C (helix-xml parser), C++ (callback registration), XML (component definitions)

**Spec:** `docs/devel/specs/2026-04-16-collapse-micro-settings-design.md`

---

### Task 1: Add `hidden_if_prop_eq`, `hidden_if_prop_not_eq`, and `hidden_if_empty` to XML Engine

**Files:**
- Modify: `lib/helix-xml/src/xml/parsers/lv_xml_obj_parser.c:106` (add new attribute handlers)

These are parse-time, one-shot attributes. By the time `lv_xml_obj_apply()` runs, `$prop` values are already resolved by `resolve_params()` in `lv_xml.c:882-925`. So the implementation just does string comparison on the resolved value.

- [ ] **Step 1: Add `hidden_if_prop_eq` handler**

In `lib/helix-xml/src/xml/parsers/lv_xml_obj_parser.c`, add after line 106 (`hidden` handler):

```c
        else if(lv_streq("hidden_if_prop_eq", name)) {
            /* Format: "resolved_value|ref_value" — hide if resolved_value == ref_value
             * At this point $prop references are already resolved by resolve_params(),
             * so value contains something like "Some description|" or "|" */
            const char * sep = lv_strchr(value, '|');
            if(sep) {
                size_t val_len = (size_t)(sep - value);
                const char * ref = sep + 1;
                size_t ref_len = lv_strlen(ref);
                if(val_len == ref_len && (val_len == 0 || lv_memcmp(value, ref, val_len) == 0)) {
                    lv_obj_add_flag(item, LV_OBJ_FLAG_HIDDEN);
                }
            }
        }
        else if(lv_streq("hidden_if_prop_not_eq", name)) {
            const char * sep = lv_strchr(value, '|');
            if(sep) {
                size_t val_len = (size_t)(sep - value);
                const char * ref = sep + 1;
                size_t ref_len = lv_strlen(ref);
                bool eq = (val_len == ref_len && (val_len == 0 || lv_memcmp(value, ref, val_len) == 0));
                if(!eq) {
                    lv_obj_add_flag(item, LV_OBJ_FLAG_HIDDEN);
                }
            }
        }
        else if(lv_streq("hidden_if_empty", name)) {
            /* Shortcut for hidden_if_prop_eq="$prop|" — hide if resolved value is empty */
            if(value[0] == '\0') {
                lv_obj_add_flag(item, LV_OBJ_FLAG_HIDDEN);
            }
        }
```

**Note on `lv_strchr`:** Check if it exists in LVGL's string utils. If not, use standard `strchr` — helix-xml already includes `<string.h>` transitively. Similarly for `lv_memcmp` — if unavailable, use `memcmp`.

- [ ] **Step 2: Build and verify compilation**

Run: `make -j`
Expected: Clean build, no errors or warnings from the new code.

- [ ] **Step 3: Smoke test with a temporary XML file**

Create a temporary test component `ui_xml/test_hidden_if.xml`:

```xml
<?xml version="1.0"?>
<component>
  <api>
    <prop name="desc" type="string" default=""/>
    <prop name="mode" type="string" default="basic"/>
  </api>
  <view extends="lv_obj" width="100%" height="content" flex_flow="column">
    <!-- Should be hidden when desc="" (default) -->
    <lv_obj name="info_icon" width="20" height="20" hidden_if_empty="$desc"/>
    <!-- Should be hidden when mode="advanced" -->
    <lv_obj name="advanced_only" width="20" height="20" hidden_if_prop_eq="$mode|advanced"/>
    <!-- Should be hidden when mode != "basic" -->
    <lv_obj name="basic_only" width="20" height="20" hidden_if_prop_not_eq="$mode|basic"/>
  </view>
</component>
```

Register it in `main.cpp` temporarily and launch with `--test -vv`. Verify in logs or by widget inspection that:
- `info_icon` is hidden (desc is empty default)
- `advanced_only` is NOT hidden (mode="basic", not "advanced")
- `basic_only` is NOT hidden (mode="basic", matches ref)

- [ ] **Step 4: Remove temporary test file and commit**

Delete `ui_xml/test_hidden_if.xml` and its registration.

```bash
git add lib/helix-xml/src/xml/parsers/lv_xml_obj_parser.c
git commit -m "feat(xml): add hidden_if_prop_eq, hidden_if_prop_not_eq, hidden_if_empty attributes (#805)"
```

---

### Task 2: Make `setting_toggle_row` Responsive with Info Icon

**Files:**
- Modify: `ui_xml/setting_toggle_row.xml` (add compact style, info icon, breakpoint bindings)
- Modify: `src/xml_registration.cpp` (register `on_setting_info_clicked` callback)

- [ ] **Step 1: Register the global info icon callback**

In `src/xml_registration.cpp`, add the callback function near the other static callbacks (around line 200):

```cpp
static void on_setting_info_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[Settings] on_setting_info_clicked");
    auto* info_btn = lv_event_get_current_target(e);
    if (!info_btn) return;
    // The info_btn sits inside the row's view root.
    // Walk up to find the row, then search for "description" by name.
    auto* row = lv_obj_get_parent(info_btn);
    if (!row) return;
    auto* desc = lv_obj_find_by_name(row, "description");
    if (!desc) return;
    if (lv_obj_has_flag(desc, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_remove_flag(desc, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(desc, LV_OBJ_FLAG_HIDDEN);
    }
    LVGL_SAFE_EVENT_CB_END();
}
```

Register it alongside the other global callbacks (around line 214):

```cpp
lv_xml_register_event_cb(nullptr, "on_setting_info_clicked", on_setting_info_clicked);
```

- [ ] **Step 2: Update `setting_toggle_row.xml`**

Replace the full contents of `ui_xml/setting_toggle_row.xml` with:

```xml
<?xml version="1.0"?>
<!-- Copyright (C) 2025-2026 356C LLC -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Reusable Settings Toggle Row Component -->
<!-- Responsive: compact padding on micro breakpoint, info icon for descriptions -->
<component>
  <api>
    <prop name="label" type="string" default="Setting"/>
    <prop name="label_tag" type="string" default=""/>
    <prop name="icon" type="string" default=""/>
    <prop name="description" type="string" default=""/>
    <prop name="description_tag" type="string" default=""/>
    <prop name="subject" type="string" default=""/>
    <prop name="callback" type="string" default=""/>
    <prop name="disabled" type="string" default=""/>
  </api>
  <styles>
    <style name="label_enabled" text_color="#text"/>
    <style name="label_disabled" text_color="#text_subtle"/>
    <style name="pad_compact" pad_left="#space_md" pad_right="#space_md"
           pad_top="#space_xs" pad_bottom="#space_xs" pad_gap="#space_xs"/>
  </styles>
  <view name="setting_row"
        extends="lv_obj" width="100%" height="content" flex_flow="row" style_flex_main_place="start"
        style_flex_cross_place="center" style_bg_opa="0" style_border_width="0" style_pad_left="#space_lg"
        style_pad_right="#space_lg" style_pad_top="#space_md" style_pad_bottom="#space_md" style_pad_gap="#space_sm"
        scrollable="false">
    <bind_style name="pad_compact" subject="ui_breakpoint" ref_value="0"/>
    <!-- Left side: Optional icon -->
    <icon name="row_icon" src="$icon" size="sm" variant="secondary"/>
    <!-- Middle: Label and optional description -->
    <lv_obj height="content"
            style_pad_all="0" flex_flow="column" style_pad_gap="#space_xs" flex_grow="1" scrollable="false">
      <text_body name="label" text="$label" translation_tag="$label_tag">
        <bind_style name="label_enabled" subject="$disabled" ref_value="0"/>
        <bind_style name="label_disabled" subject="$disabled" ref_value="1"/>
      </text_body>
      <!-- Description: hidden on micro/tiny by default -->
      <text_small name="description" text="$description" translation_tag="$description_tag">
        <bind_flag_if_lt subject="ui_breakpoint" flag="hidden" ref_value="2"/>
      </text_small>
    </lv_obj>
    <!-- Info icon: hidden if no description, hidden on medium+ -->
    <lv_obj name="info_btn" width="content" height="content"
            style_pad_all="2" style_bg_opa="0" style_border_width="0"
            clickable="true" event_bubble="false" scrollable="false"
            hidden_if_empty="$description">
      <bind_flag_if_gt subject="ui_breakpoint" flag="hidden" ref_value="1"/>
      <icon src="information_outline" size="xs" variant="muted"
            clickable="false" event_bubble="true"/>
      <event_cb trigger="clicked" callback="on_setting_info_clicked"/>
    </lv_obj>
    <!-- Right side: Switch toggle -->
    <ui_switch name="toggle" size="small">
      <bind_state_if_eq subject="$subject" state="checked" ref_value="1"/>
      <bind_state_if_eq subject="$disabled" state="disabled" ref_value="1"/>
      <event_cb trigger="value_changed" callback="$callback"/>
    </ui_switch>
  </view>
</component>
```

- [ ] **Step 3: Build and test**

Run: `make -j`
Expected: Clean build.

Launch: `./build/bin/helix-screen --test -vv -p settings`
Navigate to Display & Sound. Verify toggle rows render correctly at default breakpoint (descriptions visible, no info icons on medium+ screens).

- [ ] **Step 4: Commit**

```bash
git add ui_xml/setting_toggle_row.xml src/xml_registration.cpp
git commit -m "feat(settings): make setting_toggle_row responsive with info icon (#805)"
```

---

### Task 3: Make `setting_dropdown_row` Responsive with Info Icon

**Files:**
- Modify: `ui_xml/setting_dropdown_row.xml`

- [ ] **Step 1: Update `setting_dropdown_row.xml`**

Replace the full contents with:

```xml
<?xml version="1.0"?>
<!-- Copyright (C) 2025-2026 356C LLC -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Reusable Settings Dropdown Row Component -->
<!-- Responsive: compact padding on micro breakpoint, info icon for descriptions -->
<component>
  <api>
    <prop name="label" type="string" default="Setting"/>
    <prop name="label_tag" type="string" default=""/>
    <prop name="icon" type="string" default=""/>
    <prop name="description" type="string" default=""/>
    <prop name="description_tag" type="string" default=""/>
    <prop name="options" type="string" default=""/>
    <prop name="options_tag" type="string" default=""/>
    <prop name="callback" type="string" default=""/>
    <prop name="disabled" type="string" default=""/>
    <prop name="dropdown_width" type="string" default="33%"/>
  </api>
  <styles>
    <style name="label_enabled" text_color="#text"/>
    <style name="label_disabled" text_color="#text_subtle"/>
    <style name="pad_compact" pad_left="#space_md" pad_right="#space_md"
           pad_top="#space_xs" pad_bottom="#space_xs" pad_gap="#space_xs"/>
  </styles>
  <view name="dropdown_row"
        extends="lv_obj" width="100%" height="content" flex_flow="row" style_flex_main_place="start"
        style_flex_cross_place="center" style_bg_opa="0" style_border_width="0" style_pad_left="#space_lg"
        style_pad_right="#space_lg" style_pad_top="#space_md" style_pad_bottom="#space_md" style_pad_gap="#space_sm"
        scrollable="false">
    <bind_style name="pad_compact" subject="ui_breakpoint" ref_value="0"/>
    <!-- Left side: Optional icon -->
    <icon name="row_icon" src="$icon" size="sm" variant="secondary"/>
    <!-- Middle: Label and optional description -->
    <lv_obj height="content" width="0"
            style_pad_all="0" flex_flow="column" style_pad_gap="#space_xs" flex_grow="1" scrollable="false">
      <text_body name="label" text="$label" translation_tag="$label_tag" long_mode="wrap" width="100%">
        <bind_style name="label_enabled" subject="$disabled" ref_value="0"/>
        <bind_style name="label_disabled" subject="$disabled" ref_value="1"/>
      </text_body>
      <!-- Description: hidden on micro/tiny by default -->
      <text_small name="description" text="$description" translation_tag="$description_tag"
                  long_mode="wrap" width="100%">
        <bind_flag_if_lt subject="ui_breakpoint" flag="hidden" ref_value="2"/>
      </text_small>
    </lv_obj>
    <!-- Info icon: hidden if no description, hidden on medium+ -->
    <lv_obj name="info_btn" width="content" height="content"
            style_pad_all="2" style_bg_opa="0" style_border_width="0"
            clickable="true" event_bubble="false" scrollable="false"
            hidden_if_empty="$description">
      <bind_flag_if_gt subject="ui_breakpoint" flag="hidden" ref_value="1"/>
      <icon src="information_outline" size="xs" variant="muted"
            clickable="false" event_bubble="true"/>
      <event_cb trigger="clicked" callback="on_setting_info_clicked"/>
    </lv_obj>
    <!-- Right side: Dropdown -->
    <lv_dropdown name="dropdown"
                 width="$dropdown_width" height="content" options="$options" options_tag="$options_tag"
                 style_radius="#border_radius" style_border_width="0">
      <bind_state_if_eq subject="$disabled" state="disabled" ref_value="1"/>
      <event_cb trigger="value_changed" callback="$callback"/>
    </lv_dropdown>
  </view>
</component>
```

**Key changes from original:**
- Removed `hide_description` prop (no longer needed)
- Removed `hidden="$hide_description"` from description text_small
- Added `pad_compact` style + `bind_style`
- Added `bind_flag_if_lt` on description for breakpoint-based hiding
- Added info icon with `hidden_if_empty` and `bind_flag_if_gt`

- [ ] **Step 2: Build and verify**

Run: `make -j`
Expected: Clean build. No references to `hide_description` remain in this file.

- [ ] **Step 3: Commit**

```bash
git add ui_xml/setting_dropdown_row.xml
git commit -m "feat(settings): make setting_dropdown_row responsive with info icon (#805)"
```

---

### Task 4: Make `setting_slider_row` Responsive with Info Icon

**Files:**
- Modify: `ui_xml/setting_slider_row.xml`

- [ ] **Step 1: Update `setting_slider_row.xml`**

Replace the full contents with:

```xml
<?xml version="1.0"?>
<!-- Copyright (C) 2025-2026 356C LLC -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Reusable Settings Slider Row Component -->
<!-- Responsive: compact padding on micro breakpoint, info icon for descriptions -->
<component>
  <api>
    <prop name="label" type="string" default="Setting"/>
    <prop name="label_tag" type="string" default=""/>
    <prop name="icon" type="string" default=""/>
    <prop name="description" type="string" default=""/>
    <prop name="description_tag" type="string" default=""/>
    <prop name="min" type="string" default="0"/>
    <prop name="max" type="string" default="100"/>
    <prop name="value" type="string" default="50"/>
    <prop name="callback" type="string" default=""/>
    <prop name="user_data" type="string" default=""/>
    <prop name="min_icon" type="string" default=""/>
    <prop name="max_icon" type="string" default=""/>
    <prop name="disabled" type="string" default=""/>
  </api>
  <styles>
    <style name="label_enabled" text_color="#text"/>
    <style name="label_disabled" text_color="#text_subtle"/>
    <style name="pad_compact" pad_left="#space_md" pad_right="#space_md"
           pad_top="#space_xs" pad_bottom="#space_xs"/>
    <style name="pad_compact_slider" pad_top="#space_xs" pad_bottom="#space_xs"
           margin_top="0"/>
  </styles>
  <view name="slider_row"
        extends="lv_obj" width="100%" height="content" flex_flow="column" style_bg_opa="0" style_border_width="0"
        style_pad_left="#space_lg" style_pad_right="#space_lg" style_pad_top="#space_md" style_pad_bottom="#space_md"
        style_pad_gap="#space_xs" scrollable="false">
    <bind_style name="pad_compact" subject="ui_breakpoint" ref_value="0"/>
    <!-- Top row: Icon, label, info icon, value display -->
    <lv_obj width="100%"
            height="content" flex_flow="row" style_flex_main_place="space_between" style_flex_cross_place="center"
            style_pad_all="0" scrollable="false">
      <!-- Left side: Icon and label -->
      <lv_obj height="content"
              flex_flow="row" style_flex_cross_place="center" style_pad_gap="#space_sm" style_pad_all="0"
              scrollable="false">
        <icon name="row_icon" src="$icon" size="sm" variant="secondary"/>
        <text_body name="label" text="$label" translation_tag="$label_tag">
          <bind_style name="label_enabled" subject="$disabled" ref_value="0"/>
          <bind_style name="label_disabled" subject="$disabled" ref_value="1"/>
        </text_body>
        <!-- Info icon: hidden if no description, hidden on medium+ -->
        <lv_obj name="info_btn" width="content" height="content"
                style_pad_all="2" style_bg_opa="0" style_border_width="0"
                clickable="true" event_bubble="false" scrollable="false"
                hidden_if_empty="$description">
          <bind_flag_if_gt subject="ui_breakpoint" flag="hidden" ref_value="1"/>
          <icon src="information_outline" size="xs" variant="muted"
                clickable="false" event_bubble="true"/>
          <event_cb trigger="clicked" callback="on_setting_info_clicked"/>
        </lv_obj>
      </lv_obj>
      <!-- Right side: Current value -->
      <text_muted name="value_label" text="$value"/>
    </lv_obj>
    <!-- Description: hidden on micro/tiny by default -->
    <text_small name="description" text="$description" translation_tag="$description_tag">
      <bind_flag_if_lt subject="ui_breakpoint" flag="hidden" ref_value="2"/>
    </text_small>
    <!-- Slider with optional min/max icons -->
    <lv_obj name="slider_container" width="100%"
            height="content" flex_flow="row" style_flex_cross_place="center" style_pad_left="0" style_pad_right="0"
            style_pad_top="#space_sm" style_pad_bottom="#space_sm" style_pad_gap="#space_sm"
            style_margin_top="#space_xs" scrollable="false">
      <bind_style name="pad_compact_slider" subject="ui_breakpoint" ref_value="0"/>
      <icon name="min_icon" src="$min_icon" size="xs" variant="secondary"/>
      <lv_slider name="slider" flex_grow="1" min_value="$min" max_value="$max" value="$value">
        <bind_state_if_eq subject="$disabled" state="disabled" ref_value="1"/>
        <event_cb trigger="value_changed" callback="$callback" user_data="$user_data"/>
      </lv_slider>
      <icon name="max_icon" src="$max_icon" size="xs" variant="secondary"/>
    </lv_obj>
  </view>
</component>
```

**Key differences:**
- Info icon is placed inside the top row (next to label) since slider rows have a different layout (column with label row + slider row)
- `pad_compact_slider` style handles the slider container's tighter padding
- The `on_setting_info_clicked` callback needs to find `description` from `info_btn`'s grandparent (the view root). Since `lv_obj_find_by_name` is recursive, walking to `row = lv_obj_get_parent(lv_obj_get_parent(info_btn))` or using the current parent walk will work — `lv_obj_find_by_name` searches children recursively from the given parent.

**Note:** The callback registered in Task 2 walks up one level (`lv_obj_get_parent(info_btn)`). For the slider row, the info_btn is nested deeper (inside the label row container). The callback should walk up until it finds the row root. Update the callback to walk up multiple levels:

```cpp
static void on_setting_info_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[Settings] on_setting_info_clicked");
    auto* info_btn = lv_event_get_current_target(e);
    if (!info_btn) return;
    // Walk up to the component's view root — find "description" from there.
    // Start from info_btn's parent and walk up until we find a level that
    // contains a child named "description".
    auto* parent = lv_obj_get_parent(info_btn);
    lv_obj_t* desc = nullptr;
    while (parent) {
        desc = lv_obj_find_by_name(parent, "description");
        if (desc) break;
        parent = lv_obj_get_parent(parent);
    }
    if (!desc) return;
    if (lv_obj_has_flag(desc, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_remove_flag(desc, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(desc, LV_OBJ_FLAG_HIDDEN);
    }
    LVGL_SAFE_EVENT_CB_END();
}
```

**Update the callback from Task 2** with this version before proceeding. The walk-up-until-found pattern handles all nesting depths.

- [ ] **Step 2: Update callback in `xml_registration.cpp`**

Replace the `on_setting_info_clicked` callback body from Task 2 with the walk-up version shown above.

- [ ] **Step 3: Build and verify**

Run: `make -j`
Expected: Clean build.

- [ ] **Step 4: Commit**

```bash
git add ui_xml/setting_slider_row.xml src/xml_registration.cpp
git commit -m "feat(settings): make setting_slider_row responsive with info icon (#805)"
```

---

### Task 5: Make `setting_action_row` Responsive with Info Icon

**Files:**
- Modify: `ui_xml/setting_action_row.xml`

- [ ] **Step 1: Update `setting_action_row.xml`**

The action row is special — its root is `lv_button` (for whole-row clickability) with a content container inside. The info icon must NOT bubble clicks to the parent button (otherwise tapping info also triggers the row's action callback).

Replace the full contents with:

```xml
<?xml version="1.0"?>
<!-- Copyright (C) 2025-2026 356C LLC -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Reusable Settings Action Row Component -->
<!-- Responsive: compact padding on micro breakpoint, info icon for descriptions -->
<component>
  <api>
    <prop name="label" type="string" default="Action"/>
    <prop name="label_tag" type="string" default=""/>
    <prop name="icon" type="string" default=""/>
    <prop name="description" type="string" default=""/>
    <prop name="description_tag" type="string" default=""/>
    <prop name="bind_description" type="string"/>
    <prop name="callback" type="string" default=""/>
    <prop name="disabled" type="string" default=""/>
  </api>
  <styles>
    <style name="label_enabled" text_color="#text"/>
    <style name="label_disabled" text_color="#text_subtle"/>
    <style name="pad_compact" pad_left="#space_md" pad_right="#space_md"
           pad_top="#space_xs" pad_bottom="#space_xs" pad_gap="#space_xs"/>
  </styles>
  <!-- Root button - receives all bubbled click events -->
  <view name="action_row"
        extends="lv_button" width="100%" height="content" style_bg_opa="0" style_border_width="0" style_pad_all="0"
        style_shadow_width="0" style_radius="0" scrollable="false">
    <bind_state_if_eq subject="$disabled" state="disabled" ref_value="1"/>
    <!-- Content container: clickable=false so clicks bubble to button -->
    <lv_obj width="100%"
            height="content" flex_flow="row" style_flex_main_place="start" style_flex_cross_place="center"
            style_pad_left="#space_lg" style_pad_right="#space_lg" style_pad_top="#space_md"
            style_pad_bottom="#space_md" style_pad_gap="#space_sm" scrollable="false" clickable="false"
            event_bubble="true">
      <bind_style name="pad_compact" subject="ui_breakpoint" ref_value="0"/>
      <!-- Left side: Icon -->
      <icon name="row_icon" src="$icon" size="sm" variant="secondary" clickable="false" event_bubble="true"/>
      <!-- Middle: Label and optional description -->
      <lv_obj height="content"
              style_pad_all="0" flex_flow="column" style_pad_gap="#space_xs" flex_grow="1" scrollable="false"
              clickable="false" event_bubble="true">
        <text_body name="label" text="$label" translation_tag="$label_tag">
          <bind_style name="label_enabled" subject="$disabled" ref_value="0"/>
          <bind_style name="label_disabled" subject="$disabled" ref_value="1"/>
        </text_body>
        <!-- Description: hidden on micro/tiny by default -->
        <text_small name="description"
                    text="$description" translation_tag="$description_tag"
                    bind_text="$bind_description">
          <bind_flag_if_lt subject="ui_breakpoint" flag="hidden" ref_value="2"/>
        </text_small>
      </lv_obj>
      <!-- Info icon: hidden if no description, hidden on medium+ -->
      <!-- event_bubble="false" prevents info tap from triggering row action -->
      <lv_obj name="info_btn" width="content" height="content"
              style_pad_all="2" style_bg_opa="0" style_border_width="0"
              clickable="true" event_bubble="false" scrollable="false"
              hidden_if_empty="$description">
        <bind_flag_if_gt subject="ui_breakpoint" flag="hidden" ref_value="1"/>
        <icon src="information_outline" size="xs" variant="muted"
              clickable="false" event_bubble="true"/>
        <event_cb trigger="clicked" callback="on_setting_info_clicked"/>
      </lv_obj>
      <!-- Right side: Chevron icon -->
      <icon src="chevron_right" size="sm" variant="secondary" clickable="false" event_bubble="true"/>
    </lv_obj>
    <!-- Floating overlay: Catches clicks and bubbles to parent, covers full button -->
    <lv_obj width="100%"
            height="100%" align="top_left" style_pad_all="0" clickable="true" event_bubble="true" floating="true"
            scrollable="false"/>
    <!-- Declarative event callback -->
    <event_cb trigger="clicked" callback="$callback"/>
  </view>
</component>
```

**Key considerations:**
- The `pad_compact` `bind_style` is on the content container (not the root button), matching where padding was defined in the original
- `info_btn` has `event_bubble="false"` so its click doesn't trigger the row's action callback
- The floating overlay still captures clicks for the row action — but the info_btn sits in the normal flow above the floating overlay. Since the info_btn is `clickable="true"` and the floating overlay is `floating="true"` with `align="top_left"`, the info_btn should receive its own click events. **Verify this during testing** — if the floating overlay intercepts info icon clicks, the info_btn may need `floating="true"` with a higher z-order, or the floating overlay needs to exclude the info_btn area.

- [ ] **Step 2: Build and test**

Run: `make -j`
Expected: Clean build.

Launch: `./build/bin/helix-screen --test -vv -p settings`
Verify: Action rows in settings panel render correctly. Tapping a row still navigates. On micro breakpoint, info icon should be tappable without triggering navigation.

- [ ] **Step 3: Commit**

```bash
git add ui_xml/setting_action_row.xml
git commit -m "feat(settings): make setting_action_row responsive with info icon (#805)"
```

---

### Task 6: Make `setting_section_header` Responsive

**Files:**
- Modify: `ui_xml/setting_section_header.xml`

- [ ] **Step 1: Update `setting_section_header.xml`**

The section header has no description or info icon — just padding differences. The micro variant also removes `hide_icon` prop (always shows icon). We keep `hide_icon` for backward compatibility.

Add the `pad_compact` style. In `ui_xml/setting_section_header.xml`, add the style and bind_style:

After the existing `</styles>` close tag — wait, this file has no `<styles>` block. Add one. Replace the `<view>` section:

```xml
  <styles>
    <style name="pad_compact" pad_left="#space_md" pad_right="#space_md"
           pad_top="#space_sm" pad_bottom="#space_xxs"/>
  </styles>
  <view extends="lv_obj"
        width="100%" height="content" style_bg_opa="0" style_border_width="0" style_pad_left="#space_lg"
        style_pad_right="#space_lg" style_pad_top="#space_lg" style_pad_bottom="#space_xs" flex_flow="column"
        style_pad_gap="0" scrollable="false">
    <bind_style name="pad_compact" subject="ui_breakpoint" ref_value="0"/>
```

The rest of the file stays identical.

- [ ] **Step 2: Build and verify**

Run: `make -j`
Expected: Clean build.

- [ ] **Step 3: Commit**

```bash
git add ui_xml/setting_section_header.xml
git commit -m "feat(settings): make setting_section_header responsive (#805)"
```

---

### Task 7: Update `settings_display_sound_overlay.xml` and Delete Micro Variant

**Files:**
- Modify: `ui_xml/settings_display_sound_overlay.xml` (add responsive container padding, remove hide_description props)
- Delete: `ui_xml/micro/settings_display_sound_overlay.xml`

- [ ] **Step 1: Update the standard overlay**

In `ui_xml/settings_display_sound_overlay.xml`, make these changes:

**a) Add styles block and bind_style for the content container.** Before the `<view>` tag (after `<component>`), there's no `<styles>` block — add one:

After line 7 (`<component>`), before line 8 (`<view ...>`), insert:

```xml
  <styles>
    <style name="content_compact" pad_left="#space_md" pad_right="#space_md"
           pad_bottom="#space_sm"/>
  </styles>
```

**b) Add bind_style to the content container.** On the `overlay_content` element (line 12), add `bind_style` as a child:

After the opening `<lv_obj name="overlay_content" ...>` tag, add:

```xml
      <bind_style name="content_compact" subject="ui_breakpoint" ref_value="0"/>
```

**c) Add responsive padding to the brightness section.** The brightness section (line 78-106) has hardcoded `#space_lg`/`#space_md` padding in the standard version, but the micro uses `#space_md`/`#space_xs`. Add a compact style. Since this is a raw `lv_obj` (not a component with its own styles block), use `bind_style_prop`:

After the `<bind_flag_if_eq subject="settings_has_dimming" .../>` child (line 81), add:

```xml
        <bind_style_prop prop="pad_left" subject="ui_breakpoint" ref_value="0" value="#space_md"/>
        <bind_style_prop prop="pad_right" subject="ui_breakpoint" ref_value="0" value="#space_md"/>
        <bind_style_prop prop="pad_top" subject="ui_breakpoint" ref_value="0" value="#space_xs"/>
        <bind_style_prop prop="pad_bottom" subject="ui_breakpoint" ref_value="0" value="#space_xs"/>
```

**Note:** Check if `bind_style_prop` supports the `value` attribute this way. Look at `lv_obj_xml_bind_style_prop_apply()` in the parser. If `bind_style_prop` requires a named style rather than inline values, use a component-level `<styles>` block instead. **Alternative:** Since the brightness section is inside a component's view, we can define a style in the component's `<styles>` block:

Add to the styles block:
```xml
    <style name="brightness_compact" pad_left="#space_md" pad_right="#space_md"
           pad_top="#space_xs" pad_bottom="#space_xs"/>
```

And on the brightness_section `lv_obj`:
```xml
        <bind_style name="brightness_compact" subject="ui_breakpoint" ref_value="0"/>
```

**d) Update bottom spacer.** Change line 193 from `height="#space_lg"` to use a responsive approach. Since this is a simple spacer, we can use `bind_style_prop` or just keep `#space_lg` (the token itself is responsive — on micro, `#space_lg` = 12px vs 20px on larger breakpoints). Actually, looking at the micro variant: it uses `#space_md` for the bottom spacer. This is a one-tier downshift. Add a style:

Add to styles block:
```xml
    <style name="spacer_compact" height="#space_md"/>
```

And on the bottom spacer `lv_obj`:
```xml
      <bind_style name="spacer_compact" subject="ui_breakpoint" ref_value="0"/>
```

- [ ] **Step 2: Delete the micro variant**

```bash
git rm ui_xml/micro/settings_display_sound_overlay.xml
```

- [ ] **Step 3: Build and test**

Run: `make -j`
Expected: Clean build. No compilation errors from missing file (it's loaded at runtime via LayoutManager).

Launch: `./build/bin/helix-screen --test -vv -p settings`
Navigate to Display & Sound. Verify all setting rows render with descriptions visible (medium+ breakpoint).

- [ ] **Step 4: Commit**

```bash
git add ui_xml/settings_display_sound_overlay.xml
git commit -m "feat(settings): make display_sound_overlay responsive, delete micro variant (#805)"
```

---

### Task 8: Update `settings_panel.xml` and Delete Micro Variant

**Files:**
- Modify: `ui_xml/settings_panel.xml` (responsive bottom spacer)
- Delete: `ui_xml/micro/settings_panel.xml`

- [ ] **Step 1: Update standard settings panel**

The only difference is the bottom spacer: `#space_lg` (standard) vs `#space_md` (micro).

In `ui_xml/settings_panel.xml`, add a styles block after `<component>` (line 7):

```xml
  <styles>
    <style name="spacer_compact" height="#space_md"/>
  </styles>
```

On the bottom spacer `lv_obj` (line 62), add a child:

```xml
    <lv_obj width="100%" height="#space_lg" scrollable="false">
      <bind_style name="spacer_compact" subject="ui_breakpoint" ref_value="0"/>
    </lv_obj>
```

Note: This changes the spacer from a self-closing tag to one with a child. The spacer is a simple `lv_obj` — adding a `bind_style` child should work fine.

- [ ] **Step 2: Delete the micro variant**

```bash
git rm ui_xml/micro/settings_panel.xml
```

- [ ] **Step 3: Build and verify**

Run: `make -j`
Expected: Clean build.

- [ ] **Step 4: Commit**

```bash
git add ui_xml/settings_panel.xml
git commit -m "feat(settings): make settings_panel responsive, delete micro variant (#805)"
```

---

### Task 9: Delete Remaining Micro Setting Row Files

**Files:**
- Delete: `ui_xml/micro/setting_toggle_row.xml`
- Delete: `ui_xml/micro/setting_slider_row.xml`
- Delete: `ui_xml/micro/setting_dropdown_row.xml`
- Delete: `ui_xml/micro/setting_action_row.xml`
- Delete: `ui_xml/micro/setting_section_header.xml`

- [ ] **Step 1: Delete all 5 micro setting component files**

```bash
git rm ui_xml/micro/setting_toggle_row.xml
git rm ui_xml/micro/setting_slider_row.xml
git rm ui_xml/micro/setting_dropdown_row.xml
git rm ui_xml/micro/setting_action_row.xml
git rm ui_xml/micro/setting_section_header.xml
```

- [ ] **Step 2: Build and verify**

Run: `make -j`
Expected: Clean build. These files are loaded at runtime — no compile-time references.

- [ ] **Step 3: Verify no other XML files reference these micro variants**

Search for any XML files that might explicitly include or reference the micro setting components:

```bash
grep -r "micro/setting_" ui_xml/ src/ --include="*.xml" --include="*.cpp" --include="*.h"
```

Expected: No matches (these are loaded via LayoutManager's path resolution, not explicit references).

- [ ] **Step 4: Commit**

```bash
git commit -m "chore(settings): delete 5 redundant micro/ setting component files (#805)"
```

---

### Task 10: Visual Testing on Both Breakpoints

**Files:** None (testing only)

- [ ] **Step 1: Test on default breakpoint (800x480 or similar)**

Launch: `./build/bin/helix-screen --test -vv -p settings`

Verify:
- Settings panel: All 6 action rows show with descriptions, proper padding
- Display & Sound overlay: All sections render, descriptions visible, no info icons
- Brightness slider section has proper padding
- Section headers have proper spacing

- [ ] **Step 2: Test on micro breakpoint (480x272)**

Launch: `./build/bin/helix-screen --test -vv -p settings -r 480x272`

(Check if `-r` flag or `HELIX_RESOLUTION` env var sets resolution. If not available, use `HELIX_DISPLAY_WIDTH=480 HELIX_DISPLAY_HEIGHT=272` or similar.)

Verify:
- Settings panel: Action rows show with compact padding, descriptions hidden, info icons visible
- Tapping info icon expands description inline
- Tapping info icon again collapses it
- Display & Sound overlay: Compact padding, descriptions hidden, info icons present
- Section headers have compact spacing
- Rows without descriptions show no info icon

- [ ] **Step 3: Test edge case — action row info icon doesn't trigger navigation**

On micro breakpoint, in the settings panel, tap an info icon on an action row. Verify:
- Description toggles
- Row action (navigation) does NOT fire
- Tapping the row itself (outside info icon) still navigates normally

- [ ] **Step 4: Verify no regression on other panels using setting rows**

Search for other panels that use setting row components:

```bash
grep -r "setting_toggle_row\|setting_dropdown_row\|setting_slider_row\|setting_action_row\|setting_section_header" ui_xml/ --include="*.xml" -l
```

Launch the app and spot-check 2-3 of these panels to ensure they still render correctly.

---

### Task 11: Update Documentation

**Files:**
- Modify: `docs/devel/UI_CONTRIBUTOR_GUIDE.md`
- Modify: `docs/devel/LVGL9_XML_GUIDE.md`

- [ ] **Step 1: Add responsive settings convention to UI Contributor Guide**

In `docs/devel/UI_CONTRIBUTOR_GUIDE.md`, find the section about breakpoints or settings components. Add:

```markdown
### Responsive Setting Rows

Setting row components (`setting_toggle_row`, `setting_dropdown_row`, `setting_slider_row`,
`setting_action_row`, `setting_section_header`) are responsive by default:

- **Padding** adjusts automatically via `bind_style` — compact on Micro (breakpoint 0), standard on Medium+
- **Descriptions** are hidden on Micro/Tiny and shown on Medium+ via `bind_flag_if_lt`
- **Info icon** appears on Micro/Tiny next to rows that have a `description` prop, allowing users to tap to expand the description inline

**Never create micro/ variants for setting components.** The responsiveness is built into the base components.

If you add a new setting row type, follow the same pattern:
1. Add `pad_compact` style with tighter tokens
2. Add `bind_style name="pad_compact" subject="ui_breakpoint" ref_value="0"` on the view
3. Add `bind_flag_if_lt subject="ui_breakpoint" flag="hidden" ref_value="2"` on the description
4. Add info icon with `hidden_if_empty="$description"` and `bind_flag_if_gt subject="ui_breakpoint" flag="hidden" ref_value="1"`
```

- [ ] **Step 2: Document new XML attributes in the XML Guide**

In `docs/devel/LVGL9_XML_GUIDE.md`, find the section about attributes or flags. Add:

```markdown
### Conditional Hidden Attributes (Parse-Time)

These attributes set `LV_OBJ_FLAG_HIDDEN` based on prop values at parse time (one-shot, not reactive).
They compose safely with reactive `bind_flag_if_*` observers.

#### `hidden_if_empty`

Hides the element if the referenced prop resolves to an empty string.

```xml
<lv_obj hidden_if_empty="$description"/>
```

#### `hidden_if_prop_eq`

Hides the element if the resolved prop value equals the reference value. Format: `resolved_value|ref_value` (pipe-delimited).

```xml
<!-- Hidden if $mode resolves to "advanced" -->
<lv_obj hidden_if_prop_eq="$mode|advanced"/>

<!-- Hidden if $description is empty (equivalent to hidden_if_empty) -->
<lv_obj hidden_if_prop_eq="$description|"/>
```

#### `hidden_if_prop_not_eq`

Hides the element if the resolved prop value does NOT equal the reference value.

```xml
<!-- Hidden if $mode is anything other than "basic" -->
<lv_obj hidden_if_prop_not_eq="$mode|basic"/>
```
```

- [ ] **Step 3: Commit**

```bash
git add docs/devel/UI_CONTRIBUTOR_GUIDE.md docs/devel/LVGL9_XML_GUIDE.md
git commit -m "docs: document responsive setting rows and new XML conditional attributes (#805)"
```
