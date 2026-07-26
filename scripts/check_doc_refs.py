#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Lint gate: agent-facing docs must not cite files that don't exist.
#
# CLAUDE.md files and skills work by progressive disclosure — they are mostly
# pointers, and a pointer to a renamed or deleted file is worse than no pointer.
# It sends the reader (human or agent) looking for something that isn't there, and
# it silently teaches a wrong name. Several rounds of this have been cleaned up by
# hand:
#
#   - .claude/checklist.md cited six docs that had moved to docs/devel/
#   - CLAUDE.md taught ui_nav_push_overlay(), a function with zero call sites
#   - scripts/CLAUDE.md documented package.sh, deleted in 1ddbbbdba
#   - a lesson taught lv_xml_component_register_from_file(), a transposed name
#     that exists nowhere
#
# Two checks:
#   refs   — every backticked path in an agent-facing doc resolves
#   index  — every doc in docs/devel/ is listed in docs/devel/CLAUDE.md
#
# The index check is what makes lazy loading trustworthy: a doc missing from the
# routing table is a doc nobody will find.
#
# Usage:
#   check_doc_refs.py            # both checks, fail on any problem
#   check_doc_refs.py --refs     # broken references only
#   check_doc_refs.py --index    # index completeness only
#   check_doc_refs.py --list     # show what was scanned

import argparse
import os
import re
import sys

SKIP_DIRS = {'.git', '.worktrees', 'build', 'node_modules', '.venv', 'venv'}

# Paths that are intentionally absent from a clean checkout.
EXEMPT_SUBSTRINGS = (
    'superpowers/',        # docs/superpowers/ specs are gitignored, local-only
)

# Tokens that are obviously placeholders rather than real paths.
PLACEHOLDER_CHARS = ('<', '>', '*', '$', '…', '{')

# `some/path/file.ext` in prose or a table cell.
PATH_RE = re.compile(
    r'`([A-Za-z0-9_./-]+\.(?:md|cpp|cc|h|hpp|c|xml|py|sh|json|mk|bats|yml|yaml|html|txt))`')

DOC_INDEX = 'docs/devel/CLAUDE.md'
DOC_DIR = 'docs/devel'

# Docs deliberately not routed from the index.
INDEX_EXEMPT = {
    'CLAUDE.md',           # the index itself
}


def repo_files():
    """Every file in the repo, for suffix resolution."""
    out = set()
    for root, dirs, files in os.walk('.'):
        dirs[:] = [d for d in dirs if d not in SKIP_DIRS]
        for f in files:
            out.add(os.path.join(root, f)[2:])
    return out


def scan_targets():
    """Agent-facing docs: every CLAUDE.md, plus everything under .claude/skills/."""
    targets = []
    for root, dirs, files in os.walk('.'):
        dirs[:] = [d for d in dirs if d not in SKIP_DIRS]
        rel = root[2:]
        if rel.startswith('lib/'):
            continue
        for f in files:
            path = os.path.join(rel, f) if rel else f
            if f == 'CLAUDE.md' or (rel.startswith('.claude/skills') and f.endswith('.md')):
                targets.append(path)
    return sorted(targets)


def check_refs(targets, allpaths, verbose=False):
    problems = []
    for target in targets:
        base = os.path.dirname(target)
        try:
            text = open(target, errors='ignore').read()
        except OSError:
            continue
        for m in PATH_RE.finditer(text):
            ref = m.group(1)
            if any(c in ref for c in PLACEHOLDER_CHARS):
                continue
            if any(s in ref for s in EXEMPT_SUBSTRINGS):
                continue
            if os.path.exists(ref):
                continue
            if base and os.path.exists(os.path.join(base, ref)):
                continue
            # a bare or partial path is fine if exactly that suffix exists somewhere
            if any(p == ref or p.endswith('/' + ref) for p in allpaths):
                continue
            line = text.count('\n', 0, m.start()) + 1
            problems.append((target, line, ref))
    return problems


def check_index():
    if not os.path.isfile(DOC_INDEX):
        return [], []
    index_text = open(DOC_INDEX, errors='ignore').read()
    present = set()
    for f in os.listdir(DOC_DIR):
        full = os.path.join(DOC_DIR, f)
        if os.path.isfile(full) and (f.endswith('.md') or f.endswith('.html')):
            present.add(f)
    unindexed = sorted(f for f in present - INDEX_EXEMPT
                       if '`%s`' % f not in index_text)
    return unindexed, sorted(present)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--refs', action='store_true', help='Only check references resolve')
    ap.add_argument('--index', action='store_true', help='Only check index completeness')
    ap.add_argument('--list', action='store_true', help='List scanned files')
    args = ap.parse_args()

    do_refs = args.refs or not args.index
    do_index = args.index or not args.refs

    targets = scan_targets()
    if args.list:
        for t in targets:
            print('  scanned:', t)

    exit_code = 0

    if do_refs:
        problems = check_refs(targets, repo_files())
        if problems:
            print('❌ Doc references that do not resolve:')
            for target, line, ref in problems:
                print('   %s:%d: `%s`' % (target, line, ref))
            print('   Fix the path, or use a <placeholder> if it is illustrative.')
            exit_code = 1
        else:
            print('✅ Doc references: all resolve (%d files scanned)' % len(targets))

    if do_index:
        unindexed, present = check_index()
        if unindexed:
            print('❌ Docs in %s/ missing from %s:' % (DOC_DIR, DOC_INDEX))
            for f in unindexed:
                print('   %s' % f)
            print('   Add a row, or add to INDEX_EXEMPT if it is deliberately unrouted.')
            exit_code = 1
        else:
            print('✅ Doc index: all %d docs routed' % len(present))

    return exit_code


if __name__ == '__main__':
    sys.exit(main())
