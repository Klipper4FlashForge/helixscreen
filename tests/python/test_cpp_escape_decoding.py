# SPDX-License-Identifier: GPL-3.0-or-later
"""C hex-escape decoding in extracted translation keys.

A C++ source literal is what the extractor sees, but the runtime lookup key is
what the *compiler* produces. `"\\xc2\\xb0"` in source is two raw bytes that
together form U+00B0, so the extracted key must be the degree sign, not the
eight characters of the escape sequence.
"""

import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT / "scripts"))

from translations.extractor import (  # noqa: E402
    decode_c_escapes,
    extract_strings_from_cpp,
)


# --- decode_c_escapes() ------------------------------------------------------


def test_hex_escape_lowercase_decodes_to_degree_sign():
    assert decode_c_escapes(r"%d\xc2\xb0") == "%d°"


def test_hex_escape_uppercase_decodes_to_degree_sign():
    assert decode_c_escapes(r"%d\xC2\xB0C") == "%d°C"


def test_three_byte_sequence_decodes_to_em_dash():
    assert decode_c_escapes(r"a \xe2\x80\x94 b") == "a — b"


def test_mixed_case_within_one_sequence():
    assert decode_c_escapes(r"\xE2\x80\x94") == "—"


def test_newline_escape_is_left_alone():
    # The XML pack stores keys as attribute values, where XML attribute-value
    # normalization collapses a real newline to a space. Keys keep the literal
    # two-character \n so they survive the round trip.
    assert decode_c_escapes(r"Line one\nLine two") == r"Line one\nLine two"


def test_other_c_escapes_are_left_alone():
    assert decode_c_escapes(r'tap \"Check Again\"') == r'tap \"Check Again\"'
    assert decode_c_escapes(r"a\tb") == r"a\tb"


def test_escaped_backslash_does_not_start_a_hex_escape():
    # \\ is a literal backslash; the following xc2 is plain text, not an escape.
    assert decode_c_escapes(r"\\xc2") == r"\\xc2"


def test_plain_string_is_unchanged():
    assert decode_c_escapes("Heating to 200C") == "Heating to 200C"


def test_already_utf8_text_is_unchanged():
    assert decode_c_escapes("Heating to 200°C") == "Heating to 200°C"


def test_truncated_utf8_sequence_returns_input_unchanged():
    # \xc2 alone is not a complete UTF-8 character. Decoding must not raise and
    # must not substitute a replacement character; the raw literal is returned
    # so the corruption stays visible to the translation gates.
    raw = r"bad \xc2 tail"
    assert decode_c_escapes(raw) == raw


def test_hex_escape_with_no_digits_returns_input_unchanged():
    raw = r"trailing \x"
    assert decode_c_escapes(raw) == raw


def test_lone_trailing_backslash_is_preserved():
    assert decode_c_escapes("ends with \\") == "ends with \\"


def test_latin1_codepoint_decoding_is_not_used():
    # codecs.decode(s, "unicode_escape") maps \xc2 -> U+00C2, which is the wrong
    # answer. Guard against a future rewrite reaching for it.
    assert decode_c_escapes(r"\xc2\xb0") != "Â°"


# --- end-to-end through the C++ extractor ------------------------------------


def _extract(tmp_path, source: str):
    f = tmp_path / "sample.cpp"
    f.write_text(source, encoding="utf-8")
    return extract_strings_from_cpp(f)


def test_lv_tr_hex_escape_key_matches_compiler_output(tmp_path):
    src = 'snprintf(buf, sizeof(buf), lv_tr("Heating to %d\\xC2\\xB0C... %.0f\\xC2\\xB0C"), a, b);'
    assert "Heating to %d°C... %.0f°C" in _extract(tmp_path, src)


