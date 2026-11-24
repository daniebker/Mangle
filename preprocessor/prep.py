#!/usr/bin/env python3
"""Preprocess a manga ZIP/CBZ for the Mangle Kindle reader.

    prep.py input.cbz -o output.zip --screen 1072x1448 --direction rtl
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


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("input", nargs="+", help="manga .zip/.cbz file(s)")
    ap.add_argument("-o", "--output", help="output ZIP (or folder when several inputs are given)")
    ap.add_argument("--screen", type=screen_size, default=(1072, 1448),
                    help="target screen WIDTHxHEIGHT in portrait (default 1072x1448)")
    ap.add_argument("--direction", choices=["rtl", "ltr"], default="rtl",
                    help="reading direction (default rtl, manga)")
    ap.add_argument("--title", help="title stored in the manifest (default: input file name)")
    ap.add_argument("--no-panels", action="store_true", help="skip panel detection (page mode only)")
    ap.add_argument("--kumiko", help="path to the Kumiko checkout or its `kumiko` script")
    ap.add_argument("--kumiko-python", help="Python interpreter to run Kumiko with (default: this one)")
    ap.add_argument("--gamma", type=float, default=1.8, help="gamma for e-ink (default 1.8, 1 = off)")
    ap.add_argument("--no-dither", action="store_true", help="quantise to 16 grays without dithering")
    ap.add_argument("-j", "--jobs", type=int, help="worker processes (default: CPU count)")
    a = ap.parse_args(argv)

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
            detect=not a.no_panels, kumiko=a.kumiko, kumiko_python=a.kumiko_python, jobs=a.jobs,
            opts=Options(gamma=a.gamma, dither=not a.no_dither),
        )
        print(f"== {src}", file=sys.stderr)
        build(src, out, cfg)
    return 0


if __name__ == "__main__":
    sys.exit(main())
