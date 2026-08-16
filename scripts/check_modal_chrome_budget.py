#!/usr/bin/env python3
# Copyright (C) 2025-2026 356C LLC
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Lint gate: a modal's chrome must match the content cap it budgets against.
#
# Background (#1277). `#dialog_content_max` is a single global ladder whose
# per-breakpoint values are derived from ONE chrome shape — modal_dialog.xml's:
#
#     header + scrollable content + divider + button row  <=  85% of the screen
#
# The dialog root is `height="content"` with `style_max_height="85%"` and
# `scrollable="false"`. When the children total more than the cap, the root
# clamps and the overflow falls off the BOTTOM — which is the button row. The
# user is left with a modal they cannot dismiss.
#
# Measured on a 480x272 (micro) panel, ams_loading_error_modal.xml with a long
# fault string and the AFC diagram pinned below the scroll area:
#
#     card         y= 21  h=231  -> bottom 252   (clamped at exactly the cap)
#     scroll area  y= 50  h=140  -> bottom 190   (clamped at dialog_content_max)
#     AFC graphic  y=190  h= 38  -> bottom 228
#     button row   y=236  h= 27  -> bottom 263   <-- 11px PAST the card bottom
#
# LVGL cannot solve this in layout: lv_flex.c implements `grow` only, there is no
# CSS-style flex-shrink, so a child cannot be squeezed to make room. The budget
# has to be right up front.
#
# THE RULE
#   Everything except a divider and the button row lives INSIDE the scroll
#   container. Then the one global token is correct by construction.
#
#   Where a block must stay pinned and visible while the text scrolls (the AFC
#   fault diagram — the whole point of that modal is showing WHERE filament
#   stopped, and burying it below the fold guts the feature), the container opts
#   into `#dialog_content_pinned_max` instead, which reserves that block's height.
#
# WHAT IS FLAGGED
#   An element that follows a `#dialog_content_max` container among its siblings
#   and is neither a divider nor a button row. Fix it by moving the element
#   inside the scroll container, or by switching the container to
#   `#dialog_content_pinned_max`.
#
# NOT FLAGGED
#   - Dividers and button rows after the container. That is the budgeted shape.
#   - Containers already on `#dialog_content_pinned_max`, which get one extra
#     pinned block before they are flagged again.
#   - Modals that set an explicit `style_max_height` of their own on the root
#     instead of inheriting the 85% cap. They have opted out of the shared
#     budget and are the author's problem (klipper_recovery_dialog raises its
#     root to 90% precisely because it carries two button rows).
#   - Two sibling blocks that can never be visible together, because they are
#     bound to the same subject with different `ref_value`s. Their heights do
#     not sum (hidden_network_modal's form vs its connecting spinner).
#
# KNOWN GAP
#   Every divider and button row is treated as budgeted, but the ladder is sized
#   for exactly ONE button row. A modal carrying two (action_prompt_modal's
#   button_container plus its footer_container, both of which can be visible at
#   once) is therefore under-counted here and can still overrun. Closing it means
#   deciding what a second row costs per breakpoint, which wants a measurement on
#   a 480x272 panel rather than a guess — action_prompt_modal is currently
#   unmeasured because it needs a live Klipper prompt to reach. Until then this
#   gate catches pinned NON-button blocks only.
#
# Opt out with `MODAL_CHROME_OK: <reason>` anywhere in the file.

import argparse
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

SCAN_DIRS = ('ui_xml',)

# android/app/src/main/assets/ui_xml/ is a gradle copy of ui_xml/, not a source
# of truth. build/ and .worktrees/ are likewise derived trees.
SKIP_PARTS = ('android', 'build', '.worktrees', 'translations')

STANDARD_TOKEN = '#dialog_content_max'
PINNED_TOKEN = '#dialog_content_pinned_max'

# The cap the shared budget is derived from. A root carrying anything else has
# opted out and sized itself.
DEFAULT_ROOT_CAP = '85%'

OPT_OUT = 'MODAL_CHROME_OK'

# Chrome the budget already accounts for, allowed to follow the scroll container.
DIVIDER_TAGS = {'divider_horizontal'}
BUTTON_ROW_TAGS = {'modal_button_row'}


def is_button_row(el):
    """Is this element the modal's button row?

    modal_dialog.xml and several modals hand-roll the row rather than using the
    modal_button_row component, so the tag name alone does not identify it. Two
    hand-rolled shapes exist:

      - fixed height:  <lv_obj height="#button_height" flex_flow="row">
      - wrapping:      <lv_obj name="button_container" flex_flow="row_wrap"
                               height="content">   (action_prompt_modal)

    The wrapping form is deliberately recognised. Flagging it would be a false
    positive — it IS the budgeted button row — but note it can grow to TWO rows
    when enough buttons wrap, which the single-row budget does not cover.
    """
    if el.tag in BUTTON_ROW_TAGS:
        return True
    if el.get('height') == '#button_height':
        return True
    name = el.get('name') or ''
    flex = el.get('flex_flow') or ''
    return 'button' in name and flex.startswith('row')


