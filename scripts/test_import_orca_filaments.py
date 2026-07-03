# SPDX-License-Identifier: GPL-3.0-or-later

import json
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


def test_build_catalog_unions_orca_and_seed():
    seed = [{"id": "creality-hyper-pla", "brand": "Creality", "name": "Hyper PLA",
             "type": "PLA", "nozzle": 215, "codes": {"cfs": "01001"}, "source": "cfs-seed"}]
    cat = imp.build_catalog(FIX, seed, type_ranges={"ABS": (245, 265)}, library_marker="")
    ids = {p["id"] for p in cat}
    assert "creality-hyper-pla" in ids                 # seed preserved
    assert any(p["brand"] == "Polymaker" for p in cat)  # orca product present
    assert all("P100" not in p["id"] for p in cat)      # no placeholders


def test_build_catalog_is_deterministic():
    seed = []
    a = imp.build_catalog(FIX, seed, type_ranges={"ABS": (245, 265)}, library_marker="")
    b = imp.build_catalog(FIX, seed, type_ranges={"ABS": (245, 265)}, library_marker="")
    assert json.dumps(a) == json.dumps(b)               # stable order + shape


def test_slug_preserves_plus_suffix():
    # "PLA+" must NOT collapse to the same slug as "PLA" — distinct products.
    assert imp._slug("Generic", "PLA+") != imp._slug("Generic", "PLA")
    assert imp._slug("Generic", "PLA+") == "generic-pla-plus"
    assert imp._slug("Generic", "PLA") == "generic-pla"


def test_dedupe_ids_disambiguates_collisions():
    products = [
        {"id": "acme-widget", "brand": "Acme", "name": "Widget A", "source": "orca"},
        {"id": "acme-widget", "brand": "Acme", "name": "Widget B", "source": "cfs-seed"},
        {"id": "acme-widget", "brand": "Acme", "name": "Widget C", "source": "cfs-seed"},
    ]
    imp._dedupe_ids(products)
    ids = [p["id"] for p in products]
    assert len(ids) == len(set(ids))                    # every id now unique
    assert ids == ["acme-widget", "acme-widget-2", "acme-widget-3"]


def test_build_catalog_guards_against_id_collisions():
    # Craft a seed entry that intentionally collides with an Orca-derived id
    # (Polymaker ABS Pro @System resolves to brand "Polymaker", name "ABS Pro").
    colliding_id = imp._slug("Polymaker", "ABS Pro")
    seed = [{"id": colliding_id, "brand": "Polymaker", "name": "ABS Pro (seed)",
             "type": "ABS", "nozzle": 260, "source": "cfs-seed"}]
    cat = imp.build_catalog(FIX, seed, type_ranges={"ABS": (245, 265)}, library_marker="")
    ids = [p["id"] for p in cat]
    assert len(ids) == len(set(ids))                    # no collision survives
    assert colliding_id in ids
    assert f"{colliding_id}-2" in ids
