"""Generate a synthetic manga CBZ for tests and quick manual checks.

    python tests/sample.py out.cbz
"""

from __future__ import annotations

import io
import random
import sys
import zipfile

from PIL import Image, ImageDraw

W, H = 1600, 2400
M = 80   # outer margin
G = 40   # gutter

# Layouts as fractions of the inner page area: (x1, y1, x2, y2).
LAYOUTS = {
    # Three rows: one wide, two side by side, one wide.
    "rows": [(0, 0, 1, .3), (.5, .3, 1, .65), (0, .3, .5, .65), (0, .65, 1, 1)],
    # Tall panel on the right, stack of two on the left, wide strip below.
    "tall": [(.55, 0, 1, .7), (0, 0, .55, .35), (0, .35, .55, .7), (0, .7, 1, 1)],
    # 2x2 grid.
    "grid": [(.5, 0, 1, .5), (0, 0, .5, .5), (.5, .5, 1, 1), (0, .5, .5, 1)],
}


def panel_px(f):
    iw, ih = W - 2 * M, H - 2 * M
    x1, y1, x2, y2 = f
    return (M + round(x1 * iw) + (G // 2 if x1 > 0 else 0), M + round(y1 * ih) + (G // 2 if y1 > 0 else 0),
            M + round(x2 * iw) - (G // 2 if x2 < 1 else 0), M + round(y2 * ih) - (G // 2 if y2 < 1 else 0))


def draw_page(layout: str | None, label: str, rng: random.Random) -> tuple[Image.Image, list]:
    img = Image.new("RGB", (W, H), "white")
    d = ImageDraw.Draw(img)
    boxes = []
    if layout is None:   # cover / splash: art bleeding to the edges
        for _ in range(60):
            x, y = rng.randrange(W), rng.randrange(H)
            r = rng.randrange(40, 300)
            d.ellipse((x - r, y - r, x + r, y + r), fill=tuple(rng.randrange(256) for _ in range(3)))
        d.text((W // 2, H // 2), label, fill="black")
        return img, boxes
    for n, f in enumerate(LAYOUTS[layout], 1):
        b = panel_px(f)
        boxes.append(b)
        d.rectangle(b, outline="black", width=6)
        x1, y1, x2, y2 = b
        for _ in range(12):
            x, y = rng.randrange(x1 + 20, x2 - 20), rng.randrange(y1 + 20, y2 - 20)
            r = rng.randrange(10, 60)
            d.ellipse((x - r, y - r, x + r, y + r), fill=(rng.randrange(40, 220),) * 3)
        d.text(((x1 + x2) // 2, (y1 + y2) // 2), f"{label}#{n}", fill="black")
    # Page number speck in the margin: should not become a panel.
    d.text((W // 2, H - M // 2), label, fill="black")
    return img, boxes


def make(path: str, seed: int = 1) -> dict[str, list]:
    """Write a CBZ; return {member name: expected boxes in reading order}."""
    rng = random.Random(seed)
    expected = {}
    pages = [None, "rows", "tall", "grid", "rows", "tall"] + ["grid"] * 6
    with zipfile.ZipFile(path, "w") as zf:
        zf.writestr("__MACOSX/._page1.jpg", b"junk")
        zf.writestr("Vol/notes.txt", b"not an image")
        for i, layout in enumerate(pages, 1):
            img, boxes = draw_page(layout, f"P{i}", rng)
            buf = io.BytesIO()
            fmt, ext = (("PNG", "png") if i % 3 == 0 else ("JPEG", "jpg"))
            img.save(buf, fmt, quality=90)
            name = f"Vol/page{i}.{ext}"   # unpadded on purpose: tests natural sort
            zf.writestr(name, buf.getvalue())
            expected[name] = boxes
    return expected


if __name__ == "__main__":
    make(sys.argv[1] if len(sys.argv) > 1 else "sample.cbz")