def test_lv_tr_adjacent_literals_join_then_decode(tmp_path):
    # The real call site splits the literal so the hex escape cannot swallow the
    # following 'C' as a third hex digit.
    src = (
        "snprintf(buf, sizeof(buf),\n"
        '         lv_tr("Heating to %d\\xC2\\xB0"\n'
        '               "C... %.0f\\xC2\\xB0"\n'
        '               "C"),\n'
        "         a, b);\n"
    )
    assert "Heating to %d°C... %.0f°C" in _extract(tmp_path, src)


def test_lv_tr_em_dash_key_matches_compiler_output(tmp_path):
    src = (
        "snprintf(step_text, sizeof(step_text),\n"
        '         lv_tr("Touch the target (point %1$d of 3) \\xe2\\x80\\x94 touch %2$d of %3$d"),\n'
        "         a, b, c);\n"
    )
    expected = "Touch the target (point %1$d of 3) — touch %2$d of %3$d"
    assert expected in _extract(tmp_path, src)


def test_lv_tr_rotation_key_matches_compiler_output(tmp_path):
    src = 'lv_tr("Testing rotation: %d\\xc2\\xb0 (%d/%d) - %ds remaining")'
    assert "Testing rotation: %d° (%d/%d) - %ds remaining" in _extract(tmp_path, src)


def test_extracted_lv_tr_keys_never_contain_a_raw_hex_escape(tmp_path):
    src = (
        'lv_tr("Testing rotation: %d\\xc2\\xb0 (%d/%d) - %ds remaining");\n'
        'lv_tr("Heating to %d\\xC2\\xB0" "C");\n'
    )
    for key in _extract(tmp_path, src):
        assert "\\x" not in key, key


def test_lv_tr_newline_key_is_not_rewritten(tmp_path):
    src = 'set_status(lv_tr("Moonraker restarting...\\nWaiting for reconnection..."));'
    assert r"Moonraker restarting...\nWaiting for reconnection..." in _extract(tmp_path, src)


# --- the raw-hex-escape regression guard -------------------------------------


def _load_gate():
    import importlib.util

    path = REPO_ROOT / "scripts" / "check_translation_format_specifiers.py"
    spec = importlib.util.spec_from_file_location("_fmt_gate", path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def _write_locale(tmp_path, locale: str, translations: dict):
    import yaml

    body = {"locale": locale, "translations": translations}
    (tmp_path / f"{locale}.yml").write_text(
        yaml.safe_dump(body, allow_unicode=True), encoding="utf-8"
    )


def test_gate_flags_a_key_holding_a_raw_hex_escape(tmp_path, monkeypatch):
    gate = _load_gate()
    monkeypatch.setattr(gate, "TRANS_DIR", tmp_path)
    _write_locale(tmp_path, "de", {r"Rotation: %d\xc2\xb0": "Drehung: %d°"})
    problems = gate.check_raw_hex_escapes()
    assert [(loc, kind) for loc, kind, _ in problems] == [("de", "key")]


def test_gate_flags_a_value_holding_a_raw_hex_escape(tmp_path, monkeypatch):
    gate = _load_gate()
    monkeypatch.setattr(gate, "TRANS_DIR", tmp_path)
    _write_locale(tmp_path, "fr", {"Rotation: %d°": r"Rotation : %d\xc2\xb0"})
    problems = gate.check_raw_hex_escapes()
    assert [(loc, kind) for loc, kind, _ in problems] == [("fr", "value")]


def test_gate_accepts_resolved_keys(tmp_path, monkeypatch):
    gate = _load_gate()
    monkeypatch.setattr(gate, "TRANS_DIR", tmp_path)
    _write_locale(
        tmp_path,
        "es",
        {
            "Rotation: %d°": "Rotación: %d°",
            r"Line one\nLine two": r"Linea uno\nLinea dos",
        },
    )
    assert gate.check_raw_hex_escapes() == []


def test_shipped_locales_carry_no_raw_hex_escapes():
    # End-to-end on the real translation set: the field bug was a key that could
    # never match its runtime lookup.
    assert _load_gate().check_raw_hex_escapes() == []
