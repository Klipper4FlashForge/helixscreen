#!/usr/bin/env bats
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Meta-tests for scripts/check_orphan_subjects.py — the gate catching LVGL
# subjects that C++ registers and keeps current but nothing ever reads.
#
# The gate runs at --max-allowed 0, so its whole value is that a dead subject
# cannot land. That makes a FALSE NEGATIVE the failure that matters: an orphan
# the gate clears is invisible, and the gate still reports green. Most of these
# tests therefore assert that a dead subject IS reported, in the layouts where
# clearing it would be easiest to do by accident.
#
# The opposing pressure is real. Registrations and observer calls live a few
# lines apart by construction, and a subject fetched by name is read through a
# pointer on the NEXT statement, so the name never appears at the read. A gate
# that ignored that shape would report live subjects and get switched off. Both
# directions are pinned here.

load helpers

GATE="scripts/check_orphan_subjects.py"

setup() {
    cd "$BATS_TEST_DIRNAME/../.." || return 1
    ROOT="${BATS_TEST_TMPDIR:-$(mktemp -d)}/tree"
    mkdir -p "$ROOT/src" "$ROOT/include" "$ROOT/ui_xml"
}

run_gate() {
    run python3 "$GATE" --repo-root "$ROOT" --list
}

# ------------------------------------------------------ false negatives (the point)

@test "a subject nothing reads is reported" {
    cat > "$ROOT/src/demo.cpp" <<'EOF'
void init_subjects() {
    lv_xml_register_subject(nullptr, "totally_dead_subject", &dead_member_);
}
EOF
    run_gate
    contains "totally_dead_subject" "$output"
    contains "1 of 1 registered" "$output"
}

@test "a dead subject registered just above an unrelated observer is still reported" {
    # The fail-open shape: init_subjects() registers everything together and the
    # observer calls follow a few lines down, so a window that reaches backwards
    # from every read site clears every registration sitting above one.
    cat > "$ROOT/src/demo.cpp" <<'EOF'
void init_subjects() {
    lv_xml_register_subject(nullptr, "totally_dead_subject", &dead_member_);
    lv_xml_register_subject(nullptr, "live_subject", &live_member_);

    observe_int_sync<Panel>(&live_member_, cb);
}
EOF
    run_gate
    contains "totally_dead_subject" "$output"
    contains "1 of 2 registered" "$output"
}

@test "a dead subject registered just below an unrelated observer is still reported" {
    cat > "$ROOT/src/demo.cpp" <<'EOF'
void init_subjects() {
    lv_xml_register_subject(nullptr, "live_subject", &live_member_);
    observe_int_sync<Panel>(&live_member_, cb);

    lv_xml_register_subject(nullptr, "totally_dead_subject", &dead_member_);
}
EOF
    run_gate
    contains "totally_dead_subject" "$output"
    contains "1 of 2 registered" "$output"
}

# ------------------------------------------------------ false positives (must stay fixed)

@test "a subject fetched by name and read through the pointer is not an orphan" {
    # The literal sits on the lv_xml_get_subject() line and the read is the next
    # statement, so the name never appears at the lv_subject_get_int() call.
    cat > "$ROOT/src/demo.cpp" <<'EOF'
void init_subjects() {
    lv_xml_register_subject(nullptr, "chamber_filter_fan_on", &filter_on_);
}

void ChamberPanel::apply() {
    lv_subject_t* on = lv_xml_get_subject(nullptr, "chamber_filter_fan_on");
    tc->set_chamber_filter_fan(!on || lv_subject_get_int(on) != 1);
}
EOF
    run_gate
    lacks "chamber_filter_fan_on" "$output"
    contains "0 of 1 registered" "$output"
}

@test "a subject observed by member pointer is not an orphan" {
    # Observers take the MEMBER, which routinely does not match the subject string.
    cat > "$ROOT/src/demo.cpp" <<'EOF'
void init_subjects() {
    lv_xml_register_subject(nullptr, "volume_value", &volume_value_subject_);
}

void SoundPanel::attach() {
    observe_int_sync<SoundPanel>(&volume_value_subject_, on_volume);
}
EOF
    run_gate
    contains "0 of 1 registered" "$output"
}

@test "a subject bound only from XML is not an orphan" {
    cat > "$ROOT/src/demo.cpp" <<'EOF'
void init_subjects() {
    lv_xml_register_subject(nullptr, "nozzle_temp_text", &nozzle_text_);
}
EOF
    printf '<view><lv_label bind_text="nozzle_temp_text"/></view>\n' \
        > "$ROOT/ui_xml/panel.xml"
    run_gate
    contains "0 of 1 registered" "$output"
}

@test "a SUBJECT_OK opt-out on the call is honoured" {
    # The annotation rides the call or its continuation lines, not the line
    # above it: clang-format wraps these calls, and honouring the preceding
    # line would let one opt-out silently cover the next registration too.
    cat > "$ROOT/src/demo.cpp" <<'EOF'
void init_subjects() {
    lv_xml_register_subject(nullptr, "plugin_state",
                            &plugin_state_); // SUBJECT_OK: read by the plugin ABI
}
EOF
    run_gate
    contains "0 of 0 registered" "$output"
}

@test "one observer does not clear the whole block of registrations around it" {
    # A registration spells its own name and member, so scanning registrations
    # as read text makes each one vouch for itself and for its neighbours. The
    # window is 400 chars, which is most of an init_subjects() body: a single
    # observer anywhere in it would clear every registration in the block.
    cat > "$ROOT/src/demo.cpp" <<'EOF'
void init_subjects() {
    lv_xml_register_subject(nullptr, "first_dead_subject", &first_member_);
    lv_xml_register_subject(nullptr, "second_dead_subject", &second_member_);
    lv_xml_register_subject(nullptr, "live_subject", &live_member_);

    observe_int_sync<Panel>(&live_member_, cb);
}
EOF
    run_gate
    contains "first_dead_subject" "$output"
    contains "second_dead_subject" "$output"
    contains "2 of 3 registered" "$output"
}
