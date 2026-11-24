"""Panel detection, cleanup, reading-order sort and confidence checks.

Boxes are (x1, y1, x2, y2) in original page pixels throughout.
"""

from __future__ import annotations

import json
import os
import shutil
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

Box = tuple[int, int, int, int]


# --------------------------------------------------------------------------
# Detection
# --------------------------------------------------------------------------

# The git submodule (or the copy bundled in the release zip).
BUNDLED_KUMIKO = Path(__file__).resolve().parents[1] / "kumiko" / "kumiko"


def find_kumiko(explicit: str | None = None) -> str | None:
    """Locate the Kumiko CLI script: --kumiko, $KUMIKO, the bundled
    submodule, or `kumiko` on PATH."""
    for cand in (explicit, os.environ.get("KUMIKO")):
        if cand:
            p = Path(cand)
            if p.is_dir():
                p = p / "kumiko"
            if p.is_file():
                return str(p.resolve())
            raise FileNotFoundError(f"Kumiko not found at {cand}")
    if BUNDLED_KUMIKO.is_file():
        return str(BUNDLED_KUMIKO)
    return shutil.which("kumiko")


def detect_kumiko(kumiko: str, page_dir: Path, python: str | None = None) -> dict[str, list[Box]]:
    """Run Kumiko on a folder of pages. Returns {filename: [boxes]}.

    Kumiko is run as a separate program rather than imported: its `lib`
    package name would clash, and it must run from its own directory.
    Kumiko needs OpenCV 4.x: it crashes on OpenCV 5's line detector output.
    """
    cmd = [python or sys.executable, kumiko, "-i", str(page_dir.resolve())]
    res = subprocess.run(cmd, cwd=Path(kumiko).parent, capture_output=True, text=True)
    if res.returncode != 0:
        raise RuntimeError(f"Kumiko failed ({res.returncode}):\n{res.stderr.strip()}")
    # Kumiko may print warnings before the JSON; take the last JSON line.
    line = next((l for l in reversed(res.stdout.splitlines()) if l.startswith("[")), None)
    if line is None:
        raise RuntimeError(f"Kumiko produced no JSON:\n{res.stdout}\n{res.stderr}")
    out = {}
    for info in json.loads(line):
        boxes = [(x, y, x + w, y + h) for x, y, w, h in info.get("panels", [])]
        out[os.path.basename(info["filename"])] = boxes
    return out


