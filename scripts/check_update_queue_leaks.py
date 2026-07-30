#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Ratcheting gate on cross-test UpdateQueue leaks.
#
# A test that returns with callbacks still queued hands them to the next test.
# HelixTestFixture's ctor drains the queue, notifying subjects that died with the
# leaking test — a SIGSEGV in lv_subject_notify that lands on whichever test
# constructed a fixture next, so it moves with shard composition and looks like a
# bug in an innocent test. That is prestonbrown/helixscreen#1146.
#
# The isolation listener discards leftover callbacks between tests and reports
# each one as "[ISOLATION-LEAK] ... Producers: <tag>". Discarding makes the crash
# unreachable, but the underlying defect — a queue_update site capturing raw this
# with no lifetime guard (docs/devel/THREADING.md §2) — is still there. This gate
# stops the population growing while it gets worked down (#1166).
#
# This is a RATCHET, not a wall. The baseline freezes today's count so the debt can
# only shrink: a new leak fails the build, and fixing one lowers the number.
#
# The count is NOT shard-independent, so the baseline is only meaningful against a
# fixed invocation. Measured on the same build: 305 leaks unsharded, 286 across 8
# shards. The listener does discard after every test, but several producers are
# process singletons (AmsState::set_active_tool_port_present and friends) that only
# queue when a value actually CHANGES — and whether it changes depends on what
# earlier tests in the same process left in that singleton. Splitting the run
# changes which tests share a process, so it changes the count.
#
# The baseline is keyed by PRODUCER where a producer is known, and by victim test
# name only where it is not. Keying purely on the victim was #1170: the producer is
# stable but the victim is whichever test ran next and drained, and the debounce/
# timer-driven producers (Ad5xIfsBackend::zcolor_debounce_apply, reread_apply,
# AmsState::set_active_tool_port_present) land on a different one depending on
# wall-clock timing, not just ordering. Across four unsharded runs of one binary the
# victim set moved by 4 tests while the producer tag set did not move at all.
#
# Keying everything by producer is not an option either: 266 of ~305 reports are
# <untagged>, because most queue_update() call sites pass no tag. That would collapse
# 84% of the population into one bucket and the gate would stop catching new leaks.
# So attribution quality decides the key, per producer entry within a report:
#
#   real tag   -> "tag:<producer>"     the victim name is DELIBERATELY discarded
#   <untagged> -> "test:<victim name>"  no tag to key on; the victim is the best
#                                       identity available, and it is stable because
#                                       untagged producers are not timer-driven
#
# Measured across the same four runs: 272 keys, union == intersection, zero flap.
#
# A key set alone would still miss an EXTRA untagged callback appearing inside an
# already-baselined test, so the baseline also carries a max-untagged-callbacks
# ceiling. That total was exactly 717 in all four runs while the tagged total flapped
# (155/148/152/152) — the untagged population is the deterministic half.
#
# The baseline below is therefore pinned to ONE canonical invocation:
#
#     ./build/bin/helix-tests "~[.] ~[slow]" 2>&1 | tee run.log
#
# The 2>&1 is REQUIRED, not incidental. The listener reports via fprintf(stderr)
# while Catch2's summary goes to stdout, so a log missing either stream is rejected:
# the gate demands a Catch2 run summary before it will believe a low leak count.
# Without that, a stdout-only log reads as "0 leaks" and passes forever.
#
# Do not compare a baseline taken this way against summed shard logs; the numbers
# are not the same quantity. This is also why the gate runs in nightly rather than
# on every PR — build.yml shards across $(nproc), which varies by runner.
#
# Usage:
#   check_update_queue_leaks.py run.log                  # count
#   cat run.log | check_update_queue_leaks.py -          # or from stdin
#   check_update_queue_leaks.py --max-allowed 305 run.log  # ratcheting baseline
#   check_update_queue_leaks.py --list run.log           # every leaking test
#   check_update_queue_leaks.py --by-producer run.log    # group by queueing tag

