#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""One-shot migration of on_deactivate() overrides to the on_deactivating() hook.

prestonbrown/helixscreen#1516. Point-in-time scaffolding: it exists so the ~60-site
conversion is reviewable as a rule rather than as sixty hand edits, and is deleted
with the rest of this plan once the change lands.

Rewrites, across src/ui, include and tests:

  header   void on_deactivate() override;          ->  void on_deactivating(DeactivateReason reason) override;
  source   void X::on_deactivate() {               ->  void X::on_deactivating(DeactivateReason) {
  body     OverlayBase::on_deactivate();           ->  (deleted, with its "call base" comment)
  callers  view.on_deactivate();                   ->  view.on_deactivate(DeactivateReason::NavigateAway);

Skipped: the PanelWidget hierarchy, which has its own unrelated on_deactivate() hook,
and the files whose deactivation logic is converted by hand (the reason switch).

Usage: python3 docs/devel/plans/1516-migrate-on-deactivate.py [--check]
"""

import argparse
import pathlib
import re
import sys

# PanelWidget is a separate hierarchy with its own on_deactivate(); it is not an
# IPanelLifecycle and keeps the name.
SKIP_DIRS = ("src/ui/panel_widgets/",)
SKIP_FILES = {
    "include/panel_widget.h",
    "include/panel_lifecycle.h",
    "include/overlay_base.h",
    "include/ui_panel_base.h",
    "include/temperature_service.h",
    "src/ui/overlay_base.cpp",
    "src/ui/ui_panel_base.cpp",
    "src/ui/ui_nav_manager.cpp",
    "src/ui/temperature_service.cpp",
}

HEADER_DECL = re.compile(r"^(\s*)void on_deactivate\(\) override;\s*$")
SOURCE_DEF = re.compile(r"^void (\w+)::on_deactivate\(\)\s*\{\s*$")
BASE_CALL = re.compile(r"^\s*OverlayBase::on_deactivate\(\);.*$")
# A test or peer invoking the lifecycle hook by hand.
CALLER = re.compile(r"(?<![\w:>.])((?:[\w:.]|->)+)\.on_deactivate\(\);")
ARROW_CALLER = re.compile(r"((?:[\w:.]|->)+)->on_deactivate\(\);")
# Comment lines that say only "call the base", which the hook makes untrue.
BASE_COMMENT = re.compile(r"^\s*//\s*(call(s)? )?(the )?base( class)?\.?\s*$", re.I)


def skipped(rel: str) -> bool:
    return rel in SKIP_FILES or any(rel.startswith(d) for d in SKIP_DIRS)


def calls_panel_widgets(text: str) -> bool:
    """A file that drives home widgets calls the OTHER on_deactivate().

    HomePanel forwards deactivation to its PanelWidgets, and the widget tests
    call the widget hook directly. Both spellings are `x->on_deactivate()`, so
    a file holding either is left to a hand edit rather than guessed at.
    """
    return "PanelWidget" in text or "panel_widgets/" in text


def migrate_header(lines: list[str]) -> tuple[list[str], int]:
    out, n = [], 0
    for ln in lines:
        m = HEADER_DECL.match(ln)
        if m:
            out.append(f"{m.group(1)}void on_deactivating(DeactivateReason reason) override;\n")
            n += 1
        else:
            out.append(ln)
    return out, n


def migrate_source(lines: list[str]) -> tuple[list[str], int]:
    """Rewrite each definition, dropping the base call from inside its body."""
    out: list[str] = []
    i, n = 0, 0
    while i < len(lines):
        m = SOURCE_DEF.match(lines[i])
        if not m:
            out.append(lines[i])
            i += 1
            continue

        # Collect the body by brace balance so the base call is only removed
        # from inside this function.
        depth = 0
        end = i
        for j in range(i, len(lines)):
            depth += lines[j].count("{") - lines[j].count("}")
            if depth == 0 and j > i:
                end = j
                break
        body = lines[i : end + 1]

        kept: list[str] = []
        for k, bl in enumerate(body):
            if BASE_CALL.match(bl):
                # Drop a "// Call base class" comment sitting directly above it,
                # and a blank line left stranded above that.
                while kept and BASE_COMMENT.match(kept[-1]):
                    kept.pop()
                while kept and not kept[-1].strip():
                    kept.pop()
                continue
            kept.append(bl)

        # The parameter is named only where the body reads it, so -Wunused-parameter
        # stays clean without a (void) cast in every one of these.
        inner = "".join(kept[1:-1])
        param = "DeactivateReason reason" if re.search(r"\breason\b", inner) else "DeactivateReason"
        kept[0] = f"void {m.group(1)}::on_deactivating({param}) {{\n"

        out.extend(kept)
        n += 1
        i = end + 1
    return out, n


def migrate_callers(text: str) -> tuple[str, int]:
    """Hand-written invocations (tests, peers) now have to name a reason."""
    n = 0

    def sub(m: re.Match) -> str:
        nonlocal n
        n += 1
        return f"{m.group(0)[:-len('on_deactivate();')]}on_deactivate(DeactivateReason::NavigateAway);"

    text = CALLER.sub(sub, text)
    text = ARROW_CALLER.sub(sub, text)
    return text, n


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true", help="report without writing")
    args = ap.parse_args()

    root = pathlib.Path(__file__).resolve().parents[3]
    total = 0
    for pattern in ("include/**/*.h", "src/**/*.h", "src/**/*.cpp", "tests/**/*.h", "tests/**/*.cpp"):
        for path in sorted(root.glob(pattern)):
            rel = path.relative_to(root).as_posix()
            if skipped(rel):
                continue
            original = path.read_text()
            lines = original.splitlines(keepends=True)

            lines, n_hdr = migrate_header(lines)
            lines, n_src = migrate_source(lines)
            text = "".join(lines)
            if calls_panel_widgets(original):
                n_call = 0
                if "on_deactivate();" in text:
                    print(f"{rel}: SKIPPED caller rewrite (PanelWidget hook), hand-check it")
            else:
                text, n_call = migrate_callers(text)

            n = n_hdr + n_src + n_call
            if n == 0 or text == original:
                continue
            total += n
            print(f"{rel}: {n_hdr} decl, {n_src} def, {n_call} call")
            if not args.check:
                path.write_text(text)

    print(f"total sites: {total}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
