#!/usr/bin/env python3
"""Desktop preview of a preprocessed book: step through panels in manifest order.

    preview.py book.zip [--source original.cbz] [-o preview_dir] [--open]

Writes a folder with the book's images and an index.html. Open it in a
browser and use the arrow keys (left = forward for RTL books, like the
Kindle tap zones), Space to switch between panel and page mode.

With --source, each original page is also drawn with its detected panel
boxes numbered in reading order, shown beside the current panel, which is
the quickest way to check detection and ordering.
"""

from __future__ import annotations

import argparse
import json
import sys
import webbrowser
import zipfile
from pathlib import Path

from mangle_prep import archive

HTML = r"""<!doctype html>
<html><head><meta charset="utf-8"><title>__TITLE__ — preview</title>
<style>
 body{margin:0;background:#333;color:#eee;font:14px system-ui,sans-serif;height:100vh;display:flex;flex-direction:column}
 #bar{padding:6px 12px;background:#222;display:flex;gap:16px;align-items:center}
 #main{flex:1;display:flex;gap:12px;padding:12px;min-height:0;justify-content:center}
 .pane{display:flex;flex-direction:column;align-items:center;min-height:0;flex:1}
 .pane canvas,.pane img{max-width:100%;max-height:100%;object-fit:contain;background:#fff}
 .pane small{opacity:.7;margin-bottom:4px}
 #src{display:none}
 button{background:#444;color:#eee;border:1px solid #666;border-radius:4px;padding:4px 10px;cursor:pointer}
</style></head><body>
<div id="bar"><b>__TITLE__</b><span id="pos"></span><span style="flex:1"></span>
 <button id="fwd"></button><button id="mode">Toggle mode (Space)</button><button id="back"></button></div>
<div id="main">
 <div class="pane" id="src"><small>original + detected boxes</small><img id="srcimg"></div>
 <div class="pane"><small>Kindle image (as shown)</small><canvas id="cv" style="max-width:100%;max-height:100%;object-fit:contain;background:#fff"></canvas></div>
</div>
<script>
const M = __MANIFEST__, SRC = __SOURCES__, rtl = M.direction === "rtl";
let page = 0, panel = 0, mode = "panel";
const n = p => M.pages[p].panels.length;
const cache = {};
function draw(name, r) {
  const cv = document.getElementById("cv");
  const go = im => { const [x, y, w, h] = r || [0, 0, im.naturalWidth, im.naturalHeight];
    cv.width = w; cv.height = h; cv.getContext("2d").drawImage(im, x, y, w, h, 0, 0, w, h); };
  if (cache[name]) return go(cache[name]);
  const im = new Image(); im.onload = () => { cache[name] = im; go(im); }; im.src = name;
}
function show() {
  const pg = M.pages[page], usePanel = mode === "panel" && n(page);
  draw(pg.image, usePanel ? pg.panels[panel].rect : null);
  document.getElementById("pos").textContent =
    `${mode} mode · page ${page + 1}/${M.pages.length}` +
    (usePanel ? ` · panel ${panel + 1}/${n(page)}` : n(page) ? "" : " · whole page");
  if (SRC.length) { document.getElementById("src").style.display = "flex";
                    document.getElementById("srcimg").src = SRC[page]; }
}
function step(d) {
  if (mode === "panel" && n(page)) {
    const k = panel + d;
    if (k >= 0 && k < n(page)) { panel = k; return show(); }
  }
  const p = page + d;
  if (p < 0 || p >= M.pages.length) return;
  page = p; panel = d > 0 ? 0 : Math.max(0, n(page) - 1); show();
}
function toggle() { mode = mode === "panel" ? "page" : "panel"; panel = 0; show(); }
document.getElementById("fwd").textContent = rtl ? "◀ forward" : "◀ back";
document.getElementById("back").textContent = rtl ? "back ▶" : "forward ▶";
document.getElementById("fwd").onclick = () => step(rtl ? 1 : -1);
document.getElementById("back").onclick = () => step(rtl ? -1 : 1);
document.getElementById("mode").onclick = toggle;
document.onkeydown = e => {
  if (e.key === "ArrowLeft") step(rtl ? 1 : -1);
  else if (e.key === "ArrowRight") step(rtl ? -1 : 1);
  else if (e.key === " ") { e.preventDefault(); toggle(); }
};
show();
</script></body></html>
"""


def annotate_sources(source: Path, manifest: dict, out: Path) -> list[str]:
    """Draw numbered panel boxes on the original pages."""
    import cv2
    import numpy as np

    names = []
    with zipfile.ZipFile(source) as zf:
        members = archive.list_pages(zf)
        if len(members) != len(manifest["pages"]):
            print(f"warning: source has {len(members)} pages, book has {len(manifest['pages'])}",
                  file=sys.stderr)
        for i, (member, page) in enumerate(zip(members, manifest["pages"]), 1):
            img = cv2.imdecode(np.frombuffer(zf.read(member), np.uint8), cv2.IMREAD_COLOR)
            if img is None:
                continue
            t = max(2, img.shape[1] // 300)
            for k, p in enumerate(page["panels"], 1):
                x1, y1, x2, y2 = p["box"]
                cv2.rectangle(img, (x1, y1), (x2, y2), (0, 0, 255), t)
                cv2.putText(img, str(k), (x1 + 6 * t, y1 + 30 * t), cv2.FONT_HERSHEY_SIMPLEX,
                            t, (0, 0, 255), 2 * t)
            if not page["panels"]:
                cv2.putText(img, "whole page", (20, 60), cv2.FONT_HERSHEY_SIMPLEX, 2, (0, 0, 255), 4)
            name = f"src_{i:03d}.jpg"
            cv2.imwrite(str(out / name), img, [cv2.IMWRITE_JPEG_QUALITY, 80])
            names.append(name)
    return names


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("book", help="ZIP produced by prep.py")
    ap.add_argument("--source", help="the original CBZ/ZIP, to show detected boxes")
    ap.add_argument("-o", "--outdir", help="output folder (default: <book>_preview)")
    ap.add_argument("--open", action="store_true", help="open the preview in a browser")
    a = ap.parse_args(argv)

    book = Path(a.book)
    out = Path(a.outdir or book.with_name(book.stem + "_preview"))
    out.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(book) as zf:
        manifest = json.loads(zf.read("manifest.json"))
        for name in zf.namelist():
            # Only write the flat names the preprocessor produces.
            if "/" not in name and "\\" not in name and not name.startswith("."):
                (out / name).write_bytes(zf.read(name))
    sources = annotate_sources(Path(a.source), manifest, out) if a.source else []

    html = (HTML.replace("__MANIFEST__", json.dumps(manifest))
                .replace("__SOURCES__", json.dumps(sources))
                .replace("__TITLE__", str(manifest.get("title", book.stem)).replace("<", "&lt;")))
    index = out / "index.html"
    index.write_text(html, encoding="utf-8")
    print(index)
    if a.open:
        webbrowser.open(index.resolve().as_uri())
    return 0


if __name__ == "__main__":
    sys.exit(main())
