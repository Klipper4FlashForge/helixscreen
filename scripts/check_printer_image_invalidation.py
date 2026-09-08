#!/usr/bin/env python3
# Copyright (C) 2025-2026 356C LLC
# SPDX-License-Identifier: GPL-3.0-or-later
"""Check that no UI code deletes a printer image cache.

`invalidate_printer_image_cache(path)` removes every generated scaled `.bin` for
that source image, and each one costs a decode-and-resize plus a flash write to
rebuild. `PrinterImageWidget::refresh_printer_image()` and
`PrinterManagerOverlay::refresh_printer_info()` re-resolve the active image on
every activation, so a call from either destroys entries they are about to need.

A refresh has nothing stale to drop. Cache entries are named with the source's
mtime and size, so an image rewritten in place resolves to a different entry and
the one holding the old pixels is never consulted again.

WHO MAY CALL IT
  Only `src/system/`. `PrinterImageManager::import_image()` owns the rewrite, and
  clears the entries that can no longer be named rather than leaving them for the
  pruner. The definition itself also lives there.

WHY A LINT AND NOT A UNIT TEST
  The helper's own test cannot see a UI call site that reintroduces the call, and
  either UI caller doing so survives the whole suite. Only reading the call sites
  catches that.

Exit 0 when no UI caller invalidates, 1 otherwise.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent

# Subdirectories of the scanned tree that own printer image invalidation.
# Named relative to the scan root so a fixture tree under a different name still
# resolves.
ALLOWED_SUBDIRS = ("system",)

# The call itself, not a longer identifier that merely starts with the same name.
CALL_RE = re.compile(r"\binvalidate_printer_image_cache\s*\(")

# Line comments and the bodies of block comments; enough to keep a doc comment
# that names the function from reading as a call site.
LINE_COMMENT_RE = re.compile(r"//.*$", re.M)
BLOCK_COMMENT_RE = re.compile(r"/\*.*?\*/", re.S)


def strip_comments(text: str) -> str:
    """Blank out comments, preserving newlines so line numbers still line up."""

    def blank(match: re.Match[str]) -> str:
        return re.sub(r"[^\n]", " ", match.group(0))

    return LINE_COMMENT_RE.sub(blank, BLOCK_COMMENT_RE.sub(blank, text))


def find_violations(src_root: Path) -> list[tuple[str, int, str]]:
    out: list[tuple[str, int, str]] = []
    for path in sorted(src_root.rglob("*.cpp")) + sorted(src_root.rglob("*.h")):
        inner = path.relative_to(src_root)
        if inner.parts[0] in ALLOWED_SUBDIRS:
            continue
        rel = (Path(src_root.name) / inner).as_posix()
        try:
            text = path.read_text(encoding="utf-8", errors="replace")
        except OSError:
            continue
        if "invalidate_printer_image_cache" not in text:
            continue
        lines = strip_comments(text).splitlines()
        for i, line in enumerate(lines, start=1):
            if CALL_RE.search(line):
                out.append((rel, i, line.strip()))
    return out


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--src", default=str(REPO_ROOT / "src"),
                    help="tree to scan (fixture override for the gate's own tests)")
    args = ap.parse_args()

    violations = find_violations(Path(args.src).resolve())
    if not violations:
        print("✓ printer image invalidation: no UI caller deletes a printer image cache")
        return 0

    print("UI code must not delete a printer image cache.")
    print()
    for rel, lineno, text in violations:
        print(f"  {rel}:{lineno}")
        print(f"    {text}")
    print()
    print(f"{len(violations)} call(s) outside {chr(44).join(ALLOWED_SUBDIRS)}/.")
    print()
    print("Cache entries are named with the source's mtime and size, so a refresh")
    print("that re-resolves the same image has nothing stale to drop, and every")
    print("entry deleted costs a decode-and-resize and a flash write to rebuild.")
    print()
    print("Drop the call: src/system/ owns printer image invalidation.")
    return 1


if __name__ == "__main__":
    sys.exit(main())
