#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""
Tests for the ESP32 LittleFS staging script's XML minifier: comments must be
stripped, inter-tag whitespace collapsed, and text content / attribute values
byte-preserved. Also round-trips a real ui_xml/ file through xml.etree to
confirm the minified output still parses and carries identical text/attrs.
"""

import sys
import xml.etree.ElementTree as ET
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(REPO_ROOT / "scripts"))

from esp32_stage_assets import minify_xml  # noqa: E402


def test_strips_single_line_comment():
    xml = '<component><!-- a comment --><widget/></component>'
    out = minify_xml(xml)
    assert "<!--" not in out
    assert "comment" not in out


def test_strips_multiline_comment():
    xml = "<component>\n  <!-- line one\n       line two -->\n  <widget/>\n</component>"
    out = minify_xml(xml)
    assert "<!--" not in out
    assert "line one" not in out
    assert "line two" not in out


def test_collapses_inter_tag_whitespace():
    xml = "<component>\n  <widget/>\n  <widget/>\n</component>"
    out = minify_xml(xml)
    assert ">\n" not in out
    assert "\n<" not in out
    assert out == "<component><widget/><widget/></component>"


def test_preserves_text_content_exactly():
    xml = '<label>Hello   World\n  with   odd  spacing</label>'
    out = minify_xml(xml)
    assert "Hello   World\n  with   odd  spacing" in out


def test_preserves_attribute_values_exactly():
    xml = '<widget style_pad_all="  12  " text="a    b">\n  <child/>\n</widget>'
    out = minify_xml(xml)
    assert 'style_pad_all="  12  "' in out
    assert 'text="a    b"' in out


def test_preserves_leading_trailing_whitespace_in_text_node():
    # Regression guard: text nodes sit in the same '>...<' position the
    # inter-tag collapse targets. A naive whitespace-only-blind collapse
    # would eat this text; the minifier must leave any non-whitespace-only
    # span alone.
    xml = "<a>  padded text  </a>"
    out = minify_xml(xml)
    assert out == xml


def test_does_not_touch_pure_whitespace_text_node_between_tags():
    # A genuinely empty/whitespace-only element body between two tags is
    # exactly what "inter-tag whitespace" collapse targets, distinct from
    # real (non-whitespace) text content.
    xml = "<a>\n   \n</a>"
    out = minify_xml(xml)
    assert out == "<a></a>"


def test_globals_xml_round_trips_and_preserves_content():
    src_path = REPO_ROOT / "ui_xml" / "globals.xml"
    original = src_path.read_text(encoding="utf-8")
    minified = minify_xml(original)

    assert len(minified) < len(original), "minifier should shrink a real, comment-heavy file"
    assert "<!--" not in minified

    original_root = ET.fromstring(original)
    minified_root = ET.fromstring(minified)  # must still parse

    def collect(elem):
        return [
            (elem.tag, dict(elem.attrib), (elem.text or "").strip(), (elem.tail or "").strip())
            for elem in elem.iter()
        ]

    assert collect(original_root) == collect(minified_root)
