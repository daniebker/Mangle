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


def extract_pages(archive: str | os.PathLike, dest: str | os.PathLike) -> list[Path]:
    """Extract the image pages into `dest` as page0001.ext, page0002.ext, ...

    Files are renamed so their names sort correctly and flatten any folder
    structure; this also keeps hostile member names (../, absolute paths) out
    of the filesystem.
    """
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
