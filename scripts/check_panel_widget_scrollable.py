#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Lint gate: every <lv_obj> in a home-panel widget must state `scrollable`.
#
# In XML, <lv_obj> keeps LVGL's LV_OBJ_FLAG_SCROLLABLE default, which is ON.
# HelixScreen's theme overrides lv_obj's width/height/border/background/padding
# but NOT scrollable, so an author who reads "our theme makes lv_obj a pure
# layout container" reasonably concludes scrolling is off. It is not.
#
# That shipped a bug. ui_xml/components/panel_widget_print_status.xml had
# print_card_idle and print_card_idle_compact with no `scrollable` attribute, so
# they qualified for a page-scroll gutter and the chevrons rendered on top of the
# widget's thumbnail on an 800x480 ESP32 K-Touch (fixed in 7d69130df). Home
# widget tiles are also drag-scrolled by the grid beneath them, so an accidental
# scrollable container inside a tile steals the drag the grid wants.
#
# The rule is DECLARED INTENT, not a particular value. A genuinely scrolling
# list writes scrollable="true" and passes; a layout container writes
# scrollable="false" and passes. What fails is saying nothing, because saying
# nothing is how the default silently wins.
#
# Flagged, in ui_xml/components/panel_widget_*.xml only:
#   root   <view extends="lv_obj"> with no scrollable=  → same hazard, same fix
#   child  <lv_obj ...>            with no scrollable=
#
# NOT flagged:
#   - <view extends="ui_card"> and any other extends=. ui_card's create handler
#     already clears the flag (src/ui/ui_card.cpp:53), so those roots are safe.
#     Other extends= inherit whatever that component declared; the declaration
#     belongs on the component that actually extends lv_obj, not on every file
#     that composes it.
#   - Widgets other than lv_obj. lv_button, ui_card, text_body and friends are
#     not the hazard this gate is about.
#   - XML files outside ui_xml/components/panel_widget_*.xml. The bug is specific
#     to home-widget tiles living inside a drag-scrolled grid.
#
# There is deliberately no opt-out comment: the fix is one attribute, it is
# always available, and either value passes. An escape hatch would only ever be
# used to avoid deciding, which is exactly what the gate exists to prevent.
#
# This is a RATCHET, not a wall. 21 sites predate the gate. They are NOT fixed
# here on purpose: fixing one means guessing its author's intent, and some of
# them genuinely should scroll, so a blanket scrollable="false" would be a
# behavior change smuggled in under a lint commit. The baseline freezes today's
# count so no NEW undeclared container can be added - the number may fall as
# sites are decided one at a time, never rise.
#
# Usage:
#   check_panel_widget_scrollable.py                   # fail on any violation
#   check_panel_widget_scrollable.py --max-allowed 21  # ratcheting baseline
#   check_panel_widget_scrollable.py --summary         # counts only
#   check_panel_widget_scrollable.py --list            # every site, file:line
#   check_panel_widget_scrollable.py --rule child      # one rule only

import argparse
import fnmatch
import os
import re
import sys

SCAN_DIR = os.path.join('ui_xml', 'components')
SCAN_GLOB = 'panel_widget_*.xml'

# The one extends= that is already safe: ui_card's create handler clears
# LV_OBJ_FLAG_SCROLLABLE before XML attributes are applied.
SAFE_ROOT_EXTENDS = {'ui_card'}

RULES = ('root', 'child')

FIX = {
    'root':  'scrollable="false" on <view extends="lv_obj"> (or "true" if it really scrolls)',
    'child': 'scrollable="false" on the <lv_obj> (or "true" if it really scrolls)',
}

# An open tag, attributes possibly spanning lines. Quoted values may contain '>',
# so the attribute run consumes whole quoted strings rather than stopping at the
# first '>'.
TAG_RE = re.compile(r'<(lv_obj|view)\b((?:[^>"\']|"[^"]*"|\'[^\']*\')*)/?>')

