# Mangle

Read manga panel by panel on a jailbroken Kindle.

There are two parts:

1. **Preprocessor** (`preprocessor/`, Python, runs on your computer). It takes a ZIP/CBZ of
   pages, finds the panels, prepares every image for e-ink, and writes a ZIP with a
   `manifest.json`.
2. **Reader** (`reader/`, C, runs on the Kindle and is started from KUAL). It opens those ZIPs
   and shows them panel by panel or page by page.

All the heavy work (finding panels, resizing, dithering) happens on the computer. The Kindle
only decodes small PNGs that are already the right size, so page turns stay fast.

## 1. Preprocessor

```sh
cd preprocessor
pip install -r requirements.txt
python prep.py "My Manga v01.cbz" -o "My Manga v01.zip" --screen 1072x1448 --direction rtl
```

| Option | Default | |
|---|---|---|
| `--screen WxH` | `1072x1448` | Portrait screen size of the target Kindle |
| `--direction rtl\|ltr` | `rtl` | Reading direction (`ltr` for western comics) |
| `--kumiko PATH` | `$KUMIKO`, the bundled submodule, or `kumiko` on PATH | Kumiko checkout or its `kumiko` script |
| `--kumiko-python PATH` | the current Python | Python used to run Kumiko |
| `--no-panels` | | Skip panel detection (page mode only) |
| `--gamma G` | `1.8` | Darkens mid-tones for e-ink (`1` = off) |
| `--no-dither` | | Reduce to 16 grays without dithering |
| `-j N` | CPU count | Worker processes |

You can pass several input files with `-o some_folder/`.

Common screen sizes: Paperwhite 3/4, Oasis (1st gen) and Kindle 2022 → `1072x1448`;
Paperwhite 5 and Signature → `1236x1648`; Oasis 2/3 → `1264x1680`; basic Kindle (2019) →
`600x800`.
Check your model before converting a whole library.

What it does to each volume:

1. Extracts the images (JPEG, PNG, WebP, GIF, BMP), skips `__MACOSX` and hidden files, and sorts
   pages in natural order (`page2` before `page10`).
2. Finds panels on the full-resolution pages with [Kumiko](https://github.com/njean42/kumiko).
   If Kumiko isn't available or fails, it uses a simpler built-in OpenCV detector, which works
   for clean layouts with white gutters.
3. Drops tiny boxes, merges overlapping ones, and sorts panels in reading order: rows from top
   to bottom, columns right to left (RTL), and stacks inside a column from top to bottom.
4. Pages it isn't confident about (no panels, one huge panel, too many panels, low coverage,
   lots of small fragments) get an empty panel list, so the reader shows them whole. Each one
   is listed on stderr with the reason.
5. Writes a full-page image (white margins cropped) and one image per panel. Panels are cropped
   from the **full-resolution original**, not the reduced page, so zoomed panels stay sharp.
6. For every image: grayscale → resize to fit the screen (keeping the aspect ratio) →
   auto-contrast + gamma → 16 gray levels with Floyd–Steinberg dithering → **4-bit grayscale
   PNG**.
7. Packs the images (stored, not compressed again) and `manifest.json` into the output ZIP.

### Kumiko

Kumiko is a git submodule at `preprocessor/kumiko`, and `prep.py` uses it automatically. Clone
with submodules:

```sh
git clone --recurse-submodules https://github.com/daniebker/mangle
# or, in an existing checkout:
git submodule update --init
```

The submodule URL is SSH (`git@github.com:`). Without a GitHub SSH key, use
`git -c "url.https://github.com/.insteadOf=git@github.com:" submodule update --init`.
The release's `mangle-preprocessor.zip` already contains Kumiko. To use another copy, pass
`--kumiko PATH` or set `$KUMIKO`.

Kumiko runs as a separate process, because it has to run from its own folder and its `lib`
package name would clash with other imports.

Kumiko needs **OpenCV 4.x**. With OpenCV 5 it crashes in its line detector (`IndexError: invalid
index to scalar variable`). `requirements.txt` pins `opencv-python-headless<5` for this reason.
If Kumiko was found automatically and fails, `prep.py` says so and falls back to the built-in
detector. With an explicit `--kumiko` or `$KUMIKO`, the error stops the conversion instead. If
your main environment needs OpenCV 5, put Kumiko in its own virtualenv and pass
`--kumiko-python /path/to/venv/bin/python`.

