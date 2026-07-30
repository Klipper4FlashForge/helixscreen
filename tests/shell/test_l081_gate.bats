#!/usr/bin/env bats
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Meta-tests for scripts/check_l081_anti_pattern.py — the L081 bg-thread gate.
#
# The gate flags a bare `tok.expired()` check reached on a background thread:
# either followed by a `this`/member access (a real TOCTOU use-after-free, #707)
# or immediately followed by `tok.defer(...)` (dead code, because defer already
# re-checks atomically on the main thread).
#
# What these pin is that it reads CODE, not prose. The script matched inside
# comments and string literals, so a comment describing the anti-pattern — or
# quoting the fix advice the script itself prints — was reported as a violation.
# That is worse than a missed hit: it makes the accurate comment the thing you
# have to delete, so the next person writes a vaguer one. The sibling gate
# check_raw_this_queue_update.py already tested this; this one did not.

GATE="scripts/check_l081_anti_pattern.py"

setup() {
    cd "$BATS_TEST_DIRNAME/../.." || return 1
    load helpers.bash
    FIXTURE_DIR="${BATS_TEST_TMPDIR:-$(mktemp -d)}/l081_gate"
    mkdir -p "$FIXTURE_DIR"
}

# Write $1 into a fixture .cpp and run the gate over that file alone.
run_gate() {
    printf '%s\n' "$1" > "$FIXTURE_DIR/case.cpp"
    run python3 "$GATE" "$FIXTURE_DIR/case.cpp"
}

# --- the shapes that must still fire ----------------------------------------

@test "flags a bg expired() check followed by defer (redundant guard)" {
    run_gate 'void A::f() {
    auto token = lifetime_.token();
    api_->go([this, token]() {
        if (token.expired())
            return;
        token.defer([this]() { apply(); });
    });
}'
    [ "$status" -eq 1 ]
    [[ "$output" == *'redundant'* ]]
}

@test "flags a bg expired() check followed by a member access (UAF)" {
    run_gate 'void A::f() {
    auto token = lifetime_.token();
    api_->go([this, token]() {
        if (token.expired())
            return;
        this->apply();
    });
}'
    [ "$status" -eq 1 ]
}

# --- prose must not fire ----------------------------------------------------

@test "a violation quoted in a line comment is not a violation" {
    run_gate 'void A::f() {
    // The bare `if (token.expired()) return;` this replaced was dead code —
    // defer() already re-checks atomically on the main thread.
    api_->go(lifetime_.bg_cb("A::f", [this]() { apply(); }));
}'
    [ "$status" -eq 0 ]
}

@test "a violation quoted in a block comment is not a violation" {
    run_gate 'void A::f() {
    /* Historical note: this used to read
     *     if (token.expired())
     *         return;
     *     token.defer([this]() { apply(); });
     * before the migration to bg_cb.
     */
    api_->go(lifetime_.bg_cb("A::f", [this]() { apply(); }));
}'
    [ "$status" -eq 0 ]
}

@test "a violation inside a string literal is not a violation" {
    run_gate 'void A::f() {
    spdlog::warn("if (token.expired()) return; token.defer([this]() { apply(); });");
}'
    [ "$status" -eq 0 ]
}

# --- the opt-out still works, and it lives in a comment ----------------------

@test "L081_OK on the line suppresses a real hit" {
    run_gate 'void A::f() {
    auto token = lifetime_.token();
    api_->go([this, token]() {
        if (token.expired()) // L081_OK: synchronous wait wrapper
            return;
        token.defer([this]() { apply(); });
    });
}'
    [ "$status" -eq 0 ]
}

# --- the tree itself --------------------------------------------------------

@test "the default scan of the real tree is clean" {
    run python3 "$GATE"
    [ "$status" -eq 0 ]
}
