#!/usr/bin/env bats
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Start-path latency and the dying-daemon race in config/helixscreen.init and
# assets/config/platform/hooks-cc1.sh.
#
# Why this matters: on the Elegoo Centauri Carbon the stock COSMOS resonance
# macro restarts the UI through a Klipper shell command that carries a 5 second
# timeout (GUI_START -> gui-switcher start -> the init script). A shell command
# killed on timeout takes its whole process group with it, including the
# supervisor the init script just forked. The start path therefore has to stay
# well inside that budget, and it must not mistake a daemon that is still
# exiting for one that is healthy.

WORKTREE_ROOT="$(cd "$BATS_TEST_DIRNAME/../.." && pwd)"

setup() {
    load helpers
    MOCK_ROOT="$BATS_TEST_TMPDIR/root"
    mkdir -p "$MOCK_ROOT/bin"
}

# Extract a single function body out of a real script so the tests exercise the
# shipped source rather than a copy that can drift away from it.
_extract_fn() {
    local fn="$1" src="$2"
    awk -v fn="$fn" '
        $0 ~ "^" fn "\\(\\) \\{" { inside = 1 }
        inside { print }
        inside && /^}/ { exit }
    ' "$src"
}

# ---------------------------------------------------------------------------
# hooks-cc1.sh: platform_stop_competing_uis
# ---------------------------------------------------------------------------

@test "cc1 hook: no settle sleep when no competing UI is running" {
    local hook="$WORKTREE_ROOT/assets/config/platform/hooks-cc1.sh"
    _extract_fn platform_stop_competing_uis "$hook" > "$BATS_TEST_TMPDIR/fn.sh"

    # pidof never matches, ps lists nothing: nothing should be signalled.
    cat > "$BATS_TEST_TMPDIR/run.sh" <<'EOF'
pidof() { return 1; }
killall() { echo "KILLED $*" >> "$TRACE"; }
ps() { echo ""; }
sleep() { echo "SLEPT $*" >> "$TRACE"; }
. "$FN"
platform_stop_competing_uis
EOF
    TRACE="$BATS_TEST_TMPDIR/trace" FN="$BATS_TEST_TMPDIR/fn.sh" \
        run sh "$BATS_TEST_TMPDIR/run.sh"
    [ "$status" -eq 0 ]

    refute_grep 'SLEPT' "$BATS_TEST_TMPDIR/trace" || true
    if [ -f "$BATS_TEST_TMPDIR/trace" ]; then
        run grep -c 'SLEPT' "$BATS_TEST_TMPDIR/trace"
        [ "$output" = "0" ]
    fi
}

@test "cc1 hook: still settles when a competing UI WAS killed" {
    local hook="$WORKTREE_ROOT/assets/config/platform/hooks-cc1.sh"
    _extract_fn platform_stop_competing_uis "$hook" > "$BATS_TEST_TMPDIR/fn.sh"

    # pidof matches guppyscreen only, so exactly one UI is signalled and the
    # settle delay must still be paid.
    cat > "$BATS_TEST_TMPDIR/run.sh" <<'EOF'
pidof() { [ "$1" = "guppyscreen" ] && { echo 4242; return 0; }; return 1; }
killall() { echo "KILLED $*" >> "$TRACE"; }
ps() { echo ""; }
sleep() { echo "SLEPT $*" >> "$TRACE"; }
. "$FN"
platform_stop_competing_uis
EOF
    TRACE="$BATS_TEST_TMPDIR/trace" FN="$BATS_TEST_TMPDIR/fn.sh" \
        run sh "$BATS_TEST_TMPDIR/run.sh"
    [ "$status" -eq 0 ]

    run grep -c 'KILLED guppyscreen' "$BATS_TEST_TMPDIR/trace"
    [ "$output" = "1" ]
    run grep -c 'SLEPT 1' "$BATS_TEST_TMPDIR/trace"
    [ "$output" = "1" ]
}

# ---------------------------------------------------------------------------
# helixscreen.init: start-path pacing
# ---------------------------------------------------------------------------

