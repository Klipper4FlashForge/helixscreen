#!/usr/bin/env bats
# SPDX-License-Identifier: GPL-3.0-or-later
#
# A long-running workflow that fires on every push to a shared branch namespace
# needs two things, or the runner queue becomes the bottleneck for everyone.
#
# 1. A concurrency group. Without one, five pushes to one work branch queue five
#    whole runs and none of them cancels the others. Build alone budgets 200
#    minutes per run, so a handful of branches can hold the queue for hours while
#    every commit that actually landed waits behind superseded work.
# 2. An exclusion for claude/report-*. Those branches carry a worker's STATUS.md
#    over an otherwise untouched tree: no code changes, nothing for a compile or
#    a lint to say, and a worker pushes one every few minutes.
#
# main and release/** are deliberately keyed on the SHA rather than the ref, so
# a group never cancels a run for a commit that landed.

setup() {
    cd "$BATS_TEST_DIRNAME/../.." || return 1
    source tests/shell/helpers.bash
}

# Every workflow whose push trigger names the claude/** namespace.
claude_push_workflows() {
    python3 - <<'PY'
import glob, yaml
for path in sorted(glob.glob(".github/workflows/*.yml")):
    with open(path) as fh:
        doc = yaml.safe_load(fh)
    if not isinstance(doc, dict):
        continue
    # PyYAML resolves the bare key `on` to the boolean True.
    trig = doc.get("on", doc.get(True))
    if not isinstance(trig, dict):
        continue
    push = trig.get("push")
    if not isinstance(push, dict):
        continue
    branches = push.get("branches") or []
    if any("claude/" in str(b) for b in branches):
        print(path)
PY
}

@test "the claude/** namespace has workflows firing on it" {
    run claude_push_workflows
    [ "$status" -eq 0 ] || fail "listing claude/** workflows failed: $output"
    [ -n "$output" ] || fail "no workflow fires on claude/**; this gate would pass vacuously"
}

@test "every claude/** workflow declares a concurrency group that cancels" {
    run claude_push_workflows
    [ "$status" -eq 0 ] || fail "listing claude/** workflows failed: $output"
    local checked=0
    while read -r wf; do
        [ -n "$wf" ] || continue
        checked=$((checked + 1))
        run python3 -c "
import sys, yaml
doc = yaml.safe_load(open('$wf'))
c = doc.get('concurrency')
assert isinstance(c, dict), 'no concurrency block'
assert c.get('group'), 'concurrency block has no group'
assert 'cancel-in-progress' in c, 'concurrency block never sets cancel-in-progress'
"
        [ "$status" -eq 0 ] || fail "$wf: $output"
    done <<< "$(claude_push_workflows)"
    [ "$checked" -gt 0 ] || fail "examined no workflows"
}

@test "every claude/** workflow excludes the report branches" {
    local checked=0
    while read -r wf; do
        [ -n "$wf" ] || continue
        checked=$((checked + 1))
        run python3 -c "
import yaml
doc = yaml.safe_load(open('$wf'))
trig = doc.get('on', doc.get(True))
branches = [str(b) for b in trig['push'].get('branches') or []]
assert '!claude/report-*' in branches, 'push branches do not exclude claude/report-*: %r' % (branches,)
"
        [ "$status" -eq 0 ] || fail "$wf: $output"
    done <<< "$(claude_push_workflows)"
    [ "$checked" -gt 0 ] || fail "examined no workflows"
}

@test "main and release keep a run per commit rather than per branch" {
    local checked=0
    while read -r wf; do
        [ -n "$wf" ] || continue
        checked=$((checked + 1))
        run python3 -c "
import yaml
doc = yaml.safe_load(open('$wf'))
group = str(doc['concurrency']['group'])
assert 'github.sha' in group, 'group keys on the ref alone, so a push to main can cancel a landed commit: %s' % group
assert \"refs/heads/main\" in group, 'group does not special-case main: %s' % group
"
        [ "$status" -eq 0 ] || fail "$wf: $output"
    done <<< "$(claude_push_workflows)"
    [ "$checked" -gt 0 ] || fail "examined no workflows"
}
