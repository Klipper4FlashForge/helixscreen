#!/usr/bin/env bats
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Tests for helix-launcher.sh nice-level / co-host detection:
# - The launcher reduces priority (renice +10) only when Klipper or Moonraker
#   is running on the same host. On a standalone display (remote-display
#   SonicPad, dev workstation, kiosk pointed at a network printer) it leaves
#   priority at the default.
# - HELIX_NICE overrides the default; HELIX_NICE=0 disables.

WORKTREE_ROOT="$(cd "$BATS_TEST_DIRNAME/../.." && pwd)"
LAUNCHER="$WORKTREE_ROOT/scripts/helix-launcher.sh"

setup() {
    load helpers

    # Extract the helix_klipper_co_hosted function from the launcher into a
    # standalone snippet we can source. Range: from the function header to
    # the next line starting with '}'.
    awk '/^helix_klipper_co_hosted\(\)/,/^}/' "$LAUNCHER" \
        > "$BATS_TEST_TMPDIR/co_hosted.sh"

    # Sanity: the snippet must contain the function body, not be empty.
    [ -s "$BATS_TEST_TMPDIR/co_hosted.sh" ]
    grep -q '^}' "$BATS_TEST_TMPDIR/co_hosted.sh"

    export STUB_DIR="$BATS_TEST_TMPDIR/stubs"
    mkdir -p "$STUB_DIR"
}

# Build a pgrep stub that returns 0 for the configured target and 1 otherwise.
# The launcher passes patterns like '[k]lippy\.py' / '[m]oonraker\.py', so
# we match on the unique infix ('lippy' / 'oonraker') rather than the full
# bracketed pattern.
#
# Usage: make_pgrep_stub klippy | moonraker | none
make_pgrep_stub() {
    local mode="$1"
    cat > "$STUB_DIR/pgrep" <<EOF
#!/bin/sh
[ "\$1" = "-f" ] || exit 2
case "\$2" in
    *lippy*)    [ "$mode" = "klippy" ]    && exit 0 ;;
    *oonraker*) [ "$mode" = "moonraker" ] && exit 0 ;;
esac
exit 1
EOF
    chmod +x "$STUB_DIR/pgrep"
}

# Real Klipper/Moonraker on the test host would defeat the "no co-host"
# assertions. Skip those cases when sockets are present.
skip_if_real_klipper_sockets() {
    if [ -S /tmp/klippy_uds ] || [ -S /tmp/moonraker.sock ]; then
        skip "real Klipper/Moonraker sockets present on this host"
    fi
}

# =============================================================================
# Static structure: the launcher contains the expected logic
# =============================================================================

@test "launcher defines helix_klipper_co_hosted function" {
    grep -q '^helix_klipper_co_hosted()' "$LAUNCHER"
}

@test "co-host detection probes for klippy.py via pgrep -f" {
    # Matches both 'klippy.py' and the bracket-trick form '[k]lippy.py'
    # used to keep pgrep from matching itself.
    grep -qE "pgrep -f.*lippy" "$LAUNCHER"
}

@test "co-host detection probes for moonraker.py via pgrep -f" {
    grep -qE "pgrep -f.*oonraker" "$LAUNCHER"
}

@test "co-host detection has socket fallback for klippy_uds" {
    grep -q '/tmp/klippy_uds' "$LAUNCHER"
}

@test "co-host detection has socket fallback for moonraker.sock" {
    grep -q '/tmp/moonraker.sock' "$LAUNCHER"
}

@test "renice is gated on helix_klipper_co_hosted" {
    # The renice call must live inside an `if helix_klipper_co_hosted; then`
    # block — never unconditional.
    awk '
        /^if helix_klipper_co_hosted/ { inside = 1 }
        inside && /renice /            { found = 1 }
        inside && /^fi$/               { inside = 0 }
        END { exit found ? 0 : 1 }
    ' "$LAUNCHER"
}

@test "default nice level is +10" {
    grep -qE 'HELIX_NICE:-10' "$LAUNCHER"
}

@test "HELIX_NICE override is honored" {
    grep -q 'HELIX_NICE' "$LAUNCHER"
}

@test "HELIX_NICE=0 disables niceness" {
    # Whether expressed as `_helix_nice` or `HELIX_NICE`, there must be a
    # guard that skips renice when the requested value is "0".
    grep -qE '"\$\{?_helix_nice\}?"[[:space:]]*!=[[:space:]]*"0"' "$LAUNCHER" \
        || grep -qE '"\$\{?HELIX_NICE\}?"[[:space:]]*!=[[:space:]]*"0"' "$LAUNCHER"
}

@test "renice happens AFTER platform_pre_start hook (post-wait positioning)" {
    # The renice block must live below the platform_pre_start invocation so
    # services have come up before we probe for them.
    pre_start_line=$(grep -n 'platform_pre_start' "$LAUNCHER" | tail -1 | cut -d: -f1)
    renice_line=$(grep -n '^if helix_klipper_co_hosted' "$LAUNCHER" | tail -1 | cut -d: -f1)
    [ -n "$pre_start_line" ]
    [ -n "$renice_line" ]
    [ "$renice_line" -gt "$pre_start_line" ]
}

# =============================================================================
# Behavioral: the extracted function with stubbed pgrep
# =============================================================================

@test "co-host detection: returns 0 when pgrep finds klippy.py" {
    make_pgrep_stub klippy
    PATH="$STUB_DIR:$PATH" sh -c \
        ". '$BATS_TEST_TMPDIR/co_hosted.sh' && helix_klipper_co_hosted"
}

@test "co-host detection: returns 0 when pgrep finds moonraker.py" {
    make_pgrep_stub moonraker
    PATH="$STUB_DIR:$PATH" sh -c \
        ". '$BATS_TEST_TMPDIR/co_hosted.sh' && helix_klipper_co_hosted"
}

@test "co-host detection: returns non-zero when nothing is running and no sockets" {
    skip_if_real_klipper_sockets
    make_pgrep_stub none
    run env "PATH=$STUB_DIR:$PATH" sh -c \
        ". '$BATS_TEST_TMPDIR/co_hosted.sh' && helix_klipper_co_hosted"
    [ "$status" -ne 0 ]
}

@test "co-host detection: returns non-zero when pgrep is absent and no sockets" {
    skip_if_real_klipper_sockets
    # PATH points at an empty dir so `command -v pgrep` reports false and
    # the function falls through to the socket check. Use an absolute path
    # for sh so env can exec it after rewriting PATH.
    EMPTY="$BATS_TEST_TMPDIR/empty-path"
    mkdir -p "$EMPTY"
    SH=$(command -v sh)
    [ -x "$SH" ]
    run env "PATH=$EMPTY" "$SH" -c \
        ". '$BATS_TEST_TMPDIR/co_hosted.sh' && helix_klipper_co_hosted; echo \$?"
    [ "$status" -eq 0 ]            # `echo` always succeeds
    [ "$(last_line)" = "1" ]        # function itself returned 1
}

@test "co-host detection: socket fallback only fires if sockets exist" {
    # Concrete check on the snippet itself — no `command -v pgrep` branch in
    # the body when pgrep is missing should mean we fall through cleanly.
    grep -qE '\[ -S /tmp/klippy_uds \]' "$BATS_TEST_TMPDIR/co_hosted.sh"
    grep -qE '\[ -S /tmp/moonraker.sock \]' "$BATS_TEST_TMPDIR/co_hosted.sh"
}