### Checking the result

```sh
python preview.py book.zip --source book.cbz --open
```

This opens a browser page that steps through the book in manifest order. Use the arrow keys
(← is forward for RTL, as on the Kindle) and Space to switch between panel and page mode. With
`--source`, the original page is shown next to it with the detected boxes numbered in reading
order. It's the fastest way to check detection and ordering.

### Manifest

```json
{
  "version": 1,
  "title": "Vol 1",
  "direction": "rtl",
  "screen": [1072, 1448],
  "pages": [
    {"image": "p001.png", "panels": []},
    {"image": "p002.png", "panels": [
      {"image": "p002_1.png", "box": [540, 0, 1072, 620]},
      {"image": "p002_2.png", "box": [0, 0, 530, 620]}
    ]}
  ]
}
```

`panels` are in reading order. `box` is the panel's position on the original page
(`x1, y1, x2, y2`). It's for reference and possible future features; the reader ignores it. An
empty `panels` list means the page is always shown whole.

## 2. Reader

### Controls

| | RTL book | LTR book |
|---|---|---|
| Tap left third | forward | back |
| Tap right third | back | forward |
| Tap centre once | forward | forward |
| Double-tap centre | switch panel ↔ page mode | same |
| Hold centre (0.7 s) | close the book (back to the library) | same |
| Hold left/right | full refresh (clears ghosting) | same |
| Page-turn buttons | forward / back | same |

- **Panel mode** (the default) moves one panel at a time and carries on to the next or previous
  page. Before the first panel of a page, the whole page is shown once as an overview, so you
  see the layout before zooming in: last panel → next page whole → its first panel. Going back
  reverses this. Pages without panels are shown whole. To go straight from panel to panel, set
  `PAGE_OVERVIEW=0` in `mangle.conf` (or run the reader with `-O`).
- **Page mode** moves a whole page at a time.
- Switching modes keeps you on the same page.
- Only centre taps wait 300 ms for a possible second tap. Side taps turn the page immediately.
- Normal turns use a fast partial refresh. Every 8th turn (configurable) and every mode switch
  uses a full flashing refresh to clear ghosting.
- The next image (and the previous one) is decoded in the background while you read.
- The page, panel and mode are saved for each book and restored when you open it again. The
  library shows how far you've got in each book.

Library screen: tap a book to open it. Tap the bottom row (left/right half) to change page, and
tap the top row to exit.

### Building

From the repository root:

```sh
./build.sh toolchain kindlehf   # one-off: download koxtoolchain into ~/x-tools (~100 MB)
./build.sh kindlehf             # firmware 5.16.3 and newer -> reader/dist/mangle-kual-kindlehf.zip
./build.sh kindlepw2            # older 5.x firmware (soft-float)
./build.sh sim                  # desktop simulator
./build.sh test                 # preprocessor + reader tests
./build.sh --help
```

`build.sh` finds the cross-compiler on `PATH` or in `~/x-tools/<triplet>/bin`, fetches FBInk
if it's missing, builds the reader and packages the KUAL extension. To use another toolchain,
run `./build.sh custom --cross <triplet>`.

`./build.sh toolchain` downloads koxtoolchain's prebuilt release (`KOXTOOLCHAIN_VERSION`,
default `2026.08`; needs `curl` and `zstd`). To build the toolchain yourself instead, add
`--from-source`. That takes about an hour and needs crosstool-ng's dependencies; on
Debian/Ubuntu:
`sudo apt-get install build-essential autoconf automake bison flex gawk libtool libtool-bin libncurses-dev curl file git gperf help2man texinfo unzip wget`.

The steps it runs, if you'd rather do them by hand:

```sh
cd reader
./fetch_fbink.sh                    # FBInk v1.25.0 (GPLv3, not vendored)
make kindlehf                       # or kindlepw2
make package TARGET=kindlehf        # -> dist/mangle-kual-kindlehf.zip
```

