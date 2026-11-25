#!/usr/bin/env python3
"""Preprocess a manga ZIP/CBZ/PDF for the Mangle Kindle reader.

    prep.py input.cbz -o output.zip --screen 1072x1448 --direction rtl
    prep.py input.cbz -o output.zip --device cyd      # Cheap Yellow Display firmware
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

from mangle_prep.optimize import Options
from mangle_prep.pipeline import Config, build


def screen_size(s: str) -> tuple[int, int]:
    try:
        w, h = (int(v) for v in s.lower().split("x"))
    except ValueError:
        raise argparse.ArgumentTypeError("expected WIDTHxHEIGHT, e.g. 1072x1448")
    if w <= 0 or h <= 0:
        raise argparse.ArgumentTypeError("screen size must be positive")
    return w, h


# Defaults per reading device: (screen, gamma, dither). The CYD's LCD needs no
# e-ink gamma or dithering, and pages are stored at twice its 320x480 so that
# panels and zoom have detail to show.
DEVICES = {
    "kindle": ((1072, 1448), 1.8, True),
    "cyd": ((640, 960), 1.0, False),
}


def apply_device(a: argparse.Namespace) -> None:
    """Fill the options left unset on the command line from the device's defaults."""
    screen, gamma, dither = DEVICES[a.device]
    if a.screen is None:
        a.screen = screen
    if a.gamma is None:
        a.gamma = gamma
    if a.dither is None:
        a.dither = dither


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("input", nargs="+", help="manga .zip/.cbz/.pdf file(s)")
    ap.add_argument("-o", "--output", help="output ZIP (or folder when several inputs are given)")
    ap.add_argument("--device", choices=sorted(DEVICES), default="kindle",
                    help="reader the book is for: sets the defaults for --screen, --gamma and dithering "
                         "(kindle: 1072x1448, gamma 1.8, dithered; cyd: 640x960, gamma 1, no dithering)")
    ap.add_argument("--screen", type=screen_size,
                    help="target screen WIDTHxHEIGHT in portrait (default from --device)")
    ap.add_argument("--direction", choices=["rtl", "ltr"], default="rtl",
                    help="reading direction (default rtl, manga)")
    ap.add_argument("--title", help="title stored in the manifest (default: input file name)")
    ap.add_argument("--no-panels", action="store_true", help="skip panel detection (page mode only)")
    ap.add_argument("--kumiko", help="path to the Kumiko checkout or its `kumiko` script")
    ap.add_argument("--kumiko-python", help="Python interpreter to run Kumiko with (default: this one)")
    ap.add_argument("--gamma", type=float, help="gamma for e-ink (default from --device, 1 = off)")
    ap.add_argument("--no-dither", dest="dither", action="store_false", default=None,
                    help="quantise to 16 grays without dithering")
    ap.add_argument("--dither", dest="dither", action="store_true", help="Floyd-Steinberg dithering to 16 grays")
    ap.add_argument("--pdf-dpi", type=int, default=200, help="render resolution for PDF input (default 200)")
    ap.add_argument("-j", "--jobs", type=int, help="worker processes (default: CPU count)")
    a = ap.parse_args(argv)
    apply_device(a)

    if len(a.input) > 1 and a.output and not Path(a.output).is_dir():
        ap.error("with several inputs, -o must be an existing folder")

    for inp in a.input:
        src = Path(inp)
        if a.output and Path(a.output).is_dir():
            out = Path(a.output) / f"{src.stem}.zip"
        elif a.output:
            out = Path(a.output)
        else:
            out = src.with_name(f"{src.stem}.kindle.zip")
        if out.resolve() == src.resolve():
            ap.error(f"output would overwrite the input: {out}")
        cfg = Config(
            screen=a.screen, direction=a.direction, title=a.title if len(a.input) == 1 else None,
            detect=not a.no_panels, kumiko=a.kumiko, kumiko_python=a.kumiko_python, jobs=a.jobs, pdf_dpi=a.pdf_dpi,
            opts=Options(gamma=a.gamma, dither=a.dither),
        )
        print(f"== {src}", file=sys.stderr)
        build(src, out, cfg)
    return 0


if __name__ == "__main__":
    sys.exit(main())
