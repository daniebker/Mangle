"""E-ink image optimisation: grayscale, margin crop, resize, contrast/gamma,
16-level dithering and 4-bit grayscale PNG output."""

from __future__ import annotations

import struct
import zlib
from dataclasses import dataclass

import numpy as np
from PIL import Image, ImageOps

# 16 evenly spaced gray levels: 0x00, 0x11, ... 0xFF (what Kindle e-ink shows).
LEVELS = 16
_PALETTE_IMG = Image.new("P", (1, 1))
_PALETTE_IMG.putpalette([v for i in range(LEVELS) for v in (i * 17,) * 3] + [0] * (3 * (256 - LEVELS)))


@dataclass
class Options:
    screen: tuple[int, int] = (1072, 1448)
    gamma: float = 1.8          # >1 darkens mid-tones (helps thin lines on e-ink)
    autocontrast_cutoff: float = 0.5  # percent of histogram clipped at each end
    dither: bool = True
    upscale: bool = True        # enlarge images smaller than the screen
    white_threshold: int = 235  # pixels brighter than this count as "white" for margin crop
    margin_pad: int = 8         # pixels of margin kept after cropping (in source pixels)


def to_gray(img: Image.Image) -> Image.Image:
    if img.mode in ("RGBA", "LA", "P"):
        img = img.convert("RGBA")
        bg = Image.new("RGBA", img.size, (255, 255, 255, 255))
        img = Image.alpha_composite(bg, img)
    return img.convert("L")


def content_bbox(gray: Image.Image, threshold: int = 235, min_fraction: float = 0.004):
    """Bounding box of non-white content, ignoring rows/columns that only hold
    a few stray dark pixels (scan noise, page-number specks near the edge)."""
    a = np.asarray(gray) < threshold
    h, w = a.shape
    rows = np.flatnonzero(a.sum(axis=1) > max(1, w * min_fraction))
    cols = np.flatnonzero(a.sum(axis=0) > max(1, h * min_fraction))
    if rows.size == 0 or cols.size == 0:
        return None
    return int(cols[0]), int(rows[0]), int(cols[-1]) + 1, int(rows[-1]) + 1


def crop_box(gray: Image.Image, opts: Options) -> tuple[int, int, int, int]:
    """The region crop_margins keeps (the whole image if cropping would leave a sliver)."""
    w, h = gray.size
    whole = (0, 0, w, h)
    box = content_bbox(gray, opts.white_threshold)
    if box is None:
        return whole
    x1, y1, x2, y2 = box
    p = opts.margin_pad
    x1, y1, x2, y2 = max(0, x1 - p), max(0, y1 - p), min(w, x2 + p), min(h, y2 + p)
    # Don't crop if it would leave a sliver (blank or nearly blank page).
    if (x2 - x1) < w * 0.3 or (y2 - y1) < h * 0.3:
        return whole
    return x1, y1, x2, y2


def crop_margins(gray: Image.Image, opts: Options) -> Image.Image:
    box = crop_box(gray, opts)
    return gray if box == (0, 0, *gray.size) else gray.crop(box)


def fit_size(size: tuple[int, int], screen: tuple[int, int], upscale: bool = True) -> tuple[int, int]:
    w, h = size
    sw, sh = screen
    scale = min(sw / w, sh / h)
    if not upscale:
        scale = min(scale, 1.0)
    return max(1, round(w * scale)), max(1, round(h * scale))


def tone(gray: Image.Image, opts: Options) -> Image.Image:
    if opts.autocontrast_cutoff is not None and opts.autocontrast_cutoff >= 0:
        gray = ImageOps.autocontrast(gray, cutoff=opts.autocontrast_cutoff)
    if opts.gamma and opts.gamma != 1.0:
        lut = [round(255 * (i / 255) ** opts.gamma) for i in range(256)]
        gray = gray.point(lut)
    return gray


def quantize(gray: Image.Image, dither: bool = True) -> np.ndarray:
    """Return a uint8 array of palette indices 0..15 (gray value = index * 17)."""
    method = Image.Dither.FLOYDSTEINBERG if dither else Image.Dither.NONE
    q = gray.convert("RGB").quantize(palette=_PALETTE_IMG, dither=method)
    return np.asarray(q, dtype=np.uint8)


def encode_png4(indices: np.ndarray) -> bytes:
    """Encode 0..15 values as a 4-bit grayscale PNG (colour type 0, depth 4)."""
    h, w = indices.shape
    if w % 2:
        indices = np.pad(indices, ((0, 0), (0, 1)))
    packed = (indices[:, 0::2] << 4) | indices[:, 1::2]
    # Filter type 0 (None) per scanline. Dithered images gain little from
    # other filters and this keeps encoding fast.
    raw = np.concatenate([np.zeros((h, 1), np.uint8), packed.astype(np.uint8)], axis=1).tobytes()

    def chunk(tag: bytes, data: bytes) -> bytes:
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data))

    ihdr = struct.pack(">IIBBBBB", w, h, 4, 0, 0, 0, 0)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr)
            + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


@dataclass
class PageMap:
    """Where the source page ended up in the optimised image: source pixel
    (x, y) lands at ((x - x0) * sx, (y - y0) * sy)."""
    x0: int = 0
    y0: int = 0
    sx: float = 1.0
    sy: float = 1.0
    size: tuple[int, int] = (0, 0)

    def rect(self, box: tuple[int, int, int, int]) -> list[int]:
        """Map a source-pixel box to [x, y, w, h] in the optimised image."""
        x1 = min(max(0, int(round((box[0] - self.x0) * self.sx))), self.size[0] - 1)
        y1 = min(max(0, int(round((box[1] - self.y0) * self.sy))), self.size[1] - 1)
        x2 = min(max(x1 + 1, int(round((box[2] - self.x0) * self.sx))), self.size[0])
        y2 = min(max(y1 + 1, int(round((box[3] - self.y0) * self.sy))), self.size[1])
        return [x1, y1, x2 - x1, y2 - y1]


def optimize_page(gray: Image.Image, opts: Options) -> tuple[bytes, PageMap]:
    """Page pipeline: crop margins, fit to the screen, tone, quantise.
    Returns (png, map from source pixels to the output image)."""
    x1, y1, x2, y2 = crop_box(gray, opts)
    cropped = gray.crop((x1, y1, x2, y2)) if (x1, y1, x2, y2) != (0, 0, *gray.size) else gray
    size = fit_size(cropped.size, opts.screen, opts.upscale)
    if size != cropped.size:
        cropped = cropped.resize(size, Image.Resampling.LANCZOS)
    cropped = tone(cropped, opts)
    png = encode_png4(quantize(cropped, opts.dither))
    return png, PageMap(x1, y1, size[0] / (x2 - x1), size[1] / (y2 - y1), size)


def optimize(gray: Image.Image, opts: Options, crop: bool) -> tuple[bytes, tuple[int, int]]:
    """Full Kindle pipeline for an already-grayscale image. Returns (png, size)."""
    if crop:
        gray = crop_margins(gray, opts)
    size = fit_size(gray.size, opts.screen, opts.upscale)
    if size != gray.size:
        gray = gray.resize(size, Image.Resampling.LANCZOS)
    gray = tone(gray, opts)
    return encode_png4(quantize(gray, opts.dither)), size
