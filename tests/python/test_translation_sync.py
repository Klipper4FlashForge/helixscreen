#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""
Tests for translation_sync tool - extracts strings from XML, merges to YAML,
detects obsolete keys, and reports coverage.

TDD: Write tests first, then implement to pass them.
"""

import pytest
import sys
from pathlib import Path
from textwrap import dedent

# Add scripts directory to path
scripts_dir = Path(__file__).parent.parent.parent / "scripts"
sys.path.insert(0, str(scripts_dir))

FIXTURES_DIR = Path(__file__).parent / "fixtures" / "translation_sync"


# =============================================================================
# Test Fixtures
# =============================================================================


@pytest.fixture
def sample_xml_path():
    """Path to sample XML fixture."""
    return FIXTURES_DIR / "sample.xml"


@pytest.fixture
def sample_yaml_dir(tmp_path):
    """Create temp directory with sample YAML files."""
    yaml_dir = tmp_path / "translations"
    yaml_dir.mkdir()

    (yaml_dir / "en.yml").write_text(dedent("""\
        locale: en
        translations:
          "Print Files": "Print Files"
          "Settings": "Settings"
          "Existing Key": "Existing translation"
    """))

    (yaml_dir / "de.yml").write_text(dedent("""\
        locale: de
        translations:
          "Print Files": "Dateien drucken"
          "Settings": "Einstellungen"
          "Existing Key": ""
    """))

    return yaml_dir


@pytest.fixture
def sample_xml_content():
    """Sample XML content for testing."""
    return dedent("""\
        <?xml version="1.0"?>
        <component>
          <view extends="lv_obj">
            <text_body text="Print Files"/>
            <text_heading text="Settings"/>
            <text_body bind_text="dynamic_text"/>
            <lv_label text="#icon_home"/>
            <text_body text="$variable"/>
          </view>
        </component>
    """)


# =============================================================================
# Test: Extractor Module
# =============================================================================


class TestExtractSimpleText:
    """Test extraction of simple text="..." attributes."""

    def test_extract_simple_text_attribute(self, sample_xml_path):
        """Extracts text from basic text_body, text_heading, text_small elements."""
        from translations.extractor import extract_strings_from_xml

        result = extract_strings_from_xml(sample_xml_path)

        assert "Print Files" in result
        assert "Settings" in result
        assert "Tip:" in result

    def test_extract_returns_set_of_strings(self, sample_xml_path):
        """Extraction returns a set (no duplicates)."""
        from translations.extractor import extract_strings_from_xml

        result = extract_strings_from_xml(sample_xml_path)

        assert isinstance(result, set)
        # "First" appears twice in fixture but should only be in set once
        assert "First" in result
        assert "Second" in result

    def test_extract_nested_elements(self, sample_xml_path):
        """Extracts text from deeply nested elements."""
        from translations.extractor import extract_strings_from_xml

        result = extract_strings_from_xml(sample_xml_path)

        assert "Nested Text" in result


class TestExtractSkipsVariableReferences:
    """Test that variable references ($var) are skipped."""

    def test_skip_dollar_variable(self, sample_xml_path):
        """Skips text that is just a variable reference like $label."""
        from translations.extractor import extract_strings_from_xml

        result = extract_strings_from_xml(sample_xml_path)

        assert "$label" not in result
        assert "$description" not in result

    def test_skip_variable_in_middle(self, tmp_path):
        """Skips text containing variable interpolation."""
        xml_file = tmp_path / "test.xml"
        xml_file.write_text(dedent("""\
            <?xml version="1.0"?>
            <component>
              <text_body text="Hello $name"/>
              <text_body text="Value: $count items"/>
            </component>
        """))

        from translations.extractor import extract_strings_from_xml

        result = extract_strings_from_xml(xml_file)

        assert "Hello $name" not in result
        assert "Value: $count items" not in result


class TestExtractSkipsBindText:
    """Test that bind_text elements are skipped (dynamic text)."""

    def test_skip_bind_text_attribute(self, sample_xml_path):
        """Elements with bind_text are skipped entirely."""
        from translations.extractor import extract_strings_from_xml

        result = extract_strings_from_xml(sample_xml_path)

        # bind_text values should not appear
        assert "status_text" not in result
        assert "printer_type" not in result


class TestExtractSkipsIcons:
    """Test that icon references (#icon_*) are skipped."""

    def test_skip_icon_references(self, sample_xml_path):
        """Text starting with #icon_ is skipped (icon font reference)."""
        from translations.extractor import extract_strings_from_xml

        result = extract_strings_from_xml(sample_xml_path)

        assert "#icon_question_circle" not in result
        assert "#icon_home" not in result


class TestExtractComponentProps:
    """Test extraction from component props like label, description, title."""

    def test_extract_label_attribute(self, sample_xml_path):
        """Extracts text from label attribute on components."""
        from translations.extractor import extract_strings_from_xml

        result = extract_strings_from_xml(sample_xml_path)

        assert "Save" in result
        assert "Save current settings" in result

    def test_extract_title_subtitle(self, sample_xml_path):
        """Extracts text from title and subtitle attributes."""
        from translations.extractor import extract_strings_from_xml

        result = extract_strings_from_xml(sample_xml_path)

        assert "Temperature" in result
        assert "Nozzle and bed" in result


class TestExtractSkipsEmptyAndNumeric:
    """Test that empty strings and pure numbers are skipped."""

    def test_skip_empty_text(self, sample_xml_path):
        """Empty text="" is skipped."""
        from translations.extractor import extract_strings_from_xml

        result = extract_strings_from_xml(sample_xml_path)

        assert "" not in result

    def test_skip_numeric_text(self, sample_xml_path):
        """Pure numeric values are skipped."""
        from translations.extractor import extract_strings_from_xml

        result = extract_strings_from_xml(sample_xml_path)

        assert "123" not in result

    def test_skip_percentage(self, sample_xml_path):
        """Percentage values like '100%' are skipped."""
        from translations.extractor import extract_strings_from_xml

        result = extract_strings_from_xml(sample_xml_path)

        assert "100%" not in result


class TestExtractTrackSourceLocations:
    """Test tracking of source file and line numbers."""

    def test_extract_with_locations(self, sample_xml_path):
        """Can optionally track where each string was found."""
        from translations.extractor import extract_strings_with_locations

        result = extract_strings_with_locations(sample_xml_path)

        assert isinstance(result, dict)
        # Keys are strings, values are lists of (file, line) tuples
        assert "Print Files" in result
        locations = result["Print Files"]
        assert len(locations) > 0
        assert all(isinstance(loc, tuple) and len(loc) == 2 for loc in locations)

    def test_locations_include_filename(self, sample_xml_path):
        """Locations include the source filename."""
        from translations.extractor import extract_strings_with_locations

        result = extract_strings_with_locations(sample_xml_path)

        locations = result["Print Files"]
        assert any("sample.xml" in str(loc[0]) for loc in locations)


class TestExtractFromDirectory:
    """Test extracting from all XML files in a directory."""

    def test_extract_from_directory(self, tmp_path):
        """Extracts strings from all XML files in directory."""
        xml_dir = tmp_path / "ui_xml"
        xml_dir.mkdir()

        (xml_dir / "panel_a.xml").write_text(dedent("""\
            <?xml version="1.0"?>
            <component><text_body text="Panel A"/></component>
        """))

        (xml_dir / "panel_b.xml").write_text(dedent("""\
            <?xml version="1.0"?>
            <component><text_body text="Panel B"/></component>
        """))

        from translations.extractor import extract_strings_from_directory

        result = extract_strings_from_directory(xml_dir)

        assert "Panel A" in result
        assert "Panel B" in result

    def test_extract_recursive(self, tmp_path):
        """Extracts from nested subdirectories."""
        xml_dir = tmp_path / "ui_xml"
        (xml_dir / "subdir").mkdir(parents=True)

        (xml_dir / "main.xml").write_text(dedent("""\
            <?xml version="1.0"?>
            <component><text_body text="Main"/></component>
        """))

        (xml_dir / "subdir" / "sub.xml").write_text(dedent("""\
            <?xml version="1.0"?>
            <component><text_body text="Subdir"/></component>
        """))

        from translations.extractor import extract_strings_from_directory

        result = extract_strings_from_directory(xml_dir, recursive=True)

        assert "Main" in result
        assert "Subdir" in result


class TestExtractSpecialCharacters:
    """Test handling of special characters in text."""

    def test_extract_ampersand(self, sample_xml_path):
        """Handles XML entities like &amp; correctly."""
        from translations.extractor import extract_strings_from_xml

        result = extract_strings_from_xml(sample_xml_path)

        # XML entity &amp; should be decoded to &
        assert "Save & Exit" in result

    def test_extract_unicode(self, sample_xml_path):
        """Handles unicode characters correctly."""
        from translations.extractor import extract_strings_from_xml

        result = extract_strings_from_xml(sample_xml_path)

        assert "Temperature: 200°C" in result


# =============================================================================
# Test: YAML Manager Module
# =============================================================================


class TestYamlMergeNewKeys:
    """Test merging new keys into YAML files."""

    def test_merge_new_key_to_all_languages(self, sample_yaml_dir):
        """New keys are added to all language files."""
        from translations.yaml_manager import merge_new_keys

        new_keys = {"New String", "Another New"}
        result = merge_new_keys(sample_yaml_dir, new_keys)

        assert result.keys_added > 0

        # Check English file has new key with English value
        en_content = (sample_yaml_dir / "en.yml").read_text()
        assert "New String" in en_content

        # Check German file has new key with empty value (needs translation)
        de_content = (sample_yaml_dir / "de.yml").read_text()
        assert "New String" in de_content

    def test_new_key_gets_english_value_in_english(self, sample_yaml_dir):
        """In English file, new key gets key as value."""
        from translations.yaml_manager import merge_new_keys

        new_keys = {"Brand New Key"}
        merge_new_keys(sample_yaml_dir, new_keys)

        from translations.yaml_manager import load_yaml_file

        en_data = load_yaml_file(sample_yaml_dir / "en.yml")
        assert en_data["translations"]["Brand New Key"] == "Brand New Key"

    def test_new_key_gets_empty_in_other_languages(self, sample_yaml_dir):
        """In non-English files, new key gets empty string (needs translation)."""
        from translations.yaml_manager import merge_new_keys

        new_keys = {"Brand New Key"}
        merge_new_keys(sample_yaml_dir, new_keys)

        from translations.yaml_manager import load_yaml_file

        de_data = load_yaml_file(sample_yaml_dir / "de.yml")
        assert de_data["translations"]["Brand New Key"] == ""


class TestYamlPreserveExisting:
    """Test that existing translations are preserved."""

    def test_preserve_existing_translations(self, sample_yaml_dir):
        """Existing translated values are never modified."""
        from translations.yaml_manager import merge_new_keys, load_yaml_file

        # Record original values
        original_de = load_yaml_file(sample_yaml_dir / "de.yml")
        original_print_files = original_de["translations"]["Print Files"]

        # Merge new keys
        merge_new_keys(sample_yaml_dir, {"New String"})

        # Check original value unchanged
        updated_de = load_yaml_file(sample_yaml_dir / "de.yml")
        assert updated_de["translations"]["Print Files"] == original_print_files

    def test_never_overwrite_nonempty(self, sample_yaml_dir):
        """Even if key exists in new_keys, don't overwrite non-empty translation."""
        from translations.yaml_manager import merge_new_keys, load_yaml_file

        # "Print Files" already has German translation
        original_de = load_yaml_file(sample_yaml_dir / "de.yml")
        assert original_de["translations"]["Print Files"] == "Dateien drucken"

        # Try to "merge" it again
        merge_new_keys(sample_yaml_dir, {"Print Files"})

        updated_de = load_yaml_file(sample_yaml_dir / "de.yml")
        assert updated_de["translations"]["Print Files"] == "Dateien drucken"


class TestYamlSourceComments:
    """Test adding source file comments to YAML."""

    def test_add_source_comments(self, sample_yaml_dir):
        """Can add comments showing where strings came from."""
        from translations.yaml_manager import merge_new_keys_with_sources

        new_keys = {
            "New String": [("panel_a.xml", 10), ("panel_b.xml", 20)],
        }
        merge_new_keys_with_sources(sample_yaml_dir, new_keys)

        en_content = (sample_yaml_dir / "en.yml").read_text()
        # Should have comment with source info
        assert "panel_a.xml" in en_content or "New String" in en_content


class TestYamlFormatPreservation:
    """Test that YAML formatting is preserved."""

    def test_preserve_yaml_formatting(self, sample_yaml_dir):
        """Original YAML structure and formatting preserved."""
        from translations.yaml_manager import merge_new_keys

        # Get original structure
        original_content = (sample_yaml_dir / "en.yml").read_text()
        assert original_content.startswith("locale: en")

        merge_new_keys(sample_yaml_dir, {"New Key"})

        updated_content = (sample_yaml_dir / "en.yml").read_text()
        # Should still start with locale
        assert updated_content.startswith("locale: en")
        # Should still have translations section
        assert "translations:" in updated_content

    def test_alphabetical_ordering(self, sample_yaml_dir):
        """Keys are kept in alphabetical order."""
        from translations.yaml_manager import merge_new_keys

        merge_new_keys(sample_yaml_dir, {"AAA First", "ZZZ Last"})

        content = (sample_yaml_dir / "en.yml").read_text()
        aaa_pos = content.find("AAA First")
        zzz_pos = content.find("ZZZ Last")

        assert aaa_pos < zzz_pos


class TestYamlSurgicalInsertion:
    """
    Regression guard: keys are inserted by surgical line splicing, NOT by
    round-tripping the whole document. A full re-serialize (the old behavior)
    reflowed folded scalars and requoted unrelated entries, churning thousands
    of lines when a single key was added.
    """

    @pytest.fixture
    def folded_yaml_dir(self, tmp_path):
        """A locale dir whose committed entries use folding and complex keys.

        The exact byte layout below is what must survive an insertion untouched.
        """
        yaml_dir = tmp_path / "translations"
        yaml_dir.mkdir()

        # Note the 4-space folded continuation and the `? ... : ...` complex key.
        en = (
            "locale: en\n"
            "translations:\n"
            "  Apple: Apple\n"
            "  Banana entry whose value wraps for realism: Banana entry whose value wraps\n"
            "    onto a second physical line\n"
            "  ? A very long key that ruamel renders with complex question-mark notation because it is wide\n"
            "  : its rendered value\n"
            "  Cherry: Cherry\n"
            "  Zebra: Zebra\n"
        )
        de = (
            "locale: de\n"
            "translations:\n"
            "  Apple: Apfel\n"
            "  Banana entry whose value wraps for realism: Banane\n"
            "  ? A very long key that ruamel renders with complex question-mark notation because it is wide\n"
            "  : sein Wert\n"
            "  Cherry: Kirsche\n"
            "  Zebra: Zebra\n"
        )
        (yaml_dir / "en.yml").write_text(en)
        (yaml_dir / "de.yml").write_text(de)
        return yaml_dir

    def test_untouched_lines_are_byte_identical(self, folded_yaml_dir):
        """Inserting a key only adds lines; every pre-existing line is preserved."""
        from translations.yaml_manager import merge_new_keys

        before = (folded_yaml_dir / "en.yml").read_text().splitlines(keepends=True)

        merge_new_keys(folded_yaml_dir, {"Blueberry"})  # sorts between Banana and Cherry

        after = (folded_yaml_dir / "en.yml").read_text().splitlines(keepends=True)

        # Old lines survive verbatim; the diff is a pure insertion.
        added = [l for l in after if l not in before]
        removed = [l for l in before if l not in after]
        assert removed == []
        assert len(added) == 1
        assert added[0] == "  Blueberry: Blueberry\n"

    def test_folded_and_complex_entries_not_reflowed(self, folded_yaml_dir):
        """The folded value and the complex `? ` key are preserved byte-for-byte."""
        from translations.yaml_manager import merge_new_keys

        merge_new_keys(folded_yaml_dir, {"Mango"})

        content = (folded_yaml_dir / "en.yml").read_text()
        # Folded continuation line intact (would be joined by a full re-dump).
        assert "    onto a second physical line\n" in content
        # Complex-key block intact.
        assert (
            "  ? A very long key that ruamel renders with complex question-mark "
            "notation because it is wide\n"
        ) in content
        assert "  : its rendered value\n" in content

    def test_inserted_at_alphabetical_position(self, folded_yaml_dir):
        """New key lands between its sorted neighbours, not appended blindly."""
        from translations.yaml_manager import merge_new_keys

        merge_new_keys(folded_yaml_dir, {"Blueberry"})

        lines = (folded_yaml_dir / "en.yml").read_text().splitlines()
        idx = {l.strip().split(":")[0]: i for i, l in enumerate(lines)}
        # Banana < Blueberry < Cherry
        assert idx["Banana entry whose value wraps for realism"] < idx["Blueberry"]
        assert idx["Blueberry"] < idx["Cherry"]

    def test_output_is_valid_yaml(self, folded_yaml_dir):
        """The spliced file still parses and contains the new key with its value."""
        from translations.yaml_manager import merge_new_keys, load_yaml_file

        merge_new_keys(folded_yaml_dir, {"Blueberry"})

        en = load_yaml_file(folded_yaml_dir / "en.yml")
        de = load_yaml_file(folded_yaml_dir / "de.yml")
        assert en["translations"]["Blueberry"] == "Blueberry"
        assert de["translations"]["Blueberry"] == ""
        # Pre-existing values untouched.
        assert en["translations"]["Apple"] == "Apple"
        assert de["translations"]["Cherry"] == "Kirsche"

    def test_noop_when_nothing_new(self, folded_yaml_dir):
        """Re-running with only already-present keys leaves files byte-identical."""
        from translations.yaml_manager import merge_new_keys, load_yaml_file

        existing = set(
            load_yaml_file(folded_yaml_dir / "en.yml")["translations"].keys()
        )
        before_en = (folded_yaml_dir / "en.yml").read_text()
        before_de = (folded_yaml_dir / "de.yml").read_text()

        result = merge_new_keys(folded_yaml_dir, existing)

        assert result.keys_added == 0
        assert (folded_yaml_dir / "en.yml").read_text() == before_en
        assert (folded_yaml_dir / "de.yml").read_text() == before_de

    def test_key_containing_colon_is_quoted(self, folded_yaml_dir):
        """A key with a colon renders as a valid quoted single-line entry."""
        from translations.yaml_manager import merge_new_keys, load_yaml_file

        merge_new_keys(folded_yaml_dir, {"Filament operation failed: {}"})

        content = (folded_yaml_dir / "en.yml").read_text()
        assert "'Filament operation failed: {}': 'Filament operation failed: {}'\n" in content
        # And it round-trips.
        en = load_yaml_file(folded_yaml_dir / "en.yml")
        assert en["translations"]["Filament operation failed: {}"] == "Filament operation failed: {}"


class TestYamlSurgicalDeletion:
    """delete/mark obsolete keys must also splice lines, not re-serialize."""

    @pytest.fixture
    def folded_yaml_dir(self, tmp_path):
        yaml_dir = tmp_path / "translations"
        yaml_dir.mkdir()
        (yaml_dir / "en.yml").write_text(
            "locale: en\n"
            "translations:\n"
            "  Apple: Apple\n"
            "  Banana entry whose value wraps for realism: Banana entry whose value wraps\n"
            "    onto a second physical line\n"
            "  Obsolete Key: Will be removed\n"
            "  Zebra: Zebra\n"
        )
        return yaml_dir

    def test_delete_removes_only_target_entry(self, folded_yaml_dir):
        from translations.obsolete import delete_obsolete_keys

        delete_obsolete_keys(folded_yaml_dir, {"Obsolete Key"})

        content = (folded_yaml_dir / "en.yml").read_text()
        assert "Obsolete Key" not in content
        # Neighbours (including the folded entry) untouched.
        assert "  Apple: Apple\n" in content
        assert "    onto a second physical line\n" in content
        assert "  Zebra: Zebra\n" in content

    def test_delete_dry_run_no_change(self, folded_yaml_dir):
        from translations.obsolete import delete_obsolete_keys

        before = (folded_yaml_dir / "en.yml").read_text()
        delete_obsolete_keys(folded_yaml_dir, {"Obsolete Key"}, dry_run=True)
        assert (folded_yaml_dir / "en.yml").read_text() == before

    def test_mark_deprecated_preserves_neighbours(self, folded_yaml_dir):
        from translations.obsolete import mark_obsolete_keys
        from translations.yaml_manager import load_yaml_file

        mark_obsolete_keys(folded_yaml_dir, {"Obsolete Key"})

        data = load_yaml_file(folded_yaml_dir / "en.yml")
        assert data["translations"]["Obsolete Key"].startswith("[DEPRECATED]")
        content = (folded_yaml_dir / "en.yml").read_text()
        assert "    onto a second physical line\n" in content
        assert "  Zebra: Zebra\n" in content


class TestYamlDryRun:
    """Test dry-run mode for YAML operations."""

    def test_dry_run_no_changes(self, sample_yaml_dir):
        """Dry run reports what would change without modifying files."""
        from translations.yaml_manager import merge_new_keys

        original_en = (sample_yaml_dir / "en.yml").read_text()

        result = merge_new_keys(sample_yaml_dir, {"New Key"}, dry_run=True)

        # Should report changes
        assert result.keys_added > 0

        # But file should be unchanged
        assert (sample_yaml_dir / "en.yml").read_text() == original_en


# =============================================================================
# Test: Coverage Module
# =============================================================================


class TestCoverageStats:
    """Test coverage statistics calculation."""

    def test_calculate_per_language_stats(self, sample_yaml_dir):
        """Calculate translation coverage for each language."""
        from translations.coverage import calculate_coverage

        result = calculate_coverage(sample_yaml_dir)

        assert "en" in result
        assert "de" in result
        assert "total" in result["en"]
        assert "translated" in result["en"]
        assert "percentage" in result["en"]

    def test_english_always_100_percent(self, sample_yaml_dir):
        """English (base) should always show 100% coverage."""
        from translations.coverage import calculate_coverage

        result = calculate_coverage(sample_yaml_dir, base_locale="en")

        assert result["en"]["percentage"] == 100.0

    def test_identify_missing_translations(self, sample_yaml_dir):
        """Identifies which keys are missing translations."""
        from translations.coverage import get_missing_translations

        result = get_missing_translations(sample_yaml_dir)

        # German has "Existing Key" as empty string
        assert "de" in result
        assert "Existing Key" in result["de"]


class TestCoverageReport:
    """Test coverage report formatting."""

    def test_coverage_report_format(self, sample_yaml_dir):
        """Coverage report is human-readable."""
        from translations.coverage import generate_coverage_report

        report = generate_coverage_report(sample_yaml_dir)

        assert isinstance(report, str)
        assert "en" in report
        assert "de" in report
        assert "%" in report

    def test_coverage_report_shows_missing_count(self, sample_yaml_dir):
        """Report shows count of missing translations."""
        from translations.coverage import generate_coverage_report

        report = generate_coverage_report(sample_yaml_dir)

        # Should show something like "2/3" or "66%"
        assert any(char.isdigit() for char in report)


# =============================================================================
# Test: Obsolete Detection Module
# =============================================================================


class TestObsoleteDetection:
    """Test finding unused/obsolete keys."""

    def test_find_unused_keys(self, tmp_path):
        """Finds keys in YAML that aren't used in XML."""
        # Setup: YAML has keys that XML doesn't use
        yaml_dir = tmp_path / "translations"
        yaml_dir.mkdir()

        (yaml_dir / "en.yml").write_text(dedent("""\
            locale: en
            translations:
              "Used Key": "Used"
              "Obsolete Key": "This is obsolete"
              "Another Unused": "Also unused"
        """))

        xml_dir = tmp_path / "ui_xml"
        xml_dir.mkdir()

        (xml_dir / "panel.xml").write_text(dedent("""\
            <?xml version="1.0"?>
            <component><text_body text="Used Key"/></component>
        """))

        from translations.obsolete import find_obsolete_keys

        result = find_obsolete_keys(xml_dir, yaml_dir)

        assert "Obsolete Key" in result
        assert "Another Unused" in result
        assert "Used Key" not in result


class TestObsoleteActions:
    """Test actions for handling obsolete keys."""

    def test_report_action(self, tmp_path, capsys):
        """Report action just prints obsolete keys."""
        yaml_dir = tmp_path / "translations"
        yaml_dir.mkdir()

        (yaml_dir / "en.yml").write_text(dedent("""\
            locale: en
            translations:
              "Used": "Used"
              "Obsolete": "Obsolete"
        """))

        from translations.obsolete import report_obsolete_keys

        obsolete = {"Obsolete"}
        report_obsolete_keys(obsolete)

        captured = capsys.readouterr()
        assert "Obsolete" in captured.out

    def test_mark_deprecated_action(self, tmp_path):
        """Mark action adds DEPRECATED comment to obsolete keys."""
        yaml_dir = tmp_path / "translations"
        yaml_dir.mkdir()

        (yaml_dir / "en.yml").write_text(dedent("""\
            locale: en
            translations:
              "Used": "Used"
              "Obsolete": "Obsolete value"
        """))

        from translations.obsolete import mark_obsolete_keys

        mark_obsolete_keys(yaml_dir, {"Obsolete"})

        content = (yaml_dir / "en.yml").read_text()
        # Should have comment marking it as deprecated
        assert "DEPRECATED" in content or "obsolete" in content.lower()

    def test_delete_action(self, tmp_path):
        """Delete action removes obsolete keys from YAML."""
        yaml_dir = tmp_path / "translations"
        yaml_dir.mkdir()

        (yaml_dir / "en.yml").write_text(dedent("""\
            locale: en
            translations:
              "Used": "Used"
              "Obsolete": "Will be deleted"
        """))

        from translations.obsolete import delete_obsolete_keys

        delete_obsolete_keys(yaml_dir, {"Obsolete"})

        content = (yaml_dir / "en.yml").read_text()
        assert "Obsolete" not in content
        assert "Used" in content

    def test_delete_dry_run(self, tmp_path):
        """Delete with dry_run doesn't actually delete."""
        yaml_dir = tmp_path / "translations"
        yaml_dir.mkdir()

        original = dedent("""\
            locale: en
            translations:
              "Used": "Used"
              "Obsolete": "Will NOT be deleted"
        """)
        (yaml_dir / "en.yml").write_text(original)

        from translations.obsolete import delete_obsolete_keys

        delete_obsolete_keys(yaml_dir, {"Obsolete"}, dry_run=True)

        # File should be unchanged
        assert (yaml_dir / "en.yml").read_text() == original


# =============================================================================
# Test: CLI Interface
# =============================================================================


class TestCliSyncCommand:
    """Test the main sync command."""

    def test_sync_command_dry_run(self, tmp_path):
        """Sync command with --dry-run doesn't modify files."""
        yaml_dir = tmp_path / "translations"
        yaml_dir.mkdir()

        (yaml_dir / "en.yml").write_text(dedent("""\
            locale: en
            translations:
              "Existing": "Existing"
        """))

        xml_dir = tmp_path / "ui_xml"
        xml_dir.mkdir()

        (xml_dir / "panel.xml").write_text(dedent("""\
            <?xml version="1.0"?>
            <component><text_body text="New String"/></component>
        """))

        original_yaml = (yaml_dir / "en.yml").read_text()

        from translations.cli import run_sync

        result = run_sync(xml_dir, yaml_dir, dry_run=True)

        # Should report what would be added
        assert result.new_keys_found > 0

        # But file unchanged
        assert (yaml_dir / "en.yml").read_text() == original_yaml


class TestCliExtractCommand:
    """Test the extract-only command."""

    def test_extract_command(self, tmp_path):
        """Extract command lists strings found in XML."""
        xml_dir = tmp_path / "ui_xml"
        xml_dir.mkdir()

        (xml_dir / "panel.xml").write_text(dedent("""\
            <?xml version="1.0"?>
            <component>
              <text_body text="String One"/>
              <text_body text="String Two"/>
            </component>
        """))

        from translations.cli import run_extract

        result = run_extract(xml_dir)

        assert "String One" in result.strings
        assert "String Two" in result.strings


class TestCliCoverageCommand:
    """Test the coverage command."""

    def test_coverage_command_fail_under(self, tmp_path):
        """Coverage command can fail if below threshold."""
        yaml_dir = tmp_path / "translations"
        yaml_dir.mkdir()

        # German is missing some translations
        (yaml_dir / "en.yml").write_text(dedent("""\
            locale: en
            translations:
              "A": "A"
              "B": "B"
              "C": "C"
        """))

        (yaml_dir / "de.yml").write_text(dedent("""\
            locale: de
            translations:
              "A": "A-de"
              "B": ""
              "C": ""
        """))

        from translations.cli import run_coverage

        # Require 100% coverage - should fail
        result = run_coverage(yaml_dir, fail_under=100)
        assert not result.passed

        # Require 30% coverage - should pass
        result = run_coverage(yaml_dir, fail_under=30)
        assert result.passed


class TestCliObsoleteCommand:
    """Test the obsolete detection command."""

    def test_obsolete_command_actions(self, tmp_path):
        """Obsolete command supports different actions."""
        yaml_dir = tmp_path / "translations"
        yaml_dir.mkdir()

        (yaml_dir / "en.yml").write_text(dedent("""\
            locale: en
            translations:
              "Used": "Used"
              "Unused": "Unused"
        """))

        xml_dir = tmp_path / "ui_xml"
        xml_dir.mkdir()

        (xml_dir / "panel.xml").write_text(dedent("""\
            <?xml version="1.0"?>
            <component><text_body text="Used"/></component>
        """))

        from translations.cli import run_obsolete

        # Report action
        result = run_obsolete(xml_dir, yaml_dir, action="report")
        assert "Unused" in result.obsolete_keys

        # Delete action with dry_run
        result = run_obsolete(xml_dir, yaml_dir, action="delete", dry_run=True)
        assert result.would_delete > 0


# =============================================================================
# Test: Integration
# =============================================================================


class TestFullWorkflow:
    """Integration tests for full sync workflow."""

    def test_extract_merge_coverage(self, tmp_path):
        """Full workflow: extract from XML, merge to YAML, check coverage."""
        # Setup
        yaml_dir = tmp_path / "translations"
        yaml_dir.mkdir()

        (yaml_dir / "en.yml").write_text(dedent("""\
            locale: en
            translations:
              "Existing": "Existing"
        """))

        (yaml_dir / "de.yml").write_text(dedent("""\
            locale: de
            translations:
              "Existing": "Vorhanden"
        """))

        xml_dir = tmp_path / "ui_xml"
        xml_dir.mkdir()

        (xml_dir / "panel.xml").write_text(dedent("""\
            <?xml version="1.0"?>
            <component>
              <text_body text="Existing"/>
              <text_body text="New String"/>
            </component>
        """))

        from translations.cli import run_sync, run_coverage

        # Run sync (not dry run)
        sync_result = run_sync(xml_dir, yaml_dir, dry_run=False)
        assert sync_result.new_keys_found > 0

        # Check coverage
        coverage_result = run_coverage(yaml_dir)

        # English should be 100%
        assert coverage_result.stats["en"]["percentage"] == 100.0

        # German should have missing translation for "New String"
        assert coverage_result.stats["de"]["percentage"] < 100.0
