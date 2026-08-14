#!/usr/bin/env bats
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Meta-tests for scripts/check_panel_widget_scrollable.py - the panel-widget
# scroll-intent gate.
#
# <lv_obj> in XML keeps LVGL's LV_OBJ_FLAG_SCROLLABLE default, which is ON.
# HelixScreen's theme overrides lv_obj's width/height/border/background/padding
# but NOT scrollable, so "our theme makes lv_obj a pure layout container" is a
# reasonable reading and a wrong one. print_card_idle and
# print_card_idle_compact had no scrollable attribute, qualified for a
# page-scroll gutter, and drew chevrons over the widget's own thumbnail on an
# 800x480 ESP32 K-Touch (fixed in 7d69130df). Home tiles sit in a drag-scrolled
# grid too, so a stray scrollable container inside one eats the grid's drag.
#
# What the gate demands is a DECLARATION, not a value: scrollable="true" on a
# real scroll region passes exactly as well as scrollable="false" on a layout
# container. Only silence fails. These tests pin both halves - the silent cases
# matter as much as the loud ones, because a gate that fires on a legitimate
# scrolling list is a gate somebody switches off.

GATE="scripts/check_panel_widget_scrollable.py"
BASELINE=21

setup() {
    cd "$BATS_TEST_DIRNAME/../.." || return 1
    load helpers
    FIXTURE_DIR="${BATS_TEST_TMPDIR:-$(mktemp -d)}/pw_scrollable"
    mkdir -p "$FIXTURE_DIR"
}

# Write $2 into $FIXTURE_DIR/$1.xml and run the gate over that one file.
# $1 carries the basename, since the gate's scope is a filename glob.
run_gate() {
    local name="$1" body="$2"
    printf '%s\n' "$body" > "$FIXTURE_DIR/$name.xml"
    run python3 "$GATE" "$FIXTURE_DIR/$name.xml"
}

# ------------------------------------------------- the shape that breaks the UI

@test "flags an <lv_obj> with no scrollable attribute" {
    run_gate panel_widget_thing '<component>
  <view name="panel_widget_thing" extends="lv_obj" scrollable="false">
    <lv_obj name="card_idle" width="100%" height="100%"/>
  </view>
</component>'
    [ "$status" -eq 1 ]
    [[ "$output" == *"❌"* ]]
}

@test "flags a <view extends=\"lv_obj\"> root with no scrollable attribute" {
    # The component root is the same hazard as any child: it IS an lv_obj.
    run_gate panel_widget_root '<component>
  <view name="panel_widget_root" extends="lv_obj" height="100%">
    <lv_obj name="inner" scrollable="false"/>
  </view>
</component>'
    [ "$status" -eq 1 ]
    run python3 "$GATE" --list "$FIXTURE_DIR/panel_widget_root.xml"
    [[ "$output" == *"[root]"* ]]
    refute_sh "printf '%s' \"\$output\" | grep -q '\[child\]'"
}

@test "reports file and line so the site is findable" {
    run_gate panel_widget_lines '<component>
  <view name="panel_widget_lines" extends="lv_obj" scrollable="false">
    <lv_obj name="undeclared"/>
  </view>
</component>'
    [ "$status" -eq 1 ]
    run python3 "$GATE" --list "$FIXTURE_DIR/panel_widget_lines.xml"
    [[ "$output" == *"panel_widget_lines.xml:3"* ]]
}

@test "counts every undeclared <lv_obj>, not just the first" {
    run_gate panel_widget_many '<component>
  <view name="panel_widget_many" extends="lv_obj" scrollable="false">
    <lv_obj name="a"/>
    <lv_obj name="b"/>
    <lv_obj name="c" scrollable="false"/>
  </view>
</component>'
    [ "$status" -eq 1 ]
    run python3 "$GATE" --summary "$FIXTURE_DIR/panel_widget_many.xml"
    [[ "$output" == *"TOTAL"*"2"* ]]
}

@test "flags an <lv_obj> whose attributes wrap across lines" {
    # Every real panel widget wraps; a line-oriented check would miss these.
    run_gate panel_widget_wrapped '<component>
  <view name="panel_widget_wrapped" extends="lv_obj" scrollable="false">
    <lv_obj name="wrapped"
            width="100%"
            height="100%"
            style_pad_all="0">
      <text_body text="hi"/>
    </lv_obj>
  </view>
</component>'
    [ "$status" -eq 1 ]
}

@test "explains that the default is ON, not just that something is missing" {
    run_gate panel_widget_why '<component>
  <view name="panel_widget_why" extends="lv_obj" scrollable="false">
    <lv_obj name="x"/>
  </view>
</component>'
    [ "$status" -eq 1 ]
    [[ "$output" == *"scrollable"* ]]
}

# ---------------------------------------------- idioms that must stay silent

