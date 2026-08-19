#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Reject hand-casting an lv_subject int into PrintState or PrintJobState.

`lv_subject_get_int()` returns int, so `static_cast<PrintState>(...)` compiles
against WHICHEVER subject the author happened to name. The two print enums do not
share numbering past index 0:

    PrintJobState  STANDBY=0  PRINTING=1   PAUSED=2   COMPLETE=3  CANCELLED=4 ERROR=5
    PrintState     Idle=0     Preparing=1  Printing=2 Paused=3    Complete=4  ...

so pairing the cast with the wrong subject is silent: a COMPLETE job reads back as
Paused, a PRINTING one as Preparing. That exact mistake was made twice while
migrating guards onto the lifecycle - ams_backend_ad5x_ifs and power_device_state
- and both times the code compiled, ran, and answered a different question.

Use the typed accessors instead, which pair each subject with its own enum:

    state.get_print_lifecycle()   -> PrintState
    state.get_print_job_state()   -> PrintJobState

Per-line opt-out: `// PRINT_STATE_CAST_OK: <reason>`
"""
import re
import sys
from pathlib import Path

PATTERN = re.compile(
    r"static_cast<\s*(PrintState|helix::PrintJobState|PrintJobState)\s*>\s*\(\s*lv_subject_get_int"
)
OPT_OUT = "PRINT_STATE_CAST_OK:"

# The derivation layer legitimately converts between the wire and the lifecycle.
ALLOWLIST = {
    "src/printer/printer_print_state.cpp",  # get_print_job_state / get_print_lifecycle themselves
    "src/printer/print_lifecycle_state.cpp",
}


def main(argv: list[str]) -> int:
    root = Path(__file__).resolve().parent.parent
    files = [Path(a) for a in argv[1:]] or [
        p for d in ("src", "include") for p in (root / d).rglob("*.cpp")
    ]
    if not argv[1:]:
        files += [p for d in ("src", "include") for p in (root / d).rglob("*.h")]

    hits = []
    for f in files:
        try:
            rel = str(f.resolve().relative_to(root))
        except ValueError:
            rel = str(f)
        if rel in ALLOWLIST:
            continue
        try:
            lines = f.read_text().splitlines()
        except (OSError, UnicodeDecodeError):
            continue
        for i, line in enumerate(lines, 1):
            if not PATTERN.search(line):
                continue
            window = "\n".join(lines[max(0, i - 4) : i])
            if OPT_OUT in window or OPT_OUT in line:
                continue
            hits.append(f"{rel}:{i}: {line.strip()}")

    if hits:
        print("Hand-cast of an lv_subject int into a print enum:")
        for h in hits:
            print("  " + h)
        print()
        print("  Use the typed accessor that owns the pairing:")
        print("    state.get_print_lifecycle()  -> PrintState")
        print("    state.get_print_job_state()  -> PrintJobState")
        print(f"  Genuinely needed? Annotate with // {OPT_OUT} <reason>")
        return 1

    print(f"✅ print-state casts: 0 hand-casts ({len(files)} files scanned)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
