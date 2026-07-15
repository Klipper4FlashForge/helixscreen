#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""
Stage the ESP32 LittleFS "storage" partition image contents.

Assembles a minified copy of ui_xml/, the config JSON assets, and (if present)
Task 3's printer images into a staging directory, prints a per-class size
table, and fails if the total exceeds the storage partition budget.

Usage:
    python3 scripts/esp32_stage_assets.py [--out DIR]

Then build the firmware normally (idf.py build picks up the staging dir via
littlefs_create_partition_image — see main/CMakeLists.txt). This script does
NOT flash anything; `idf.py build` bakes the staged tree into the storage.bin
image, and a normal `idf.py flash` (or `idf.py littlefs-flash`) writes it.

Sizing model: LittleFS allocates whole erase blocks, so a directory of many
small files uses far more flash than the sum of their byte counts (measured:
308 ui_xml component files summing to 1.04MB of text used 1.76MB on a real
image — 65% overhead from block rounding alone). All budget accounting below
is done in block-rounded ("on-flash") bytes, not raw byte counts, using the
same --block-size=4096 the build passes to littlefs-python (see
managed_components/joltwallet__littlefs/project_include.cmake). A fixed
metadata reserve on top covers directory-metadata growth as more files are
added (measured 2-3 blocks of drift between the naive per-file block-rounding
sum and the real packer's output when populating ui_xml/translations/).
"""

import argparse
import re
import shutil
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_OUT = REPO_ROOT / "firmware" / "helixscreen-esp32" / "build" / "littlefs_staging"

# Hard cap: the `storage` partition is 0x3c0000 bytes (partitions.csv).
STORAGE_PARTITION_BYTES = 3_932_160

# Must match --block-size passed to littlefs-python by
# littlefs_create_partition_image() (project_include.cmake) — every file
# consumes a whole number of these regardless of its actual size.
BLOCK_SIZE = 4096

# Fixed reserve subtracted from the partition budget before deciding what
# fits: covers directory-metadata blocks the naive per-file block-rounding
# sum doesn't model (see module docstring). 16 blocks = 64KB.
METADATA_RESERVE_BYTES = 16 * BLOCK_SIZE

EFFECTIVE_BUDGET_BYTES = STORAGE_PARTITION_BYTES - METADATA_RESERVE_BYTES

# ui_xml subtrees/files excluded from staging.
EXCLUDED_XML_DIRS = ("micro",)
EXCLUDED_XML_FILES = ("translations.xml",)  # merged file; per-language files ship instead


def on_flash_bytes(size: int) -> int:
    """Round a file's byte size up to the block size LittleFS actually
    allocates for it. This — not the raw byte count — is what determines
    whether a set of files fits in the partition."""
    return ((size + BLOCK_SIZE - 1) // BLOCK_SIZE) * BLOCK_SIZE


def strip_xml_comments(text: str) -> str:
    """Remove <!-- ... --> comments (including multiline)."""
    return re.sub(r"<!--.*?-->", "", text, flags=re.DOTALL)


def collapse_inter_tag_whitespace(text: str) -> str:
    """Collapse whitespace-only runs between a tag's '>' and the next '<'.

    Only matches spans that are *entirely* whitespace, so real text content
    (which sits in the same '>' ... '<' position for a non-empty element) is
    never touched, and attribute values (inside a single tag, between '<' and
    its own '>') are structurally outside this pattern and untouched too.
    """
    return re.sub(r">\s+<", "><", text)


def minify_xml(text: str) -> str:
    """Conservative XML minifier: strip comments, collapse inter-tag whitespace.

    Byte-preserves text content and attribute values. When a transform can't
    prove it's formatting-only whitespace, it doesn't touch the bytes.
    """
    return collapse_inter_tag_whitespace(strip_xml_comments(text))


def format_bytes(n: int) -> str:
    return f"{n:,} B ({n / 1024:.1f} KiB)"


def stage_ui_xml(ui_xml_dir: Path, out_dir: Path) -> int:
    """Minify and copy ui_xml/, excluding micro/ and the merged translations.xml.
    Returns total on-flash bytes written (translations/ handled separately by
    stage_translations, not counted here)."""
    total = 0
    for src in sorted(ui_xml_dir.rglob("*.xml")):
        rel = src.relative_to(ui_xml_dir)
        if rel.parts and rel.parts[0] in EXCLUDED_XML_DIRS:
            continue
        if rel.parts and rel.parts[0] == "translations":
            continue  # handled by stage_translations
        if src.name in EXCLUDED_XML_FILES:
            continue
        text = src.read_text(encoding="utf-8")
        minified = minify_xml(text)
        dest = out_dir / "ui_xml" / rel
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_text(minified, encoding="utf-8")
        total += on_flash_bytes(len(minified.encode("utf-8")))
    return total


def stage_translations(ui_xml_dir: Path, out_dir: Path, remaining_budget: int) -> tuple[int, list[str]]:
    """Minify per-language translation files: `en` always, then the rest
    largest-first while each still fits in the remaining on-flash budget
    (first-fit decreasing — a smaller language later in the list may still
    fit after a larger one didn't). Returns (on-flash bytes written, included
    language codes)."""
    translations_dir = ui_xml_dir / "translations"
    if not translations_dir.is_dir():
        return 0, []

    candidates = {}
    for src in sorted(translations_dir.glob("*.xml")):
        if src.name in EXCLUDED_XML_FILES:
            continue
        text = src.read_text(encoding="utf-8")
        minified = minify_xml(text)
        candidates[src.stem] = minified

    included: list[str] = []
    total = 0

    def add(lang: str) -> bool:
        nonlocal total
        minified = candidates[lang]
        size = on_flash_bytes(len(minified.encode("utf-8")))
        if total + size > remaining_budget:
            return False
        dest = out_dir / "ui_xml" / "translations" / f"{lang}.xml"
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_text(minified, encoding="utf-8")
        total += size
        included.append(lang)
        return True

    if "en" in candidates:
        add("en")

    others = sorted(
        (lang for lang in candidates if lang != "en"),
        key=lambda lang: len(candidates[lang].encode("utf-8")),
        reverse=True,
    )
    for lang in others:
        add(lang)

    return total, included


def stage_config(assets_dir: Path, out_dir: Path) -> int:
    """Copy assets/config/{printer_database.json, printing_tips.json, themes/}.
    Returns total on-flash bytes."""
    total = 0
    config_dir = assets_dir / "config"
    dest_config = out_dir / "assets" / "config"

    for name in ("printer_database.json", "printing_tips.json"):
        src = config_dir / name
        if not src.is_file():
            continue
        dest_config.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, dest_config / name)
        total += on_flash_bytes(src.stat().st_size)

    themes_src = config_dir / "themes"
    if themes_src.is_dir():
        themes_dest = dest_config / "themes"
        shutil.copytree(themes_src, themes_dest, dirs_exist_ok=True)
        total += sum(on_flash_bytes(f.stat().st_size) for f in themes_dest.rglob("*") if f.is_file())

    return total


def stage_filaments(assets_dir: Path, out_dir: Path) -> int:
    src = assets_dir / "filaments.json"
    if not src.is_file():
        return 0
    dest = out_dir / "assets" / "filaments.json"
    dest.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(src, dest)
    return on_flash_bytes(src.stat().st_size)


def stage_printer_images(repo_root: Path, out_dir: Path) -> tuple[int, bool]:
    """Copy build/esp32_printer_images/ if Task 3 has produced it. Returns
    (on-flash bytes copied, whether the source dir existed)."""
    src = repo_root / "build" / "esp32_printer_images"
    if not src.is_dir():
        return 0, False
    dest = out_dir / "printer_images"
    shutil.copytree(src, dest, dirs_exist_ok=True)
    total = sum(on_flash_bytes(f.stat().st_size) for f in dest.rglob("*") if f.is_file())
    return total, True


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, default=DEFAULT_OUT,
                        help=f"staging output directory (default: {DEFAULT_OUT})")
    args = parser.parse_args()

    out_dir: Path = args.out
    if out_dir.exists():
        shutil.rmtree(out_dir)
    out_dir.mkdir(parents=True)

    ui_xml_dir = REPO_ROOT / "ui_xml"
    assets_dir = REPO_ROOT / "assets"

    ui_xml_bytes = stage_ui_xml(ui_xml_dir, out_dir)
    # ui_xml on-flash bytes staged; translations get whatever room is left
    # under the (metadata-reserved) effective budget. config/filaments/images
    # are small, fixed-size classes so they're staged unconditionally.
    config_bytes = stage_config(assets_dir, out_dir)
    filaments_bytes = stage_filaments(assets_dir, out_dir)
    printer_images_bytes, printer_images_present = stage_printer_images(REPO_ROOT, out_dir)

    fixed_total = ui_xml_bytes + config_bytes + filaments_bytes + printer_images_bytes
    remaining_budget = EFFECTIVE_BUDGET_BYTES - fixed_total
    translations_bytes, included_langs = stage_translations(ui_xml_dir, out_dir, remaining_budget)

    total = fixed_total + translations_bytes

    print("ESP32 LittleFS storage staging (on-flash, block-rounded bytes):")
    print(f"  ui_xml (minified, excl. micro/ + translations.xml): {format_bytes(ui_xml_bytes)}")
    print(f"  translations ({', '.join(included_langs) if included_langs else 'none'}): "
          f"{format_bytes(translations_bytes)}")
    print(f"  assets/config (printer_database, printing_tips, themes): {format_bytes(config_bytes)}")
    print(f"  assets/filaments.json: {format_bytes(filaments_bytes)}")
    if printer_images_present:
        print(f"  printer_images (build/esp32_printer_images/): {format_bytes(printer_images_bytes)}")
    else:
        print("  printer_images: SKIPPED (build/esp32_printer_images/ not present — Task 3)")
    print(f"  TOTAL: {format_bytes(total)} / effective budget {format_bytes(EFFECTIVE_BUDGET_BYTES)} "
          f"(partition {format_bytes(STORAGE_PARTITION_BYTES)} minus "
          f"{format_bytes(METADATA_RESERVE_BYTES)} metadata reserve)")
    print(f"  output: {out_dir}")

    if total > EFFECTIVE_BUDGET_BYTES:
        print(f"FAIL: staged on-flash size {total} exceeds effective storage partition budget "
              f"{EFFECTIVE_BUDGET_BYTES}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
