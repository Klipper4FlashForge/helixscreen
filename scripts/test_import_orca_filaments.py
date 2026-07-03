# SPDX-License-Identifier: GPL-3.0-or-later

import os
import import_orca_filaments as imp

FIX = os.path.join(os.path.dirname(__file__), "fixtures", "orca")


def load_fixtures():
    return imp.load_profiles(FIX)


def test_resolve_inherits_flattens_chain():
    by_name = load_fixtures()
    leaf = by_name["Polymaker ABS Pro @System"]
    r = imp.resolve_inherits(leaf, by_name)
    assert imp.first_scalar(r["filament_type"]) == "ABS"          # from fdm_filament_abs
    assert imp.first_scalar(r["filament_vendor"]) == "Polymaker"  # from @base
    assert imp.first_scalar(r["nozzle_temperature"]) == "280"     # @base overrides


def test_first_scalar_handles_nil_and_arrays():
    assert imp.first_scalar(["220", "220"]) == "220"
    assert imp.first_scalar(["nil"]) is None
    assert imp.first_scalar([]) is None


def test_map_type_known_and_unknown():
    assert imp.map_type("PLA") == "PLA"
    assert imp.map_type("PET-CF") == "PET-CF"
    assert imp.map_type("WeirdNew") == "WeirdNew"  # unknown → raw fallback


def test_collapse_bed_prefers_textured_skips_nil():
    r = imp.resolve_inherits(load_fixtures()["Polymaker ABS Pro @System"], load_fixtures())
    assert imp.collapse_bed(r) == 105   # textured wins
    common = {"cool_plate_temp": ["55"], "hot_plate_temp": ["nil"], "textured_plate_temp": ["nil"]}
    assert imp.collapse_bed(common) == 55  # falls through to cool


def test_build_product_thin_when_range_matches_type():
    r = imp.resolve_inherits(load_fixtures()["Polymaker ABS Pro @System"], load_fixtures())
    p = imp.build_product(r, base_type_range=(245, 265))
    assert p["brand"] == "Polymaker"
    assert p["type"] == "ABS"
    assert p["nozzle"] == 280
    assert p["nozzle_min"] == 270 and p["nozzle_max"] == 290  # differs from type → emitted
    assert p["bed"] == 105
    assert p["orca_id"] == "OGFPMABSPRO"
    assert p["source"] == "orca"