import argparse
import collections
import re
import sys

# Matches the listener's report in tests/unit/test_isolation_listener.cpp.
LEAK_RE = re.compile(
    r'\[ISOLATION-LEAK\] test "(?P<test>.*)" left (?P<count>\d+) queued UpdateQueue '
    r'callback\(s\); discarded\. Producers: (?P<producers>.*)$'
)


# Catch2's end-of-run summary. Its presence proves the log really is a completed
# test run, which is what makes "zero leaks" trustworthy rather than just silent.
RUN_MARKER_RE = re.compile(r'All tests passed|test cases:\s*\d+|assertions:\s*\d+')


def parse(streams):
    """Return (leaks, total_callbacks, saw_run). leaks is [(test, count, producers)]."""
    leaks = []
    total_callbacks = 0
    saw_run = False
    for stream in streams:
        for line in stream:
            if not saw_run and RUN_MARKER_RE.search(line):
                saw_run = True
            m = LEAK_RE.search(line)
            if not m:
                continue
            count = int(m.group('count'))
            leaks.append((m.group('test'), count, m.group('producers').strip()))
            total_callbacks += count
    return leaks, total_callbacks, saw_run


UNTAGGED = '<untagged>'

# "Tag x3" -> ("Tag", 3); a bare "Tag" means one callback.
PRODUCER_RE = re.compile(r'^(?P<tag>.*?)(?: x(?P<n>\d+))?$')

CEILING_KEY = 'max-untagged-callbacks'


def split_producers(producers):
    """Yield (tag, count) for each entry in a report's Producers: field."""
    for entry in producers.split(','):
        entry = entry.strip()
        if not entry:
            continue
        m = PRODUCER_RE.match(entry)
        yield m.group('tag').strip(), int(m.group('n') or 1)


def keys_and_untagged(leaks):
    """Return (key set, untagged callback total).

    Attribution quality picks the key. A known producer keys on itself and throws
    the victim away — the victim is the part that migrates (#1170). An untagged
    producer has nothing else to key on, so it falls back to the victim test.
    """
    keys = set()
    untagged_callbacks = 0
    for test, _, producers in leaks:
        for tag, n in split_producers(producers):
            if tag == UNTAGGED:
                keys.add('test:' + test)
                untagged_callbacks += n
            else:
                keys.add('tag:' + tag)
    return keys, untagged_callbacks


BASELINE_HEADER = (
    '# Known cross-test UpdateQueue leaks (#1166). Generated by:\n'
    '#   ./build/bin/helix-tests "~[.] ~[slow]" 2>&1 | tee run.log\n'
    '#   scripts/check_update_queue_leaks.py --write-baseline <this file> run.log\n'
    '#\n'
    '# Each leak is keyed by ATTRIBUTION QUALITY, not by who it landed on:\n'
    '#   tag:<producer>  a queue_update() site that passes a tag. The victim test is\n'
    '#                   deliberately not recorded — the producer is stable, but the\n'
    '#                   victim is whichever test drained next, and the debounce-driven\n'
    '#                   producers move between victims on wall-clock timing (#1170).\n'
    '#   test:<name>     an <untagged> producer. Nothing better to key on, and stable:\n'
    '#                   untagged producers are not timer-driven.\n'
    '#\n'
    '# A key NOT listed here fails the build. The list may SHRINK as leaks are fixed;\n'
    '# it must not grow. Regenerate rather than hand-editing.\n'
    '#\n'
    '# The ceiling below catches an EXTRA untagged callback appearing inside a test\n'
    '# that is already listed, which the key set alone would miss. Untagged callbacks\n'
    '# totalled exactly 717 across four identical runs; the tagged total flapped.\n'
)


