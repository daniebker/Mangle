"""Write a ZIP of PNGs in every colour type and bit depth (for build/tests).

    python test/make_pngs.py out.zip
"""

import io
import random
import sys
import zipfile

from PIL import Image


def picture(w, h, seed):
    rng = random.Random(seed)
    img = Image.new("RGBA", (w, h), (255, 255, 255, 255))
    px = img.load()
    for y in range(h):
        for x in range(w):
            px[x, y] = ((x * 5 + y) & 255, (y * 3) & 255, rng.randrange(256), (x * y) & 255)
    return img


def png(img, **kw):
    buf = io.BytesIO()
    img.save(buf, "PNG", **kw)
    return buf.getvalue()


def main(out):
    w, h = 37, 23   # odd sizes: partial bytes at the end of rows
    base = picture(w, h, 1)
    files = {
        "rgba.png": png(base),
        "rgb.png": png(base.convert("RGB")),
        "gray.png": png(base.convert("L")),
        "gray_alpha.png": png(base.convert("LA")),
        "gray16.png": png(base.convert("L").convert("I").point(lambda v: v * 257).convert("I;16")),
        "bilevel.png": png(base.convert("1")),
        "pal8.png": png(base.convert("RGB").convert("P", palette=Image.ADAPTIVE, colors=200)),
        "pal4.png": png(base.convert("RGB").convert("P", palette=Image.ADAPTIVE, colors=16), bits=4),
        "pal2.png": png(base.convert("RGB").convert("P", palette=Image.ADAPTIVE, colors=4), bits=2),
        "big.png": png(picture(700, 90, 2).convert("L"), compress_level=9),
    }
    pal_t = base.convert("RGB").convert("P", palette=Image.ADAPTIVE, colors=32)
    files["pal_trns.png"] = png(pal_t, transparency=bytes(range(0, 256, 8)))
    with zipfile.ZipFile(out, "w") as zf:
        for i, (name, data) in enumerate(files.items()):
            # Half the entries deflated: the reader inflates ZIP entries too.
            zf.writestr(name, data, compress_type=zipfile.ZIP_DEFLATED if i % 2 else zipfile.ZIP_STORED)


if __name__ == "__main__":
    main(sys.argv[1])