ATTR_RE = re.compile(r'\bscrollable\s*=')
EXTENDS_RE = re.compile(r'\bextends\s*=\s*"([^"]*)"')
COMMENT_RE = re.compile(r'<!--.*?-->', re.S)


def strip_comments(src):
    """Blank out comment bodies, keeping newlines so line numbers stay true."""
    return COMMENT_RE.sub(lambda m: re.sub(r'[^\n]', ' ', m.group(0)), src)


def is_panel_widget(path):
    return fnmatch.fnmatch(os.path.basename(path), SCAN_GLOB)


def scan_file(path):
    try:
        src = open(path, errors='ignore').read()
    except OSError:
        return []

    src = strip_comments(src)
    lines = src.split('\n')
    hits = []

    for m in TAG_RE.finditer(src):
        tag, attrs = m.group(1), m.group(2)

        if tag == 'view':
            extends = EXTENDS_RE.search(attrs)
            # A <view> with no extends= is not an lv_obj; one extending ui_card
            # (or any other component) is somebody else's declaration to make.
            if not extends or extends.group(1) != 'lv_obj':
                continue
            rule = 'root'
        else:
            rule = 'child'

        if ATTR_RE.search(attrs):
            continue

        lineno = src.count('\n', 0, m.start()) + 1
        hits.append((path, lineno, rule, lines[lineno - 1].strip()[:100]))

    return hits


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--max-allowed', type=int, default=None,
                    help='Pass if total violations <= N (ratcheting baseline). '
                         'Default: fail on any.')
    ap.add_argument('--summary', action='store_true', help='Per-rule counts only')
    ap.add_argument('--list', action='store_true', help='Print every site')
    ap.add_argument('--rule', choices=RULES, help='Restrict to one rule')
    ap.add_argument('paths', nargs='*',
                    help=f'Files to scan (default: {SCAN_DIR}/{SCAN_GLOB})')
    args = ap.parse_args()

    if args.paths:
        # Scope holds even for explicit paths: a hand-passed component that is not
        # a panel widget is out of scope, not an exemption to argue about.
        targets = [p for p in args.paths if is_panel_widget(p) and os.path.isfile(p)]
    else:
        targets = []
        for root, _, files in os.walk(SCAN_DIR):
            targets += [os.path.join(root, f) for f in files if is_panel_widget(f)]

    hits = []
    for path in sorted(targets):
        hits += scan_file(path)
    if args.rule:
        hits = [h for h in hits if h[2] == args.rule]

    by_rule = {}
    for _, _, rule, _ in hits:
        by_rule[rule] = by_rule.get(rule, 0) + 1
    total = len(hits)

    if args.list:
        for path, lineno, rule, line in hits:
            print(f'{path}:{lineno}: [{rule}] {line}')
        print()

    if args.summary or args.list:
        for rule in RULES:
            n = by_rule.get(rule, 0)
            if not args.rule or args.rule == rule:
                print(f'  {rule:<7} {n:>5}   → {FIX[rule]}')
        print(f'  {"TOTAL":<7} {total:>5}')

    limit = args.max_allowed
    if limit is None:
        if total:
            print(f'❌ Panel-widget scrollable: {total} <lv_obj> with no scrollable attribute.')
            return 1
        print('✅ Panel-widget scrollable: every <lv_obj> states its intent.')
        return 0

    if total > limit:
        print(f'❌ Panel-widget scrollable: {total} undeclared exceeds baseline ({limit}).')
        print('   <lv_obj> defaults to SCROLLABLE; say scrollable="false" (or "true").')
        print('   Run: python3 scripts/check_panel_widget_scrollable.py --list')
        return 1
    if total < limit:
        print(f'✅ Panel-widget scrollable: {total} (baseline {limit} - ratchet the baseline down)')
        return 0
    print(f'✅ Panel-widget scrollable: {total} == baseline ({limit})')
    return 0


if __name__ == '__main__':
    sys.exit(main())