def read_baseline(path):
    """Return (key set, untagged ceiling or None)."""
    keys = set()
    ceiling = None
    try:
        with open(path) as fh:
            for line in fh:
                line = line.strip()
                if not line or line.startswith('#'):
                    continue
                if line.startswith(CEILING_KEY + ':'):
                    ceiling = int(line.split(':', 1)[1].strip())
                    continue
                keys.add(line)
    except OSError as e:
        print(f'❌ UpdateQueue leaks: cannot read baseline {path}: {e}')
        sys.exit(2)
    except ValueError:
        print(f'❌ UpdateQueue leaks: malformed {CEILING_KEY} in {path}')
        sys.exit(2)
    # A pre-#1170 baseline is bare test names with no prefix. Those would read as
    # zero known keys and fail on all ~272, which is a confusing way to say "stale".
    if keys and not any(k.startswith(('tag:', 'test:')) for k in keys):
        print(f'❌ UpdateQueue leaks: {path} is in the pre-#1170 bare-name format.')
        print('   Regenerate it: --write-baseline <file> run.log')
        sys.exit(2)
    return keys, ceiling


def open_streams(paths):
    if not paths or paths == ['-']:
        return [sys.stdin]
    handles = []
    for p in paths:
        try:
            handles.append(open(p, 'r', errors='replace'))
        except OSError as e:
            print(f'❌ UpdateQueue leaks: cannot read {p}: {e}')
            sys.exit(2)
    return handles


