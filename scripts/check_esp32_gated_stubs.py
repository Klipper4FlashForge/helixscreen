#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Catch ESP32 link failures caused by dropping a file that carries its own stubs.

A subsystem gated off for a constrained target is compiled out with
``#if HELIX_HAS_<FEATURE>``, and the file's ``#else`` arm supplies no-op
definitions so callers still link. That arm only works if the file is compiled,
which for the ESP32 firmware means being listed in ``app_srcs.txt``. Curating
that list by subsystem name invites dropping such a file as "the viewer" and
taking its stubs with it, and the failure surfaces only as an undefined
reference from a toolchain nothing outside CI has.

Excluding one is legitimate when nothing compiled for the target calls into it,
which is why this checks references rather than demanding every gated file be
listed: a file is required only once some listed translation unit names a symbol
its stub arm defines.
"""

import glob
import os
import re
import sys

APP_SRCS = "firmware/helixscreen-esp32/components/helixapp/app_srcs.txt"
GATE_OPEN = re.compile(r"^#if\s+(HELIX_HAS_\w+)", re.M)
GATE_ELSE = re.compile(r"^#else\s*//\s*!(HELIX_HAS_\w+)", re.M)

# Definitions the stub arm supplies: `Type Class::method(`, `Type func(`, and
# `ns::func(`. The class name alone is enough to spot a caller naming the type.
# Anchored at column 0: a definition sits at file scope, while a *call* to
# something like spdlog::debug() is indented inside a function body. Without the
# anchor every library call in the arm reads as a symbol the file defines.
DEF_QUALIFIED = re.compile(r"^[A-Za-z_][^\n]*?\b(\w+)::(\w+)\s*\(", re.M)
DEF_FREE = re.compile(r"^(?:void|bool|int|float|double|auto|std::\w+[^\n]*?)\s+(\w+)\s*\(", re.M)

# Names too generic to attribute to one file.
NOISE = {"if", "for", "while", "switch", "return", "sizeof", "std", "lv", "operator", "spdlog"}


def listed_sources(root):
    path = os.path.join(root, APP_SRCS)
    with open(path) as fh:
        return [
            line.strip()
            for line in fh
            if line.strip() and not line.startswith("#") and line.strip().endswith((".cpp", ".c"))
        ]


def stub_symbols(text, feature):
    """Names defined inside the !feature arm."""
    m = GATE_ELSE.search(text)
    if not m or m.group(1) != feature:
        return set()
    arm = text[m.end() :]
    end = arm.rfind("#endif")
    if end != -1:
        arm = arm[:end]
    names = {c for c, _ in DEF_QUALIFIED.findall(arm)}
    names |= set(DEF_FREE.findall(arm))
    return {n for n in names if n not in NOISE and len(n) > 3}


def main():
    root = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
    os.chdir(root)
    listed = listed_sources(root)
    listed_set = set(listed)

    gated = []
    for path in sorted(glob.glob("src/**/*.cpp", recursive=True)):
        text = open(path, encoding="utf-8", errors="replace").read()
        open_m = GATE_OPEN.search(text)
        if not open_m or not GATE_ELSE.search(text):
            continue
        gated.append((path, open_m.group(1), text))

    if not gated:
        print("no self-stubbing HELIX_HAS_* files found; this gate would pass vacuously")
        return 1

    # Read every listed TU once.
    bodies = {}
    for rel in listed:
        if os.path.exists(rel):
            bodies[rel] = open(rel, encoding="utf-8", errors="replace").read()

    failures = []
    for path, feature, text in gated:
        if path in listed_set:
            continue
        symbols = stub_symbols(text, feature)
        if not symbols:
            continue
        for rel, body in bodies.items():
            hit = sorted(s for s in symbols if re.search(r"\b%s\b" % re.escape(s), body))
            if hit:
                failures.append((path, feature, rel, hit[:4]))
                break

    print(
        "esp32 gated stubs: %d gated file(s), %d listed in app_srcs.txt"
        % (len(gated), sum(1 for p, _, _ in gated if p in listed_set))
    )
    if failures:
        print("\nThese files supply stubs that the ESP32 build needs but never compiles:")
        for path, feature, caller, hit in failures:
            print("  %s (%s=0 arm)" % (path, feature))
            print("    %s names %s" % (caller, ", ".join(hit)))
        print("\nAdd the file to %s; its stub arm is what makes the link resolve." % APP_SRCS)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