@test "init: start confirms a live supervisor without any fixed sleep" {
    # Model the post-fork confirmation block: PIDFILE holds a live PID, so the
    # answer is available on the first check and no sleep should be needed.
    cat > "$BATS_TEST_TMPDIR/run.sh" <<'EOF'
NAME=helixscreen
PIDFILE="$TMP/pidfile"
echo $$ > "$PIDFILE"
sleep() { echo "SLEPT $*" >> "$TRACE"; }
_start_waited=0
while :; do
    if [ -f "$PIDFILE" ]; then
        PID=$(cat "$PIDFILE")
        if kill -0 "$PID" 2>/dev/null; then
            echo "$NAME started (PID $PID)"
            exit 0
        fi
    fi
    [ "$_start_waited" -ge 2 ] && break
    sleep 1
    _start_waited=$((_start_waited + 1))
done
echo "Failed to start $NAME"
exit 1
EOF
    TMP="$BATS_TEST_TMPDIR" TRACE="$BATS_TEST_TMPDIR/trace" \
        run sh "$BATS_TEST_TMPDIR/run.sh"
    [ "$status" -eq 0 ]
    [[ "$output" == *"started (PID"* ]]
    [ ! -f "$BATS_TEST_TMPDIR/trace" ]
}

@test "init: the shipped start path polls rather than sleeping a flat 2s" {
    # Guard against the flat `sleep 2` coming back: the confirmation block must
    # be a loop, and must not unconditionally sleep before its first check.
    run grep -n "Wait briefly and check if started" "$WORKTREE_ROOT/config/helixscreen.init"
    [ "$status" -ne 0 ]

    run grep -c "_start_waited" "$WORKTREE_ROOT/config/helixscreen.init"
    [ "$output" -ge 3 ]
}

@test "init: start waits for a dying daemon before declaring already-running" {
    # A stop cut off mid-kill leaves the daemon briefly alive. The start path
    # must let it finish rather than no-opping and leaving a blank screen.
    cat > "$BATS_TEST_TMPDIR/run.sh" <<'EOF'
DAEMON_NAME=helix-screen
# Alive for the first two probes, gone afterwards.
pidof() {
    n=$(cat "$TMP/probes" 2>/dev/null || echo 0)
    n=$((n + 1)); echo "$n" > "$TMP/probes"
    [ "$n" -le 2 ] && { echo 999; return 0; }
    return 1
}
sleep() { echo "SLEPT $*" >> "$TRACE"; }
if pidof "$DAEMON_NAME" >/dev/null 2>&1; then
    _exit_waited=0
    while pidof "$DAEMON_NAME" >/dev/null 2>&1; do
        [ "$_exit_waited" -ge 3 ] && break
        sleep 1
        _exit_waited=$((_exit_waited + 1))
    done
fi
if pidof "$DAEMON_NAME" >/dev/null 2>&1; then
    echo "WRONGLY-ALREADY-RUNNING"
    exit 1
fi
echo "PROCEEDED-TO-START"
EOF
    TMP="$BATS_TEST_TMPDIR" TRACE="$BATS_TEST_TMPDIR/trace" \
        run sh "$BATS_TEST_TMPDIR/run.sh"
    [ "$status" -eq 0 ]
    [ "$output" = "PROCEEDED-TO-START" ]
}

@test "init: a genuinely running daemon is still reported as already-running" {
    # The dying-daemon wait must be bounded, and a daemon that never exits must
    # still short-circuit the start instead of launching a second instance.
    cat > "$BATS_TEST_TMPDIR/run.sh" <<'EOF'
DAEMON_NAME=helix-screen
pidof() { echo 999; return 0; }   # never exits
sleep() { echo "SLEPT $*" >> "$TRACE"; }
if pidof "$DAEMON_NAME" >/dev/null 2>&1; then
    _exit_waited=0
    while pidof "$DAEMON_NAME" >/dev/null 2>&1; do
        [ "$_exit_waited" -ge 3 ] && break
        sleep 1
        _exit_waited=$((_exit_waited + 1))
    done
fi
if pidof "$DAEMON_NAME" >/dev/null 2>&1; then
    echo "already running"
    exit 0
fi
echo "STARTED-A-SECOND-INSTANCE"
exit 1
EOF
    TMP="$BATS_TEST_TMPDIR" TRACE="$BATS_TEST_TMPDIR/trace" \
        run sh "$BATS_TEST_TMPDIR/run.sh"
    [ "$status" -eq 0 ]
    [ "$output" = "already running" ]
    # Bounded: exactly 3 waits, never an unbounded spin.
    run grep -c 'SLEPT 1' "$BATS_TEST_TMPDIR/trace"
    [ "$output" = "3" ]
}

@test "init: the shipped script contains the dying-daemon wait" {
    run grep -c "_exit_waited" "$WORKTREE_ROOT/config/helixscreen.init"
    [ "$output" -ge 3 ]
}

@test "init and cc1 hook are POSIX sh clean" {
    run sh -n "$WORKTREE_ROOT/config/helixscreen.init"
    [ "$status" -eq 0 ]
    run sh -n "$WORKTREE_ROOT/assets/config/platform/hooks-cc1.sh"
    [ "$status" -eq 0 ]
}
