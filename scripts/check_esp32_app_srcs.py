#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Drift gate for the ESP32 firmware app source manifest.

The firmware is a deliberately curated subset of `src/` — the "v1 Core+AMS cut"
(see firmware/helixscreen-esp32/components/helixapp/CMakeLists.txt). Whole
subsystems are gated off (camera, label printer, gcode/bed-mesh 3D, plugins,
timelapse viewer, screensaver, calibration panels, sound, mocks, the concrete
libhv client). The manifest (`app_srcs.txt`) is hand-maintained: it was
generated once from the native-audit 491-file Xtensa-compile classification and
has drifted twice since (ams_endless_spool.cpp, toolhead_homing.cpp — each
landed in main without making the manifest, and the firmware link broke ~25 min
into esp32-build CI).

Curation is unavoidable for a subset build. The fix is not to auto-glob all of
src/ (that pulls hundreds of Xtensa-incompatible files) but to make the drift
LOUD at quality-check / PR time instead of at a 25-minute link step.

WHAT IS FLAGGED
  - A `src/**/*.{cpp,c}` in NEITHER app_srcs.txt NOR app_srcs_excluded.txt
    ("undecided" — a new file, or one nobody decided on). Add it to app_srcs.txt
    (compile it on the firmware) or to the exclusion file with a reason (don't).
  - A `src/...` line in app_srcs.txt whose file no longer exists ("stale" — the
    source was deleted/renamed but the manifest line wasn't).

NOT FLAGGED
  - Manifest entries OUTSIDE src/ (e.g. lib/lv_markdown/*.c). The universe is
    src/ only; the manifest legitimately pulls a few lib/ sources.
  - Anything not under src/.

EXCLUSION FILE FORMAT (app_srcs_excluded.txt)
  - One path per line; `#`-prefixed lines and trailing `# reason` are ignored.
  - A path ending in `/` excludes a whole directory recursively.
  - Any other path excludes that single file.

MODES
  (default)          check; exit 0 if no drift, 1 if undecided/stale.
  --list             print the undecided files, one per line.
  --summary          one-line counts (undecided / stale / totals).
  --write-exclusions regenerate app_srcs_excluded.txt from the current undecided
                     set, compressing whole directories to dir-level entries.
                     Use once to seed, then hand-annotate reasons. Refuses to
                     overwrite an existing file unless --force.

Exit 0 when the manifest and exclusion baseline cover src/ exactly, 1 otherwise.
"""

from __future__ import annotations

import argparse
import sys
from collections import defaultdict
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_MANIFEST = REPO_ROOT / "firmware/helixscreen-esp32/components/helixapp/app_srcs.txt"
DEFAULT_EXCLUSIONS = REPO_ROOT / "firmware/helixscreen-esp32/components/helixapp/app_srcs_excluded.txt"
DEFAULT_SRC_ROOT = REPO_ROOT / "src"
SRC_SUFFIXES = (".cpp", ".c")


def load_manifest(path: Path) -> set[str]:
    """Paths from the manifest (stripped of comments). May include non-src/ entries."""
    included: set[str] = set()
    if not path.exists():
        return included
    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.split("#", 1)[0].strip()
        if line.endswith(SRC_SUFFIXES):
            included.add(line)
    return included


def load_exclusions(path: Path) -> tuple[set[str], set[str]]:
    """Return (file_exclusions, dir_exclusions) from the baseline file."""
    files: set[str] = set()
    dirs: set[str] = set()
    if not path.exists():
        return files, dirs
    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.split("#", 1)[0].strip()
        if not line:
            continue
        if line.endswith("/"):
            dirs.add(line)
        else:
            files.add(line)
    return files, dirs


def scan_universe(src_root: Path) -> set[str]:
    """All src/**/*.{cpp,c} as forward-slash paths relative to src_root's parent.

    Relative to the PARENT (not REPO_ROOT) so the paths read ``src/...`` for both
    the real tree (src_root = <repo>/src, parent = <repo>) and an external
    fixture (src_root = <tmp>/src, parent = <tmp>) — matching how manifest and
    exclusion entries are written.
    """
    base = src_root.parent
    universe: set[str] = set()
    for p in src_root.rglob("*"):
        if p.is_file() and p.suffix in SRC_SUFFIXES:
            universe.add(str(p.relative_to(base)).replace("\\", "/"))
    return universe


def display(path: Path) -> str:
    """Repo-relative if under the repo, else the path as-is (for fixtures)."""
    try:
        return str(path.relative_to(REPO_ROOT))
    except ValueError:
        return str(path)


def is_excluded(rel: str, ex_files: set[str], ex_dirs: set[str]) -> bool:
    if rel in ex_files:
        return True
    return any(rel.startswith(d) for d in ex_dirs)


def compute(
    manifest: Path, exclusions: Path, src_root: Path
) -> tuple[set[str], set[str], set[str]]:
    included = load_manifest(manifest)
    ex_files, ex_dirs = load_exclusions(exclusions)
    universe = scan_universe(src_root)
    undecided = sorted(
        f for f in universe
        if f not in included and not is_excluded(f, ex_files, ex_dirs)
    )
    # Stale = manifest src/ entries that no longer exist on disk.
    stale = sorted(i for i in included if i.startswith("src/") and i not in universe)
    return undecided, stale, universe


def write_exclusions(undecided: list[str], universe: set[str], path: Path) -> int:
    """Emit undecided as dir-compressed entries to `path`.

    A directory (at any depth) becomes a single `dir/` line when every src/ file
    beneath it is undecided; remaining files are listed individually.
    """
    undecided_set = set(undecided)

    # For each undecided file's parent chain, find the highest dir whose entire
    # src/ file-set is undecided. Prefer the SHALLOWEST whole-undecided dir so a
    # fully-excluded tree is one line, not one per subdir.
    dir_covers: dict[str, list[str]] = {}
    covered: set[str] = set()
    for f in sorted(undecided_set):
        parts = f.split("/")
        # walk parent dirs shallowest-first; pick the widest whole-undecided one
        best_dir = None
        for i in range(2, len(parts)):  # parts[0]=="src", dir needs >= "src/X/"
            d = "/".join(parts[:i]) + "/"
            files_under = {u for u in universe if u.startswith(d)}
            if files_under and files_under <= undecided_set:
                best_dir = d  # keep widening
        if best_dir:
            dir_covers.setdefault(best_dir, []).append(f)
        # else: emitted per-file below

    for files in dir_covers.values():
        covered.update(files)
    leftover = sorted(f for f in undecided_set if f not in covered)

    lines = [
        "# ESP32 firmware app_srcs exclusion baseline.",
        "#",
        "# Every src/**/*.cpp|.c NOT compiled by the firmware (i.e. not in",
        "# app_srcs.txt) must appear here, or scripts/check_esp32_app_srcs.py",
        "# fails. A path ending in '/' excludes a whole directory recursively.",
        "# Add a trailing '# reason' so the next person knows why.",
        "#",
        "# Regenerate with: python3 scripts/check_esp32_app_srcs.py --write-exclusions --force",
        "# (then re-annotate reasons by hand).",
        "",
    ]
    if dir_covers:
        lines.append("# --- whole directories excluded ---")
        for d in sorted(dir_covers):
            lines.append(f"{d}  # all {len(dir_covers[d])} src/ files beneath")
        lines.append("")
    if leftover:
        lines.append("# --- individual files (same dir, partially compiled) ---")
        for f in leftover:
            lines.append(f"{f}  # not in the v1 Core+AMS cut")
        lines.append("")

    path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    n = len(dir_covers) + len(leftover)
    print(f"Wrote {display(path)}: {n} entries "
          f"({len(dir_covers)} dir-level, {len(leftover)} file-level) "
          f"covering {len(undecided_set)} src/ files.")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    ap.add_argument("--manifest", type=Path, default=DEFAULT_MANIFEST)
    ap.add_argument("--exclusions", type=Path, default=DEFAULT_EXCLUSIONS)
    ap.add_argument("--src-root", type=Path, default=DEFAULT_SRC_ROOT)
    ap.add_argument("--list", action="store_true", help="print undecided files")
    ap.add_argument("--summary", action="store_true", help="one-line counts")
    ap.add_argument("--write-exclusions", action="store_true",
                    help="regenerate the exclusion baseline from current drift")
    ap.add_argument("--force", action="store_true",
                    help="with --write-exclusions: overwrite an existing baseline")
    args = ap.parse_args()

    undecided, stale, universe = compute(args.manifest, args.exclusions, args.src_root)

    if args.write_exclusions:
        if args.exclusions.exists() and not args.force:
            print(f"FAIL: {display(args.exclusions)} already exists — "
                  "use --force to overwrite (you will lose hand-written reasons).",
                  file=sys.stderr)
            return 1
        return write_exclusions(undecided, universe, args.exclusions)

    if args.summary:
        print(f"esp32 app_srcs: {len(undecided)} undecided, {len(stale)} stale, "
              f"{len(universe)} src files total.")
        return 1 if (undecided or stale) else 0

    if undecided or stale:
        if undecided:
            print(f"FAIL: {len(undecided)} src/ file(s) not decided for the ESP32 firmware build.\n"
                  f"      Each must be in app_srcs.txt (compile it) or app_srcs_excluded.txt (don't, with a reason):",
                  file=sys.stderr)
            for f in undecided:
                print(f"        {f}", file=sys.stderr)
            print("\n      Add to the manifest if the firmware should link it, or to the exclusion"
                  "\n      baseline with a reason if it should not. Seed the baseline in one step with"
                  "\n        python3 scripts/check_esp32_app_srcs.py --write-exclusions",
                  file=sys.stderr)
        if stale:
            print(f"FAIL: {len(stale)} stale manifest line(s) — src/ files that no longer exist:",
                  file=sys.stderr)
            for f in stale:
                print(f"        {f}", file=sys.stderr)
            print("\n      Remove the stale line(s) from app_srcs.txt.", file=sys.stderr)
        if args.list:
            print()
            for f in undecided:
                print(f)
        return 1

    print(f"OK: every src/**/*.{{cpp,c}} ({len(universe)}) is in the manifest or exclusion baseline.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