Cross-compiling needs [koxtoolchain](https://github.com/koreader/koxtoolchain) on `PATH`, with
`arm-kindlehf-linux-gnueabihf-gcc` for `kindlehf` and `arm-kindlepw2-linux-gnueabi-gcc` for
`kindlepw2`. For any other toolchain, use `make build/<name>/mangle TARGET=<name>
CROSS=<triplet>`.

miniz, cJSON and stb_image are included in `reader/third_party/`. FBInk is built as a
`MINIMAL BITMAP IMAGE` static library (built-in font plus raw image blitting). Its Kobo-only
i2c-tools dependency is skipped.

### Releases

`.github/workflows/build.yml` runs the tests and both Kindle builds on every push and pull
request. Every push to `main` also publishes a GitHub release named `build-<run number>`,
marked as latest. It contains:

- `mangle-kual-kindlehf.zip`: the KUAL extension for firmware 5.16.3 and newer
- `mangle-kual-kindlepw2.zip`: the KUAL extension for older 5.x firmware
- `mangle-preprocessor.zip`: `prep.py`, `preview.py` and their requirements
- `SHA256SUMS`

### Installing on the Kindle

1. Jailbreak the Kindle and install KUAL.
2. Download `mangle-kual-<target>.zip` from the latest release, or build it yourself
   (`reader/dist/`). Unzip it into `/mnt/us/extensions/` (the `extensions` folder when the
   Kindle is connected over USB). You should end up with `extensions/mangle/`.
3. Copy your converted ZIPs into `/mnt/us/manga/`.
4. Open KUAL and choose **Mangle → Library** or **Mangle → Resume last book**.

Settings are in `extensions/mangle/mangle.conf`: library folder, full-refresh interval, page
overview, waveform, and touch fixes. If something goes wrong, the log is in `extensions/mangle/mangle.log`.

What the launcher does:

- It detaches from KUAL, pauses the Kindle UI (`killall -STOP cvm`) and stops the screensaver
  from starting.
- The reader takes exclusive access to the touchscreen and page buttons (`EVIOCGRAB`). Devices
  that include the power button are left alone, so sleep still works.
- When the reader exits, crashes, or the script gets a signal, the launcher resumes `cvm`,
  allows the screensaver again and returns to the Home screen.

The screen size and rotation come from FBInk at runtime. If a book was made for a different
size, the reader centres it, or shrinks it if it's too big, and logs a warning.

### Testing on the desktop

The reader has a simulator backend. It writes frames as PGM files and reads taps from stdin, so
navigation, refresh behaviour, resume and the library can all be tested without a Kindle:

```sh
cd reader
make test                 # unit tests (ASan/UBSan) + end-to-end simulator test
printf 'tap 10 700\ntap 10 700\nquit\n' | MANGLE_SIM_DIR=/tmp/frames ./build/sim/mangle -v book.zip
```

Simulator commands are `tap X Y`, `long X Y`, `key CODE`, `sleep MS` and `quit`. The screen size
comes from `MANGLE_SIM_SCREEN=WxH`.

Preprocessor tests: `cd preprocessor && python -m pytest tests`.

## Status and open questions

- **Not yet run on a real Kindle.** Everything above was tested on the desktop with the
  simulator backend. Both Kindle targets build with koxtoolchain 2026.08 (`kindlehf` needs
  glibc 2.17, `kindlepw2` glibc 2.4), but the binaries haven't been run. Expect the touch input
  to need tuning on the device. Some models report mirrored or swapped coordinates; set
  `MANGLE_TOUCH=swap,mirror_x,mirror_y` in `mangle.conf` as needed.
- **Target model and firmware** are still open. They decide `--screen`, `kindlehf` vs
  `kindlepw2`, and whether there are page-turn buttons (the reader handles `KEY_PAGEUP`/`DOWN`
  and the arrow keys).
- **WebP input** works whenever Pillow was built with WebP support (the standard wheels are).
- **Left-to-right comics** are supported with `--direction ltr`, which flips the panel order
  and the tap zones.
- Licensing: Mangle is AGPL-3.0 (see `LICENSE`), as is the bundled Kumiko. The Kindle binary
  also links FBInk (GPLv3) statically. miniz and cJSON are MIT; stb_image is public domain.
