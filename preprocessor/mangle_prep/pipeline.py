"""Turn a ZIP/CBZ of manga pages into a Kindle-ready ZIP with manifest.json."""

from __future__ import annotations

import json
import os
import sys
import tempfile
import zipfile
from concurrent.futures import ProcessPoolExecutor
from dataclasses import dataclass, field
from pathlib import Path

from PIL import Image

from . import archive, optimize, panels

MANIFEST_VERSION = 1


@dataclass
class Config:
    screen: tuple[int, int] = (1072, 1448)
    direction: str = "rtl"
    title: str | None = None
    detect: bool = True             # False = full pages only (build step 1)
    kumiko: str | None = None       # path to Kumiko CLI; None = auto-detect
    kumiko_python: str | None = None  # interpreter for Kumiko; default: this one
    panel_pad: int = 6              # source pixels added around each panel crop
    jobs: int | None = None
    opts: optimize.Options = field(default_factory=optimize.Options)
    log: object = sys.stderr


def _process_page(args) -> dict:
    """Worker: optimise one page and its panels. Runs in a separate process."""
    index, path, boxes, cfg = args
    stem = f"p{index:03d}"
    files: dict[str, bytes] = {}
    with Image.open(path) as im:
        im.load()
        gray = optimize.to_gray(im)
    page_png, _ = optimize.optimize(gray, cfg.opts, crop=True)
    files[f"{stem}.png"] = page_png
    entries = []
    w, h = gray.size
    for n, (x1, y1, x2, y2) in enumerate(boxes, 1):
        p = cfg.panel_pad
        crop = gray.crop((max(0, x1 - p), max(0, y1 - p), min(w, x2 + p), min(h, y2 + p)))
        png, _ = optimize.optimize(crop, cfg.opts, crop=False)
        name = f"{stem}_{n}.png"
        files[name] = png
        entries.append({"image": name, "box": [x1, y1, x2, y2]})
    return {"image": f"{stem}.png", "panels": entries, "_files": files}


def detect_all(pages: list[Path], cfg: Config) -> list[list[panels.Box]]:
    """Raw panel boxes for each page (full-resolution originals)."""
    if not cfg.detect:
        return [[] for _ in pages]
    kumiko = panels.find_kumiko(cfg.kumiko)
    if kumiko:
        print(f"Detecting panels with Kumiko ({kumiko})...", file=cfg.log)
        try:
            found = panels.detect_kumiko(kumiko, pages[0].parent, cfg.kumiko_python)
            return [found.get(p.name, []) for p in pages]
        except RuntimeError as e:
            if cfg.kumiko or os.environ.get("KUMIKO"):
                raise   # explicitly requested: don't silently switch detectors
            last = str(e).strip().splitlines()[-1]
            print(f"Kumiko failed ({last}); using the built-in OpenCV detector. "
                  "Kumiko needs OpenCV 4.x (see requirements.txt).", file=cfg.log)
    else:
        print("Kumiko not found (use --kumiko or $KUMIKO); using the built-in OpenCV detector.",
              file=cfg.log)
    return [panels.detect_opencv(p) for p in pages]


def build(input_path: str | Path, output_path: str | Path, cfg: Config) -> dict:
    input_path = Path(input_path)
    rtl = cfg.direction == "rtl"
    cfg.opts.screen = cfg.screen
    with tempfile.TemporaryDirectory(prefix="mangle-") as tmp:
        pages = archive.extract_pages(input_path, Path(tmp) / "pages")
        print(f"{len(pages)} pages", file=cfg.log)

        raw = detect_all(pages, cfg)
        jobs = []
        for i, (path, boxes) in enumerate(zip(pages, raw), 1):
            if cfg.detect:
                with Image.open(path) as im:
                    size = im.size
                res = panels.process_boxes(boxes, size, rtl)
                if res.reason:
                    print(f"  p{i:03d}: whole page ({res.reason})", file=cfg.log)
                boxes = res.boxes
            jobs.append((i, path, boxes, cfg))

        manifest = {
            "version": MANIFEST_VERSION,
            "title": cfg.title or input_path.stem,
            "direction": cfg.direction,
            "screen": list(cfg.screen),
            "pages": [],
        }
        output_path = Path(output_path)
        tmp_out = output_path.with_name(output_path.name + ".part")
        log_cfg, cfg.log = cfg.log, None   # file handles don't pickle
        try:
            with zipfile.ZipFile(tmp_out, "w") as zf, ProcessPoolExecutor(cfg.jobs) as pool:
                for n, page in enumerate(pool.map(_process_page, jobs), 1):
                    for name, data in page.pop("_files").items():
                        # PNGs are already compressed: store them so the
                        # Kindle reads them without inflating.
                        zf.writestr(name, data, compress_type=zipfile.ZIP_STORED)
                    manifest["pages"].append(page)
                    print(f"\r  optimised {n}/{len(jobs)}", end="", file=log_cfg, flush=True)
                print(file=log_cfg)
                zf.writestr("manifest.json", json.dumps(manifest, indent=1),
                            compress_type=zipfile.ZIP_DEFLATED)
            tmp_out.replace(output_path)
        finally:
            cfg.log = log_cfg
            tmp_out.unlink(missing_ok=True)

    n_panels = sum(len(p["panels"]) for p in manifest["pages"])
    whole = sum(1 for p in manifest["pages"] if not p["panels"])
    print(f"Wrote {output_path}: {len(manifest['pages'])} pages, {n_panels} panels, "
          f"{whole} whole-page", file=cfg.log)
    return manifest
