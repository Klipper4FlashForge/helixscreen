"""Inline XML text extraction — parity with lib/helix-xml/src/xml/lv_xml.c PCDATA handling."""

import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT / "scripts"))

from translations.extractor import (  # noqa: E402
    collapse_whitespace,
    extract_strings_from_xml,
)

# Shared collapse-parity table — keep in sync with the C tests in
# tests/unit/test_xml_inline_text.cpp (inputs are post-entity-decode).
COLLAPSE_TABLE = [
    ("Hello world", "Hello world"),
    ("  Hello  world  ", "Hello world"),
    ("\n    Hello\n    world\n  ", "Hello world"),
    ("Tabs\there\tand\rthere", "Tabs here and there"),
    ("Hello\nworld", "Hello world"),
    ("   \n\t  ", ""),
    ("", ""),
]


def test_collapse_whitespace_parity_table():
    for raw, expected in COLLAPSE_TABLE:
        assert collapse_whitespace(raw) == expected, f"input: {raw!r}"


def _extract(tmp_path, xml: str):
    f = tmp_path / "sample.xml"
    f.write_text(xml, encoding="utf-8")
    return extract_strings_from_xml(f)


def test_inline_text_extracted(tmp_path):
    xml = """<component>
  <view extends="lv_obj">
    <text_muted name="msg">Print speed</text_muted>
  </view>
</component>"""
    assert "Print speed" in _extract(tmp_path, xml)


def test_inline_text_collapsed_before_keying(tmp_path):
    xml = """<component>
  <view extends="lv_obj">
    <text_body name="msg">
      Multi line
      copy here
    </text_body>
  </view>
</component>"""
    result = _extract(tmp_path, xml)
    assert "Multi line copy here" in result
    assert not any("\n" in s for s in result)


def test_inline_entities_decoded_then_collapsed(tmp_path):
    # &#10; decodes to a newline, which then collapses to a space — this
    # mirrors expat (decode) + the C collapse, in that order.
    xml = '<view><text_muted name="m">Fish &amp; chips&#10;tonight</text_muted></view>'
    assert "Fish & chips tonight" in _extract(tmp_path, xml)


def test_inline_skips_bind_text_elements(tmp_path):
    xml = '<view><text_muted name="m" bind_text="some_subject">fallback junk</text_muted></view>'
    assert "fallback junk" not in _extract(tmp_path, xml)


def test_inline_skips_conflicting_text_attr(tmp_path):
    # Parser drops inline text when text= is present; extractor must too
    # (the text= value itself is still extracted by the existing attr pass).
    xml = '<view><text_muted name="m" text="Kept attr">dropped inline</text_muted></view>'
    result = _extract(tmp_path, xml)
    assert "dropped inline" not in result
    assert "Kept attr" in result


def test_inline_skips_prop_and_const_tokens(tmp_path):
    xml = """<view>
    <text_muted name="a">$title</text_muted>
    <text_muted name="b">#space_md</text_muted>
</view>"""
    result = _extract(tmp_path, xml)
    assert "$title" not in result
    assert "#space_md" not in result


def test_inline_whitespace_only_ignored(tmp_path):
    xml = '<view><text_muted name="m">\n    </text_muted></view>'
    assert "" not in _extract(tmp_path, xml)