def detect_opencv(path: Path) -> list[Box]:
    """Fallback detector used when Kumiko isn't available.

    Treats bright gutters as separators: threshold, close small gaps, and take
    the bounding boxes of the outer contours. Much cruder than Kumiko, but fine
    for clean manga layouts with white gutters.
    """
    import cv2
    import numpy as np

    img = cv2.imread(str(path), cv2.IMREAD_GRAYSCALE)
    if img is None:
        raise ValueError(f"cannot read {path}")
    h, w = img.shape
    _, ink = cv2.threshold(img, 225, 255, cv2.THRESH_BINARY_INV)
    k = max(3, (min(w, h) // 300) | 1)
    ink = cv2.morphologyEx(ink, cv2.MORPH_CLOSE, np.ones((k, k), np.uint8))
    contours, _ = cv2.findContours(ink, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
    boxes = []
    for c in contours:
        x, y, bw, bh = cv2.boundingRect(c)
        boxes.append((x, y, x + bw, y + bh))
    return boxes


# --------------------------------------------------------------------------
# Cleanup
# --------------------------------------------------------------------------

def area(b: Box) -> int:
    return max(0, b[2] - b[0]) * max(0, b[3] - b[1])


def intersection(a: Box, b: Box) -> int:
    return area((max(a[0], b[0]), max(a[1], b[1]), min(a[2], b[2]), min(a[3], b[3])))


def union_box(a: Box, b: Box) -> Box:
    return (min(a[0], b[0]), min(a[1], b[1]), max(a[2], b[2]), max(a[3], b[3]))


def drop_tiny(boxes: list[Box], page: tuple[int, int], min_side: float = 0.08,
              min_area: float = 0.015) -> list[Box]:
    """Remove boxes too small to be real panels (specks, SFX, page numbers)."""
    w, h = page
    return [b for b in boxes
            if (b[2] - b[0]) >= w * min_side and (b[3] - b[1]) >= h * min_side
            and area(b) >= w * h * min_area]


def merge_overlapping(boxes: list[Box], threshold: float = 0.3) -> list[Box]:
    """Merge boxes whose overlap exceeds `threshold` of the smaller box,
    repeating until nothing changes (merges can create new overlaps)."""
    boxes = list(boxes)
    changed = True
    while changed:
        changed = False
        for i in range(len(boxes)):
            for j in range(i + 1, len(boxes)):
                a, b = boxes[i], boxes[j]
                small = min(area(a), area(b))
                if small and intersection(a, b) > threshold * small:
                    boxes[i] = union_box(a, b)
                    del boxes[j]
                    changed = True
                    break
            if changed:
                break
    return boxes


def clamp(boxes: list[Box], page: tuple[int, int]) -> list[Box]:
    w, h = page
    out = []
    for x1, y1, x2, y2 in boxes:
        b = (max(0, x1), max(0, y1), min(w, x2), min(h, y2))
        if area(b):
            out.append(b)
    return out


# --------------------------------------------------------------------------
# Reading order
# --------------------------------------------------------------------------

def _overlap(a1: int, a2: int, b1: int, b2: int) -> int:
    return max(0, min(a2, b2) - max(a1, b1))


def _bands(boxes: list[Box], axis: int, frac: float = 0.3) -> list[list[Box]]:
    """Group boxes into bands along an axis (0 = columns by x, 1 = rows by y).

    A box joins the current band if it overlaps the band's extent by more
    than `frac` of its own length on that axis.
    """
    lo, hi = axis, axis + 2
    bands: list[list[Box]] = []
    extent = None
    for b in sorted(boxes, key=lambda b: (b[lo], b[hi])):
        length = b[hi] - b[lo]
        if extent and _overlap(b[lo], b[hi], *extent) > frac * length:
            bands[-1].append(b)
            extent = (extent[0], max(extent[1], b[hi]))
        else:
            bands.append([b])
            extent = (b[lo], b[hi])
    return bands


def reading_order(boxes: list[Box], rtl: bool = True) -> list[Box]:
    """Sort panels top-to-bottom, then right-to-left (or left-to-right).

    Recursive cut: split the page into rows; split each row into columns
    (ordered by reading direction); split each column into rows again. This
    handles a tall panel beside a stack of smaller ones: the column holding
    the stack is read top-to-bottom before moving on.
    """
    if len(boxes) <= 1:
        return list(boxes)
    rows = _bands(boxes, axis=1)
    if len(rows) > 1:
        return [b for row in rows for b in reading_order(row, rtl)]
    cols = _bands(boxes, axis=0)
    if len(cols) > 1:
        cols.sort(key=lambda c: max(b[2] for b in c) if rtl else min(b[0] for b in c), reverse=rtl)
        return [b for col in cols for b in reading_order(col, rtl)]
    # Can't split further (overlapping layout): plain top-down, then by direction.
    return sorted(boxes, key=lambda b: (b[1], -b[2] if rtl else b[0]))


# --------------------------------------------------------------------------
# Confidence
# --------------------------------------------------------------------------

@dataclass
class PageResult:
    boxes: list[Box]
    reason: str | None = None   # set when the page falls back to whole-page view


def process_boxes(raw: list[Box], page: tuple[int, int], rtl: bool = True,
                  max_panels: int = 12, huge: float = 0.85) -> PageResult:
    """Clean, sort and sanity-check detected boxes for one page."""
    w, h = page
    boxes = clamp(raw, page)
    kept = merge_overlapping(drop_tiny(boxes, page))
    if not kept:
        return PageResult([], "no panels found")
    if len(kept) == 1 and area(kept[0]) >= huge * w * h:
        return PageResult([], "single full-page panel")
    if len(kept) > max_panels:
        return PageResult([], f"too many panels ({len(kept)})")
    covered = sum(area(b) for b in kept) / (w * h)
    if covered < 0.35:
        return PageResult([], f"panels cover only {covered:.0%} of page")
    tiny_raw = len(boxes) - len(drop_tiny(boxes, page))
    if tiny_raw > 3 * max(1, len(kept)) and tiny_raw > 10:
        return PageResult([], f"fragmented ({tiny_raw} tiny boxes)")
    return PageResult(reading_order(kept, rtl))
