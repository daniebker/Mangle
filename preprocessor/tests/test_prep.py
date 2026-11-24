import json
import sys
import zipfile
from pathlib import Path

import numpy as np
import pytest
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
sys.path.insert(0, str(Path(__file__).resolve().parent))

import sample  # noqa: E402
from mangle_prep import archive, optimize, panels  # noqa: E402
from mangle_prep.pipeline import Config, build  # noqa: E402


def test_natural_sort():
    names = ["p10.jpg", "p2.jpg", "P1.jpg", "p3.jpg"]
    assert sorted(names, key=archive.natural_key) == ["P1.jpg", "p2.jpg", "p3.jpg", "p10.jpg"]


def test_list_pages_skips_junk(tmp_path):
    z = tmp_path / "x.cbz"
    with zipfile.ZipFile(z, "w") as zf:
        for n in ["b/page10.png", "b/page2.png", "__MACOSX/._page2.png", ".hidden.png", "readme.txt", "b/"]:
            zf.writestr(n, b"x")
    with zipfile.ZipFile(z) as zf:
        assert archive.list_pages(zf) == ["b/page2.png", "b/page10.png"]


def test_png4_roundtrip():
    rng = np.random.default_rng(0)
    for w in (1, 7, 64):
        idx = rng.integers(0, 16, (5, w), dtype=np.uint8)
        im = Image.open(__import__("io").BytesIO(optimize.encode_png4(idx)))
        assert im.size == (w, 5)
        assert np.array_equal(np.asarray(im.convert("L")), idx * 17)


def test_fit_size():
    assert optimize.fit_size((2000, 3000), (1072, 1448)) == (965, 1448)
    assert optimize.fit_size((3000, 1000), (1072, 1448)) == (1072, 357)
    assert optimize.fit_size((100, 100), (1072, 1448), upscale=False) == (100, 100)


def test_reading_order_rtl():
    # Tall panel on the right, two stacked on the left, wide strip below.
    tall, lt, lb, wide = (600, 0, 1000, 700), (0, 0, 580, 340), (0, 360, 580, 700), (0, 720, 1000, 1000)
    assert panels.reading_order([wide, lb, tall, lt], rtl=True) == [tall, lt, lb, wide]
    assert panels.reading_order([wide, lb, tall, lt], rtl=False) == [lt, lb, tall, wide]
    # 2x2 grid.
    tr, tl, br, bl = (500, 0, 1000, 500), (0, 0, 480, 500), (500, 520, 1000, 1000), (0, 520, 480, 1000)
    assert panels.reading_order([bl, br, tl, tr]) == [tr, tl, br, bl]


def test_merge_and_drop():
    page = (1000, 1000)
    boxes = [(0, 0, 500, 500), (10, 10, 490, 520), (600, 600, 610, 610)]
    kept = panels.merge_overlapping(panels.drop_tiny(boxes, page))
    assert kept == [(0, 0, 500, 520)]


def test_low_confidence():
    page = (1000, 1000)
    assert panels.process_boxes([], page).reason
    assert panels.process_boxes([(0, 0, 1000, 990)], page).reason
    many = [(x, y, x + 90, y + 90) for x in range(0, 1000, 100) for y in range(0, 300, 100)]
    assert panels.process_boxes(many, page).reason
    ok = panels.process_boxes([(0, 0, 1000, 480), (0, 520, 1000, 1000)], page)
    assert ok.reason is None and len(ok.boxes) == 2


@pytest.mark.parametrize("detect", [True, False])
def test_build(tmp_path, monkeypatch, detect):
    monkeypatch.setattr(panels, "find_kumiko", lambda *_: None)   # use the OpenCV fallback
    src = tmp_path / "in.cbz"
    expected = sample.make(str(src))
    out = tmp_path / "out.zip"
    cfg = Config(screen=(600, 800), detect=detect, jobs=2)
    build(src, out, cfg)
    with zipfile.ZipFile(out) as zf:
        m = json.loads(zf.read("manifest.json"))
        assert m["version"] == 2 and m["screen"] == [600, 800] and m["direction"] == "rtl"
        assert len(m["pages"]) == len(expected)
        for page in m["pages"]:
            name = page["image"]
            assert zf.getinfo(name).compress_type == zipfile.ZIP_STORED
            im = Image.open(zf.open(name))
            assert im.width <= 600 and im.height <= 800
            assert max(im.size[0] / 600, im.size[1] / 800) > 0.99   # fits one side
            for p in page["panels"]:    # panels are rectangles inside the page image
                x, y, w, h = p["rect"]
                assert w > 0 and h > 0 and x >= 0 and y >= 0 and x + w <= im.width and y + h <= im.height
        if detect:
            assert m["pages"][0]["panels"] == []           # cover: whole page
            exp = list(expected.values())
            for page, boxes in zip(m["pages"][1:], exp[1:]):
                got = [p["box"] for p in page["panels"]]
                assert len(got) == len(boxes)
                for g, e in zip(got, boxes):   # same order, roughly same place
                    assert all(abs(a - b) < 80 for a, b in zip(g, e)), (g, e)
        else:
            assert all(p["panels"] == [] for p in m["pages"])


def _opencv4():
    import cv2
    return cv2.__version__.startswith("4.")


@pytest.mark.skipif(not panels.BUNDLED_KUMIKO.is_file(), reason="kumiko submodule not checked out")
@pytest.mark.skipif(not _opencv4(), reason="Kumiko needs OpenCV 4.x")
def test_build_with_kumiko(tmp_path):
    src = tmp_path / "in.cbz"
    expected = sample.make(str(src))
    out = tmp_path / "out.zip"
    build(src, out, Config(screen=(600, 800), jobs=2))   # finds the bundled Kumiko
    with zipfile.ZipFile(out) as zf:
        m = json.loads(zf.read("manifest.json"))
    assert m["pages"][0]["panels"] == []
    for page, boxes in zip(m["pages"][1:], list(expected.values())[1:]):
        got = [p["box"] for p in page["panels"]]
        assert len(got) == len(boxes)
        for g, e in zip(got, boxes):
            assert all(abs(a - b) < 80 for a, b in zip(g, e)), (g, e)


def test_page_map_rects():
    # White margins are cropped, so source boxes shift and scale into the stored page.
    img = Image.new("L", (1000, 1500), 255)
    img.paste(0, (100, 200, 900, 1300))
    opts = optimize.Options(screen=(500, 750))
    png, m = optimize.optimize_page(img, opts)
    assert m.size[0] <= 500 and m.size[1] <= 750
    x, y, w, h = m.rect((100, 200, 900, 1300))
    assert w >= m.size[0] - 20 and h >= m.size[1] - 20   # inside the 8px pad
    assert 0 <= x < 20 and 0 <= y < 20
    x, y, w, h = m.rect((0, 0, 5000, 5000))               # clamped to the image
    assert (x, y) == (0, 0) and (w, h) == m.size