def main():
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    ap.add_argument('logs', nargs='*', help='Test output logs (or - for stdin)')
    ap.add_argument(
        '--max-allowed',
        type=int,
        default=None,
        help='Pass if leaking tests <= N (ratcheting baseline). Omit to fail on any leak.',
    )
    ap.add_argument(
        '--baseline',
        metavar='FILE',
        help='Known-leak key set (tag:<producer> / test:<victim>) plus an untagged '
        'callback ceiling. Fail on a key NOT listed, or on the ceiling being exceeded. '
        'Preferred over --max-allowed, which flaps: the total count is timing-dependent.',
    )
    ap.add_argument(
        '--write-baseline',
        metavar='FILE',
        help='Write the leak keys and untagged ceiling in these logs to FILE and exit.',
    )
    ap.add_argument('--list', action='store_true', help='Print every leaking test')
    ap.add_argument(
        '--by-producer', action='store_true', help='Group leaks by queueing tag'
    )
    ap.add_argument('--summary', action='store_true', help='Counts only')
    args = ap.parse_args()

    streams = open_streams(args.logs)
    leaks, total_callbacks, saw_run = parse(streams)
    for s in streams:
        if s is not sys.stdin:
            s.close()

    total = len(leaks)

    # Absence of evidence is not evidence of absence. A log with no reports is only
    # trustworthy if it also shows a completed run — otherwise it is a broken
    # invocation (wrong path, crashed early, or stderr not captured, since the
    # listener reports via fprintf(stderr)) and would silently "pass" forever.
    if not saw_run:
        print('❌ UpdateQueue leaks: no Catch2 run summary found in the log(s).')
        print('   This does not look like a completed test run. Capture stderr:')
        print('     ./build/bin/helix-tests "~[.] ~[slow]" 2>&1 | tee run.log')
        return 2

    if args.list:
        for test, count, producers in sorted(leaks):
            print(f'{count:4d}  {test}  [{producers}]')
        print()

    if args.by_producer:
        by_tag = collections.Counter()
        for _, _, producers in leaks:
            for tag in producers.split(','):
                by_tag[re.sub(r' x\d+$', '', tag.strip())] += 1
        for tag, n in by_tag.most_common():
            print(f'{n:4d}  {tag}')
        print()

    detail = f'{total} leaking test(s), {total_callbacks} callback(s)'
    keys, untagged_callbacks = keys_and_untagged(leaks)

    if args.write_baseline:
        # Regenerating from several runs is good practice, but the ceiling is a
        # PER-RUN quantity — summing it across logs would write a ceiling several
        # times the real one and neuter it. Union the keys, take the highest ceiling.
        if len(args.logs) > 1:
            keys = set()
            untagged_callbacks = 0
            for path in args.logs:
                with open(path, 'r', errors='replace') as fh:
                    per_log = parse([fh])[0]
                k, n = keys_and_untagged(per_log)
                keys |= k
                untagged_callbacks = max(untagged_callbacks, n)
        with open(args.write_baseline, 'w') as fh:
            fh.write(BASELINE_HEADER)
            fh.write(f'{CEILING_KEY}: {untagged_callbacks}\n')
            for k in sorted(keys):
                fh.write(k + '\n')
        print(
            f'✅ Wrote {len(keys)} known leak key(s) and a ceiling of '
            f'{untagged_callbacks} untagged callback(s) to {args.write_baseline}'
        )
        return 0

    if args.baseline:
        known, ceiling = read_baseline(args.baseline)
        new = sorted(keys - known)
        gone = sorted(known - keys)
        failed = False
        if new:
            print(f'❌ UpdateQueue leaks: {len(new)} leak key(s) not in the baseline.')
            for k in new:
                print(f'     {k}')
            print('   A test returned with callbacks queued; the next fixture drain would')
            print('   notify subjects that died with it. Drain in the owning fixture dtor,')
            print('   while its subjects are still alive.')
            print('   A tag: key is a NEW producer. A test: key is a new untagged leaker —')
            print('   tag its queue_update() site to make the next report point at the code.')
            print('   See prestonbrown/helixscreen#1166 and docs/devel/THREADING.md §2.')
            failed = True
        # The key set cannot see a test that was already leaking start leaking MORE,
        # so the untagged callbacks are counted too. Only the untagged half: the
        # tagged half is the timing-driven population that made #1170 flap.
        if ceiling is not None and untagged_callbacks > ceiling:
            print(
                f'❌ UpdateQueue leaks: {untagged_callbacks} untagged callback(s) '
                f'exceeds the baseline ceiling ({ceiling}).'
            )
            print('   An already-baselined test is leaking more than it was. Find it with:')
            print('     scripts/check_update_queue_leaks.py --list <logs>')
            failed = True
        if failed:
            return 1
        # Absences are expected: the tagged producers flap with thread timing, so a
        # short "gone" list is noise, not progress. Only a sustained drop is worth
        # rewriting the baseline for, hence this reports without failing.
        print(f'✅ UpdateQueue leaks: {detail}; no new leak keys ({len(known)} known).')
        if ceiling is not None:
            print(f'   {untagged_callbacks} untagged callback(s), ceiling {ceiling}.')
        if gone:
            print(f'   {len(gone)} baseline key(s) did not leak this run.')
            print('   If that holds across runs, regenerate with --write-baseline.')
        return 0

    if args.max_allowed is None:
        if total:
            print(f'❌ UpdateQueue leaks: {detail}.')
            print('   Drain in the owning fixture dtor, while its subjects are alive.')
            return 1
        print('✅ UpdateQueue leaks: none.')
        return 0

    limit = args.max_allowed
    if total > limit:
        print(f'❌ UpdateQueue leaks: {detail} exceeds baseline ({limit}).')
        print('   A test returned with callbacks queued; the next fixture drain would')
        print('   notify subjects that died with it. Drain in the owning fixture dtor.')
        print('   Run: python3 scripts/check_update_queue_leaks.py --list <logs>')
        print('   See prestonbrown/helixscreen#1166 and docs/devel/THREADING.md §2.')
        return 1
    if total < limit:
        print(f'✅ UpdateQueue leaks: {detail} (baseline {limit} — ratchet the baseline down)')
        return 0
    print(f'✅ UpdateQueue leaks: {detail} == baseline ({limit})')
    return 0



if __name__ == '__main__':
    sys.exit(main())
