# SPDX-License-Identifier: GPL-3.0-or-later

"""Unit tests for golden comparison. No app required."""

import pytest
from PIL import Image

from helix.goldens import GoldenMismatch, assert_golden, compare


def _solid(w, h, color):
    return Image.new("RGBA", (w, h), color)


def test_identical_images_match(tmp_path):
    golden = tmp_path / "g.png"
    _solid(4, 4, (10, 20, 30, 255)).save(golden)
    result = compare(_solid(4, 4, (10, 20, 30, 255)), golden)
    assert result.matches


def test_single_pixel_difference_fails(tmp_path):
    golden = tmp_path / "g.png"
    _solid(4, 4, (10, 20, 30, 255)).save(golden)
    actual = _solid(4, 4, (10, 20, 30, 255))
    actual.putpixel((0, 0), (11, 20, 30, 255))
    result = compare(actual, golden)
    assert not result.matches
    assert "1 pixel" in result.reason


def test_size_mismatch_reports_both_sizes(tmp_path):
    golden = tmp_path / "g.png"
    _solid(4, 4, (0, 0, 0, 255)).save(golden)
    result = compare(_solid(8, 4, (0, 0, 0, 255)), golden)
    assert not result.matches
    assert "8x4" in result.reason and "4x4" in result.reason


def test_missing_golden_raises_rather_than_creating_it(tmp_path):
    # A silently created golden asserts nothing on its first run.
    with pytest.raises(GoldenMismatch) as exc:
        assert_golden(_solid(4, 4, (0, 0, 0, 255)), "newthing",
                      goldens_dir=tmp_path / "goldens",
                      artifacts_dir=tmp_path / "artifacts",
                      accept=False)
    assert "--accept-goldens" in str(exc.value)
    assert not (tmp_path / "goldens" / "newthing.png").exists()


def test_accept_writes_the_golden(tmp_path):
    assert_golden(_solid(4, 4, (1, 2, 3, 255)), "newthing",
                  goldens_dir=tmp_path / "goldens",
                  artifacts_dir=tmp_path / "artifacts",
                  accept=True)
    assert (tmp_path / "goldens" / "newthing.png").exists()


def test_mismatch_writes_actual_and_diff_artifacts(tmp_path):
    golden_dir = tmp_path / "goldens"
    golden_dir.mkdir()
    _solid(4, 4, (0, 0, 0, 255)).save(golden_dir / "thing.png")
    with pytest.raises(GoldenMismatch):
        assert_golden(_solid(4, 4, (255, 255, 255, 255)), "thing",
                      goldens_dir=golden_dir,
                      artifacts_dir=tmp_path / "artifacts",
                      accept=False)
    assert (tmp_path / "artifacts" / "thing.actual.png").exists()
    assert (tmp_path / "artifacts" / "thing.diff.png").exists()
