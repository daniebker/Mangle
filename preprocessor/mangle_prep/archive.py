"""Extract page images from a ZIP/CBZ and sort them in natural order."""

from __future__ import annotations

import os
import re
import zipfile
from pathlib import Path, PurePosixPath

IMAGE_EXTS = {".jpg", ".jpeg", ".png", ".webp", ".gif", ".bmp"}


def natural_key(s: str):
    """Sort key so that 'page2' comes before 'page10' (case-insensitive)."""
    return [int(t) if t.isdigit() else t.lower() for t in re.split(r"(\d+)", s)]


def _is_junk(name: str) -> bool:
    parts = PurePosixPath(name).parts
    return any(p.startswith(".") or p == "__MACOSX" for p in parts)


def list_pages(zf: zipfile.ZipFile) -> list[str]:
    """Image members of the archive, in natural reading order."""
    names = []
    for info in zf.infolist():
        if info.is_dir() or _is_junk(info.filename):
            continue
        if PurePosixPath(info.filename).suffix.lower() in IMAGE_EXTS:
            names.append(info.filename)
    return sorted(names, key=natural_key)


def extract_pdf_pages(pdf: str | os.PathLike, dest: str | os.PathLike, dpi: int = 200) -> list[Path]:
    """Render each PDF page to `dest` as page0001.png, page0002.png, ..."""
    try:
        import pypdfium2 as pdfium
    except ImportError:
        raise RuntimeError("PDF input needs pypdfium2 (pip install pypdfium2)") from None
    dest = Path(dest)
    dest.mkdir(parents=True, exist_ok=True)
    out = []
    doc = pdfium.PdfDocument(str(pdf))
    try:
        for i in range(len(doc)):
            page = doc[i]
            try:
                img = page.render(scale=dpi / 72).to_pil().convert("RGB")
            finally:
                page.close()
            target = dest / f"page{i + 1:04d}.png"
            img.save(target)
            out.append(target)
    finally:
        doc.close()
    if not out:
        raise ValueError(f"no pages found in {pdf}")
    return out


def extract_pages(archive: str | os.PathLike, dest: str | os.PathLike, dpi: int = 200) -> list[Path]:
    """Extract the image pages into `dest` as page0001.ext, page0002.ext, ...

    Files are renamed so their names sort correctly and flatten any folder
    structure; this also keeps hostile member names (../, absolute paths) out
    of the filesystem.
    """
    if Path(archive).suffix.lower() == ".pdf":
        return extract_pdf_pages(archive, dest, dpi)
    dest = Path(dest)
    dest.mkdir(parents=True, exist_ok=True)
    out = []
    with zipfile.ZipFile(archive) as zf:
        for i, name in enumerate(list_pages(zf), 1):
            ext = PurePosixPath(name).suffix.lower()
            target = dest / f"page{i:04d}{ext}"
            with zf.open(name) as src, open(target, "wb") as dst:
                while chunk := src.read(1 << 20):
                    dst.write(chunk)
            out.append(target)
    if not out:
        raise ValueError(f"no images found in {archive}")
    return out
