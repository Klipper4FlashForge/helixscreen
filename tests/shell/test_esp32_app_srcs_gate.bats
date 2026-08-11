#!/usr/bin/env bats
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Meta-tests for scripts/check_esp32_app_srcs.py — the ESP32 firmware app_srcs
# manifest drift gate.
#
# The firmware compiles a hand-maintained SUBSET of src/ (the "v1 Core+AMS cut":
# camera, label printer, gcode/bed-mesh 3D, plugins, timelapse viewer,
# screensaver, calibration, sound, mocks, and the concrete libhv client are all
# gated off). The list was generated once from the native-audit 491-file
# Xtensa-compile manifest and has drifted twice since — ams_endless_spool.cpp
# and toolhead_homing.cpp both landed in main without making app_srcs.txt, and
# the firmware link broke ~25 min into esp32-build CI.
#
# Curation is unavoidable for a subset build; the gate's job is to make the
# drift loud at quality-check / PR time. Every src/**/*.{cpp,c} must be in
# app_srcs.txt (compile it) OR app_srcs_excluded.txt (don't, with a reason).
#
# These tests pin both halves. The catch half is the whole point — a new file
# that nobody decided on must fail. The quiet half matters equally: a gate that
# fired on files the manifest legitimately excludes would be noise on every
# firmware commit and would get switched off, defeating the purpose.

GATE="scripts/check_esp32_app_srcs.py"

setup() {
    cd "$BATS_TEST_DIRNAME/../.." || return 1
    # Fixture: a tiny src/ tree + manifest + exclusions, isolated from the repo.
    ROOT="${BATS_TEST_TMPDIR:-$(mktemp -d)}/fw"
    mkdir -p "$ROOT/src/printer" "$ROOT/src/camera"
    touch "$ROOT/src/printer/compiled.cpp" \
          "$ROOT/src/printer/secretly_new.cpp" \
          "$ROOT/src/printer/excluded_one.cpp" \
          "$ROOT/src/camera/cam.cpp"
    # manifest: compiles compiled.cpp only
    printf 'src/printer/compiled.cpp\n' > "$ROOT/manifest.txt"
    # exclusions: one file + one whole dir
    printf 'src/printer/excluded_one.cpp  # reason\nsrc/camera/  # whole dir\n' > "$ROOT/excluded.txt"
}

run_gate() {
    run python3 "$GATE" \
        --manifest "$ROOT/manifest.txt" \
        --exclusions "$ROOT/excluded.txt" \
        --src-root "$ROOT/src"
}

# ----------------------------------------------------------- the catch half

@test "flags a src/ file in neither manifest nor exclusions (the drift case)" {
    run_gate
    [ "$status" -eq 1 ]
    [[ "$output" == *"secretly_new.cpp"* ]]
    [[ "$output" == *"app_srcs.txt"* ]]
    [[ "$output" == *"app_srcs_excluded.txt"* ]]
}

@test "flags a stale manifest line (src/ file that no longer exists)" {
    printf 'src/printer/compiled.cpp\nsrc/printer/deleted.cpp\n' > "$ROOT/manifest.txt"
    printf 'src/printer/excluded_one.cpp  # reason\nsrc/camera/  # whole dir\n' > "$ROOT/excluded.txt"
    run_gate
    [ "$status" -eq 1 ]
    [[ "$output" == *"stale"*"deleted.cpp"* ]]
}

@test "--list prints the undecided files" {
    run python3 "$GATE" --list \
        --manifest "$ROOT/manifest.txt" \
        --exclusions "$ROOT/excluded.txt" \
        --src-root "$ROOT/src"
    [ "$status" -eq 1 ]
    [[ "$output" == *"src/printer/secretly_new.cpp"* ]]
}

# ----------------------------------------------------------- the quiet half

@test "passes when every src/ file is in the manifest or exclusions" {
    # decide the new file: add it to the manifest
    printf 'src/printer/compiled.cpp\nsrc/printer/secretly_new.cpp\n' > "$ROOT/manifest.txt"
    run_gate
    [ "$status" -eq 0 ]
    [[ "$output" == *"OK"* ]]
}

@test "a dir-level exclusion (trailing /) covers every file beneath it" {
    run_gate
    # camera/cam.cpp is covered by the src/camera/ dir exclusion — only
    # secretly_new is undecided, not cam.cpp.
    [[ "$output" != *"cam.cpp"* ]]
}

@test "ignores manifest entries outside src/ (e.g. lib/ sources)" {
    printf 'src/printer/compiled.cpp\nsrc/printer/secretly_new.cpp\nlib/lv_markdown/src/x.c\n' > "$ROOT/manifest.txt"
    run_gate
    [ "$status" -eq 0 ]
    [[ "$output" != *"lv_markdown"* ]]
}

# ----------------------------------------------------------- the seed tooling

@test "--write-exclusions seeds a baseline covering all undecided files" {
    rm -f "$ROOT/excluded.txt"
    run python3 "$GATE" --write-exclusions \
        --manifest "$ROOT/manifest.txt" \
        --exclusions "$ROOT/excluded.txt" \
        --src-root "$ROOT/src"
    [ "$status" -eq 0 ]
    # now the gate passes with the freshly-seeded baseline
    run_gate
    [ "$status" -eq 0 ]
}

@test "--write-exclusions refuses to overwrite a baseline without --force" {
    printf '# hand-written baseline\nsrc/printer/excluded_one.cpp\n' > "$ROOT/excluded.txt"
    run python3 "$GATE" --write-exclusions \
        --manifest "$ROOT/manifest.txt" \
        --exclusions "$ROOT/excluded.txt" \
        --src-root "$ROOT/src"
    [ "$status" -eq 1 ]
    [[ "$output" == *"--force"* ]]
    # the hand-written baseline is untouched
    [[ "$(cat "$ROOT/excluded.txt")" == *"hand-written baseline"* ]]
}
