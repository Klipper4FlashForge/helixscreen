#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Derive HelixScreen's filaments.json from the OrcaSlicer filament library.

Facts only (temps/densities); no OrcaSlicer profile files are copied or shipped.
"""
import json
import os
import re

# Orca filament_type (open enum) -> our filament_database.h type name.
# Only entries that differ or need pinning are listed; unknown types fall back
# to the raw Orca string (still usable via the product's own temps).
TYPE_MAP = {
    "PLA": "PLA", "PLA-CF": "PLA-CF", "PETG": "PETG", "PETG-CF": "PETG-CF",
    "PET-CF": "PET-CF", "PET-GF": "PET-GF", "ABS": "ABS", "ABS-CF": "ABS-CF",
    "ASA": "ASA", "PC": "PC", "PA": "PA", "PA-CF": "PA-CF", "PA6": "PA6",
    "TPU": "TPU", "PVA": "PVA", "HIPS": "HIPS", "PPS-CF": "PPS-CF",
}

BED_PRIORITY = ("textured_plate_temp", "hot_plate_temp", "cool_plate_temp")


def load_profiles(root: str) -> dict:
    """Map every profile's `name` -> profile dict, across the whole tree."""
    by_name = {}
    for dirpath, _dirs, files in os.walk(root):
        for fn in files:
            if not fn.endswith(".json"):
                continue
            with open(os.path.join(dirpath, fn), encoding="utf-8") as f:
                p = json.load(f)
            if p.get("type") == "filament" and "name" in p:
                by_name[p["name"]] = p
    return by_name


def resolve_inherits(profile: dict, by_name: dict) -> dict:
    """Flatten the inherits chain: parent first, child overrides."""
    parent_name = profile.get("inherits")
    if parent_name and parent_name in by_name:
        merged = resolve_inherits(by_name[parent_name], by_name)
    else:
        merged = {}
    merged = dict(merged)
    for k, v in profile.items():
        if k in ("inherits", "name", "instantiation"):
            continue
        merged[k] = v
    return merged


def first_scalar(value):
    """Orca stores per-extruder string arrays; return [0] or None for nil/empty."""
    if isinstance(value, list):
        if not value:
            return None
        value = value[0]
    if value is None:
        return None
    s = str(value).strip()
    if s == "" or s.lower() == "nil":
        return None
    return s


def _as_int(value):
    s = first_scalar(value)
    if s is None:
        return None
    try:
        return int(round(float(s)))
    except ValueError:
        return None


def map_type(orca_type: str) -> str:
    return TYPE_MAP.get(orca_type, orca_type)


def collapse_bed(resolved: dict):
    """First real (non-nil, non-zero) value in textured -> hot -> cool."""
    for key in BED_PRIORITY:
        v = _as_int(resolved.get(key))
        if v:  # non-None and non-zero
            return v
    return None


def _slug(brand: str, name: str) -> str:
    raw = f"{brand}-{name}".lower()
    return re.sub(r"[^a-z0-9]+", "-", raw).strip("-")


def build_product(resolved: dict, base_type_range):
    """Turn a resolved Orca profile into a filaments.json product dict."""
    orca_type = first_scalar(resolved.get("filament_type")) or ""
    brand = first_scalar(resolved.get("filament_vendor")) or "Generic"
    orca_id = resolved.get("filament_id", "")  # top-level bare string
    name = first_scalar(resolved.get("_product_name")) or orca_id or "Unknown"

    nozzle = _as_int(resolved.get("nozzle_temperature"))
    nmin = _as_int(resolved.get("nozzle_temperature_range_low"))
    nmax = _as_int(resolved.get("nozzle_temperature_range_high"))

    product = {
        "id": _slug(brand, name),
        "brand": brand,
        "name": name,
        "type": map_type(orca_type),
        "nozzle": nozzle,
        "bed": collapse_bed(resolved),
        "source": "orca",
    }
    # Emit explicit range only when it differs from the base type's range.
    if nmin is not None and nmax is not None and (nmin, nmax) != base_type_range:
        product["nozzle_min"] = nmin
        product["nozzle_max"] = nmax
    density = resolved.get("filament_density")
    dval = first_scalar(density)
    if dval and float(dval) > 0:
        product["density"] = round(float(dval), 3)
    if orca_id:
        product["orca_id"] = orca_id
    return product
