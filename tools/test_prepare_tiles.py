"""Testes do prepare_tiles.py (sem rede). Rodar: python3 -m pytest tools/test_prepare_tiles.py -q"""
import os
import sys
from pathlib import Path

from PIL import Image

sys.path.insert(0, str(Path(__file__).parent))
import prepare_tiles  # noqa: E402


def _img(path: Path, size=(256, 256), mode="RGB", **save):
    path.parent.mkdir(parents=True, exist_ok=True)
    Image.new(mode, size, (10, 120, 200) if mode == "RGB" else (10, 120, 200, 255)).save(path, **save)


def test_png_becomes_baseline_jpeg(tmp_path):
    _img(tmp_path / "in/15/1/2.png", mode="RGBA")
    prepare_tiles.prepare(tmp_path / "in", tmp_path / "out", 80)
    out = tmp_path / "out/tiles/15/1/2.jpg"
    with Image.open(out) as im:
        assert im.format == "JPEG"
        assert "progressive" not in im.info and "progression" not in im.info
        assert im.size == (256, 256)


def test_progressive_jpeg_reencoded_baseline(tmp_path):
    _img(tmp_path / "in/14/3/4.jpg", format="JPEG", progressive=True)
    with Image.open(tmp_path / "in/14/3/4.jpg") as im:
        assert "progressive" in im.info or "progression" in im.info  # a entrada é mesmo progressiva
    prepare_tiles.prepare(tmp_path / "in", tmp_path / "out", 80)
    with Image.open(tmp_path / "out/tiles/14/3/4.jpg") as im:
        assert "progressive" not in im.info and "progression" not in im.info


def test_resizes_non_256(tmp_path):
    _img(tmp_path / "in/12/0/0.png", size=(512, 512))
    prepare_tiles.prepare(tmp_path / "in", tmp_path / "out", 80)
    with Image.open(tmp_path / "out/tiles/12/0/0.jpg") as im:
        assert im.size == (256, 256)


def test_report_counts_holes(tmp_path):
    for x, y in [(0, 0), (1, 0), (0, 1)]:
        _img(tmp_path / f"in/10/{x}/{y}.png")
    rep = prepare_tiles.prepare(tmp_path / "in", tmp_path / "out", 80)
    assert rep[10]["count"] == 3
    assert rep[10]["holes"] == 1
    assert (rep[10]["xmin"], rep[10]["xmax"], rep[10]["ymin"], rep[10]["ymax"]) == (0, 1, 0, 1)


def test_skips_up_to_date(tmp_path):
    _img(tmp_path / "in/10/0/0.png")
    prepare_tiles.prepare(tmp_path / "in", tmp_path / "out", 80)
    out = tmp_path / "out/tiles/10/0/0.jpg"
    os.utime(out, (2_000_000_000, 2_000_000_000))
    prepare_tiles.prepare(tmp_path / "in", tmp_path / "out", 80)
    assert out.stat().st_mtime == 2_000_000_000