@test "silent on scrollable=\"false\"" {
    run_gate panel_widget_false '<component>
  <view name="panel_widget_false" extends="lv_obj" scrollable="false">
    <lv_obj name="card" width="100%" scrollable="false"/>
  </view>
</component>'
    [ "$status" -eq 0 ]
}

@test "silent on scrollable=\"true\" - a real scroll region is fine" {
    # The gate wants a decision, not a particular answer. A genuinely scrolling
    # list (job queue, gcode console) declares true and must never be nagged.
    run_gate panel_widget_true '<component>
  <view name="panel_widget_true" extends="lv_obj" scrollable="true">
    <lv_obj name="job_list" width="100%" scrollable="true" scroll_dir="ver"/>
  </view>
</component>'
    [ "$status" -eq 0 ]
}

@test "silent on a <view extends=\"ui_card\"> root with no scrollable" {
    # ui_card's create handler clears LV_OBJ_FLAG_SCROLLABLE before XML
    # attributes are applied (src/ui/ui_card.cpp:53), so those roots are already
    # safe. Flagging them would be pure noise on the most common widget root.
    run_gate panel_widget_card '<component>
  <view name="panel_widget_card" extends="ui_card" height="100%">
    <text_body text="hi"/>
  </view>
</component>'
    [ "$status" -eq 0 ]
}

@test "ui_card really does clear the flag - the exemption is not folklore" {
    # If this create handler ever stops clearing the flag, the exemption above
    # silently becomes a hole. Pin it to the source.
    run grep -q "lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE)" src/ui/ui_card.cpp
    [ "$status" -eq 0 ]
}

@test "silent on an <lv_obj> in a NON-panel_widget file" {
    # Scope is home-widget tiles. A plain component is out of scope, not an
    # exemption - the drag-scrolled-grid hazard does not apply there.
    run_gate some_other_component '<component>
  <view name="some_other_component" extends="lv_obj">
    <lv_obj name="undeclared" width="100%"/>
  </view>
</component>'
    [ "$status" -eq 0 ]
}

@test "silent on widgets that are not lv_obj" {
    run_gate panel_widget_others '<component>
  <view name="panel_widget_others" extends="lv_obj" scrollable="false">
    <ui_card name="card">
      <lv_button name="btn"/>
      <text_body text="hi"/>
      <lv_image src="x"/>
    </ui_card>
  </view>
</component>'
    [ "$status" -eq 0 ]
}

@test "an <lv_obj> inside a comment is not an <lv_obj>" {
    run_gate panel_widget_commented '<component>
  <view name="panel_widget_commented" extends="lv_obj" scrollable="false">
    <!-- <lv_obj name="old_card" width="100%"/> -->
    <lv_obj name="card" scrollable="false"/>
  </view>
</component>'
    [ "$status" -eq 0 ]
}

@test "a quoted > inside an attribute does not end the tag early" {
    # Parsing that stops at the first '>' would truncate the attribute run and
    # miss the scrollable= that follows it, firing on correct XML.
    run_gate panel_widget_quoted '<component>
  <view name="panel_widget_quoted" extends="lv_obj" scrollable="false">
    <lv_obj name="cmp" bind_text="a gt b" text="x &gt; y" scrollable="false"/>
  </view>
</component>'
    [ "$status" -eq 0 ]
}

@test "a <view> with no extends= is not treated as an lv_obj" {
    run_gate panel_widget_noextends '<component>
  <view name="panel_widget_noextends">
    <lv_obj name="card" scrollable="false"/>
  </view>
</component>'
    [ "$status" -eq 0 ]
}

# ---------------------------------------------------- the ratcheting baseline

@test "the real tree passes at the baseline" {
    run python3 "$GATE" --max-allowed "$BASELINE"
    [ "$status" -eq 0 ]
    [[ "$output" == *"✅"* ]]
}

@test "the real tree FAILS one below the baseline - the ratchet bites" {
    # If this ever passes, the baseline has drifted below the real count and the
    # gate has stopped constraining anything.
    run python3 "$GATE" --max-allowed "$((BASELINE - 1))"
    [ "$status" -eq 1 ]
    [[ "$output" == *"❌"* ]]
}

@test "the real tree is exactly at the baseline, not under it" {
    # Under the baseline is a pass, but a silent one - this names the drift so
    # the number gets ratcheted down instead of rotting.
    run python3 "$GATE" --summary --max-allowed "$BASELINE"
    [ "$status" -eq 0 ]
    [[ "$output" == *"== baseline"* ]]
}

@test "quality-checks.sh wires the gate in at the same baseline" {
    run grep -E "check_panel_widget_scrollable.py --max-allowed $BASELINE" \
        scripts/quality-checks.sh
    [ "$status" -eq 0 ]
}