def is_budgeted_chrome(el):
    return el.tag in DIVIDER_TAGS or is_button_row(el)


def hidden_bindings(el):
    """{subject: ref_value} for each `hidden` flag binding on this element."""
    out = {}
    for binding in el:
        if not binding.tag.startswith('bind_flag_if'):
            continue
        if binding.get('flag') != 'hidden':
            continue
        subject = binding.get('subject')
        if subject is not None:
            out[subject] = (binding.tag, binding.get('ref_value'))
    return out


def mutually_exclusive(a, b):
    """Can these two siblings never be on screen at the same time?

    hidden_network_modal shows either its entry form or its connecting spinner,
    switched by `hidden_connecting`: the form hides on 1, the spinner hides on 0.
    Their heights never sum, so counting the second against the first's budget
    is a false positive — and a gate that fires on correct markup gets disabled.
    """
    a_bind, b_bind = hidden_bindings(a), hidden_bindings(b)
    for subject, (a_tag, a_ref) in a_bind.items():
        if subject not in b_bind:
            continue
        b_tag, b_ref = b_bind[subject]
        # Same predicate, different trigger value -> exactly one is visible.
        if a_tag == b_tag and a_ref != b_ref:
            return True
    return False


def find_violations(path):
    """Yield (parent_tag, offending_tag, token) for each unbudgeted pinned block."""
    text = path.read_text(encoding='utf-8', errors='replace')
    if OPT_OUT in text:
        return

    try:
        root = ET.fromstring(text)
    except ET.ParseError:
        # Malformed XML is the xmllint pass's job, not this gate's. Staying
        # silent here beats reporting a parse error as a chrome violation.
        return

    # A modal that sets its own root cap has opted out of the shared 85% budget
    # and did its own arithmetic (klipper_recovery_dialog raises the card to 90%
    # precisely because it carries two button rows, and records the numbers in a
    # comment). Only the modals inheriting the standard cap are this gate's
    # business.
    for view in root.iter('view'):
        root_cap = view.get('style_max_height')
        if root_cap is not None and root_cap != DEFAULT_ROOT_CAP:
            return

    for parent in root.iter():
        children = list(parent)
        for idx, child in enumerate(children):
            token = child.get('style_max_height')
            if token not in (STANDARD_TOKEN, PINNED_TOKEN):
                continue

            # A pinned container has already paid for exactly one extra block.
            budget = 1 if token == PINNED_TOKEN else 0

            for sibling in children[idx + 1:]:
                if is_budgeted_chrome(sibling):
                    continue
                if mutually_exclusive(child, sibling):
                    continue
                if budget > 0:
                    budget -= 1
                    continue
                yield (parent.tag, sibling.tag, token)


def iter_xml_files(repo_root):
    for scan_dir in SCAN_DIRS:
        base = repo_root / scan_dir
        if not base.is_dir():
            continue
        for path in sorted(base.rglob('*.xml')):
            if any(part in SKIP_PARTS for part in path.parts):
                continue
            yield path


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--list', action='store_true', help='list every violation')
    ap.add_argument('--repo-root', default='.', help='repository root')
    args = ap.parse_args()

    repo_root = Path(args.repo_root).resolve()

    violations = []
    for path in iter_xml_files(repo_root):
        for parent_tag, tag, token in find_violations(path):
            violations.append((path.relative_to(repo_root), parent_tag, tag, token))

    if not violations:
        print('✅ Modal chrome budget: every pinned block is accounted for')
        return 0

    print(f'❌ Modal chrome budget: {len(violations)} unbudgeted pinned block(s)\n')
    for rel, parent_tag, tag, token in violations:
        print(f'  {rel}: <{tag}> follows a {token} container (in <{parent_tag}>)')

    print(
        '\n'
        'Each of these sits BELOW the scroll area and outside the budget that\n'
        f'{STANDARD_TOKEN} was sized for, so the modal overruns its 85% cap and\n'
        'the button row is clipped off the bottom of the card.\n'
        '\n'
        'Fix by either:\n'
        '  - moving the element INSIDE the scroll container (preferred), or\n'
        f'  - switching that container to {PINNED_TOKEN}, which reserves the\n'
        '    height of one pinned block, when it must stay visible while the\n'
        '    text scrolls.\n'
        '\n'
        f'Deliberate exception? Add "{OPT_OUT}: <reason>" to the file.'
    )
    return 1


if __name__ == '__main__':
    sys.exit(main())
