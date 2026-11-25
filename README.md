# Mangle

Read manga panel by panel on a jailbroken Kindle, or on a 3.5" ESP32 touch-screen board.

There are two parts:

1. **Preprocessor** (`preprocessor/`, Python, runs on your computer). It takes a ZIP/CBZ of
   pages, finds the panels, prepares every image for the screen, and writes a ZIP with a
   `manifest.json`.
2. **Reader** (C). It opens those ZIPs and shows them panel by panel or page by page. One app,
   built for two devices with the same features:
   - **Kindle** (`reader/`): a KUAL extension for jailbroken Kindles.
   - **ESP32 boards** (`cyd/`): firmware for the 3.5" ESP32-3248S035 "Cheap Yellow Display"
     and the Freenove ESP32-S3 Display 3.5" (FNK0104N).

### Code layout

| Folder | What |
|---|---|
| `common/src` | The whole app, shared by every build: library, reading screen, menu, settings, reading statistics, navigation, gestures, zoom, ZIP and PNG decoding, scaling. `hal.h` is the interface to the device. |
| `common/sim` | A desktop simulator of that interface, to run and test the app as either device. |
| `common/tests` | Unit tests and end-to-end tests of the app, as a Kindle and as both CYD boards. |
| `reader/` | The Kindle's hardware layer (`src/hal_kindle.c`: FBInk e-ink display, evdev touch and buttons, frontlight, suspend) and the KUAL packaging. |
| `cyd/` | The ESP32 boards' hardware layers and the PlatformIO project: `src/esp32/` is what they share (input loop, sleep, backlight, Wi-Fi clock), `src/boards/<board>/` each board's display, touch and SD card. |

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
| `--device kindle\|cyd` | `kindle` | Sets the defaults for `--screen`, `--gamma` and dithering (see the CYD section) |
| `--screen WxH` | `1072x1448` | Portrait screen size of the target Kindle |
| `--direction rtl\|ltr` | `rtl` | Reading direction (`ltr` for western comics) |
| `--kumiko PATH` | `$KUMIKO`, the bundled submodule, or `kumiko` on PATH | Kumiko checkout or its `kumiko` script |
| `--kumiko-python PATH` | the current Python | Python used to run Kumiko |
| `--no-panels` | | Skip panel detection (page mode only) |
| `--gamma G` | `1.8` | Darkens mid-tones for e-ink (`1` = off) |
| `--no-dither` / `--dither` | dither | Reduce to 16 grays without / with Floyd–Steinberg dithering |
| `--pdf-dpi N` | `200` | Render resolution for PDF input |
| `-j N` | CPU count | Worker processes |

You can pass several input files with `-o some_folder/`.

Common screen sizes: Paperwhite 3/4, Oasis (1st gen) and Kindle 2022 → `1072x1448`;
Paperwhite 5 and Signature → `1236x1648`; Oasis 2/3 → `1264x1680`; basic Kindle (2019) →
`600x800`.
Check your model before converting a whole library.

What it does to each volume:

1. Extracts the images (for a PDF, renders each page with pypdfium2; JPEG, PNG, WebP, GIF, BMP), skips `__MACOSX` and hidden files, and sorts
   pages in natural order (`page2` before `page10`).
2. Finds panels on the full-resolution pages with [Kumiko](https://github.com/njean42/kumiko).
   If Kumiko isn't available or fails, it uses a simpler built-in OpenCV detector, which works
   for clean layouts with white gutters.
3. Drops tiny boxes, merges overlapping ones, and sorts panels in reading order: rows from top
   to bottom, columns right to left (RTL), and stacks inside a column from top to bottom.
4. Pages it isn't confident about (no panels, one huge panel, too many panels, low coverage,
   lots of small fragments) get an empty panel list, so the reader shows them whole. Each one
   is listed on stderr with the reason.
5. Writes a full-page image (white margins cropped). Panels are rectangles into that image, which
   the reader scales up when showing them, so the ZIP is about the size of the source.
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
  "version": 2,
  "title": "Vol 1",
  "direction": "rtl",
  "screen": [1072, 1448],
  "pages": [
    {"image": "p001.png", "panels": []},
    {"image": "p002.png", "panels": [
      {"rect": [560, 0, 512, 600], "box": [540, 0, 1072, 620]},
      {"rect": [0, 0, 540, 600], "box": [0, 0, 530, 620]}
    ]}
  ]
}
```

`panels` are in reading order. `rect` is the panel's position in the stored page image
(`x, y, width, height`); the reader crops it out and scales it to the screen, so panels cost no
extra space. `box` is the panel's position on the original page (`x1, y1, x2, y2`), for reference;
the reader ignores it. Version 1 books, where each panel was its own `image`, still open. An
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
| Hold left/right | zoom 2x around the finger, again for 3x, again for the fitted view | same |
| Swipe (not zoomed) | swipe right: forward, left: back | swipe left: forward, right: back |
| Page-turn buttons | forward / back | same |
| Pinch out / in (two fingers, not on the resistive CYD) | zoom in / out of the panel or page | same |
| Drag (while zoomed) | pan | same |
| Double-tap centre (while zoomed) | back to the fitted view | same |

- **Panel mode** (the default) moves one panel at a time and carries on to the next or previous
  page. Before the first panel of a page, the whole page is shown once as an overview, so you
  see the layout before zooming in: last panel → next page whole → its first panel. Going back
  reverses this. Pages without panels are shown whole. To go straight from panel to panel, set
  `PAGE_OVERVIEW=0` in `mangle.conf` (or run the reader with `-O`).
- **Page mode** moves a whole page at a time.
- Switching modes keeps you on the same page.
- **Zoom**: hold a side of the screen to zoom 2x around your finger, hold again for 3x, and a
  third time to go back to the fitted view (with a full refresh on e-ink). Or pinch with two
  fingers to zoom into the current panel or page (up to 4x the fitted size), around the point
  between your fingers; the new view is drawn once you lift a finger. While zoomed, drag with
  one finger to pan; the view stays inside the panel (or page). Pinch back in, or double-tap
  the centre, to return to the fitted view. Turning the page or panel also resets the zoom.
- Only centre taps wait 300 ms for a possible second tap. Side taps turn the page immediately.
- On e-ink, normal turns use a fast partial refresh. Every 8th turn (configurable), every mode
  switch and every return from a menu uses a full flashing refresh to clear ghosting. The `R`
  key also forces one.
- On the Kindle the next image (and the previous one) is decoded in the background while you
  read. The CYD has too little memory for that and draws straight from the card.
- The page, panel and mode are saved for each book and restored when you open it again. The
  library shows how far you've got in each book.
- Tap the top edge for the menu bar: `< Library` (back to the shelf), `Settings`, `Stats` and
  the page number.
- **Settings**: page preview in panel mode, sleep after 2/5/10/30 minutes or never,
  brightness (frontlight or backlight), rotation (0/90/180/270), night mode (inverted grays),
  plus the CYD's `Invert panel` and `Calibrate touch`. They are saved in the state folder.
- **Sleep**: after the idle time the book's cover is shown and the device sleeps (the Kindle
  suspends and wakes with the power button; the CYD turns its screen off and wakes on a touch).
- **Resume**: the Kindle's `Resume last book` reopens the last book. The CYD reopens the book
  that was open when it was switched off, and starts in the library if you closed the book.

Library screen: a shelf of covers (the first page of each book) with the title and progress
under each one. Tap a cover to open it, tap or swipe the bottom strip to change shelf page, and
use the header for `Stats`, `Settings` and (on the Kindle) `Exit`. Covers are cached in the
state folder.

### Reading statistics

Both devices record reading statistics. Every reading session is logged to `stats.log` in the
state folder, one line per session:
`START END PAGES BOOK` (Unix times, number of different pages seen, book file name). A session
starts when you open a book and ends when you close it, when the device goes to sleep, or after
5 minutes without a tap or key press (it ends at your last input, and the next input starts a new
session). Sessions under 10 seconds aren't logged.

Open the stats from the library's `Stats` button or from the menu bar's `Stats` entry while
reading. The session you're in is included.

- **Daily**: today's and this week's reading time and pages, your current and best streak (days
  in a row with some reading), bar charts for the last 14 days, time of day and day of the week,
  and all-time totals.
- **Monthly**: a calendar shaded by how long you read each day (with the time in each cell),
  then the month's reading time, pages, days read, sessions, books, average per reading day, best
  day, and the change from the previous month. Tap the bottom strip (or use the page-turn
  buttons) to go to the previous or next month; the middle goes back to this month.

Days and hours use local time. On the Kindle, if they look shifted, set `TZ` in `mangle.conf`.
The CYD has no clock that survives power-off: it sets the time over Wi-Fi at power-on (see
the CYD section), and without that the stats screen says so and nothing is recorded.

### Building

From the repository root:

```sh
./build.sh toolchain kindlehf   # one-off: download koxtoolchain into ~/x-tools (~100 MB)
./build.sh kindlehf             # firmware 5.16.3 and newer -> reader/dist/mangle-kual-kindlehf.zip
./build.sh kindlepw2            # older 5.x firmware (soft-float)
./build.sh sim                  # desktop simulator (common/build/mangle)
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

miniz and stb_image (tests only) are included in `common/third_party/`. FBInk is built as a
`MINIMAL BITMAP IMAGE` static library (built-in font plus raw image blitting). Its Kobo-only
i2c-tools dependency is skipped.

### Releases

`.github/workflows/build.yml` runs the tests and both Kindle builds on every push to `main` and
on pull requests. `.github/workflows/firmware.yml` builds the CYD firmware on every branch push and
pull request that touches it. Pushing a version tag (`git tag v0.9.0 && git push origin v0.9.0`)
makes both workflows publish to a GitHub release for that tag, marked as latest. It contains:

- `mangle-kual-kindlehf.zip`: the KUAL extension for firmware 5.16.3 and newer
- `mangle-kual-kindlepw2.zip`: the KUAL extension for older 5.x firmware
- `mangle-cyd35r.bin` / `mangle-cyd35c.bin` and `SHA256SUMS-cyd`: Cheap Yellow Display firmware
  (see below), attached by `.github/workflows/firmware.yml`
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

The app runs on the desktop with a simulated device. It writes each screen as a PGM file and
reads taps from stdin, so navigation, refresh behaviour, resume, the library, settings and
statistics can all be tested without a Kindle or a CYD:

```sh
make -C common test        # unit tests (ASan/UBSan) + end-to-end tests as a Kindle and as both CYDs
make -C reader test        # Kindle-only tests (evdev touch decoding)
printf 'tap 10 700\ntap 10 700\nquit\n' | MANGLE_SIM_DIR=/tmp/frames common/build/mangle -v book.zip
```

Simulator commands are `tap X Y`, `long X Y`, `drag X Y DX DY`, `pinch X Y SCALE [DX DY]`
(two fingers around X,Y; SCALE > 1 zooms in), `key CODE`, `sleep MS` and `quit`.
`MANGLE_SIM_DEVICE=kindle|cyd35r|cyd35c` picks the device (screen size, touch, which settings
exist); `MANGLE_SIM_SCREEN=WxH` overrides the screen size. Options are the Kindle's (`-l`, `-s`,
`-r`, `-t`, `-f`, `-O`, `-v`), plus `-R` to reopen the book only if it was open, as the CYD does.

Preprocessor tests: `cd preprocessor && python -m pytest tests`.

## 3. Cheap Yellow Display firmware

`cyd/` is the reader as firmware for 3.5" 320x480 ESP32 touch-screen boards. It is the same app
as on the Kindle (see the Reader section for the controls); only the hardware layer is its own.

| Board | Chip | Screen and touch | Firmware |
|---|---|---|---|
| ESP32-3248S035**R** ("Cheap Yellow Display") | ESP32 | ST7796 on SPI, resistive (XPT2046) | `cyd35r` |
| ESP32-3248S035**C** | ESP32 | ST7796 on SPI, capacitive (GT911) | `cyd35c` |
| Freenove ESP32-S3 Display 3.5" (FNK0104N) | ESP32-S3, 8 MB PSRAM | ST77922 IPS on QSPI, capacitive | `freenove35` |

The ESP32-3248S035 boards have a CH340 USB-serial chip; the Freenove board's USB-C goes to the
ESP32-S3 itself (its port is `/dev/cu.usbmodem…` on a Mac). With its PSRAM, the Freenove board
decodes the next pages ahead like the Kindle, and turns pages faster.

The Freenove panel's initialisation sequence (power, gamma and timing registers for that glass)
comes from Freenove's own library, which is licensed CC BY-NC-SA 3.0 and so can't be part of
Mangle's AGPL source. Like FBInk for the Kindle, it is downloaded when the firmware is built
(`cyd/src/boards/freenove_fnk0104n/fetch_init.py`, pinned to a Freenove commit).

### Making books for it

```sh
python prep.py "My Manga v01.cbz" -o "My Manga v01.zip" --device cyd
```

`--device cyd` stores pages at 640x960 with no e-ink gamma and no dithering, which suits an LCD.
The reader shrinks whole pages to fit and uses the extra detail when it shows a panel or zooms.
For the fastest page turns, use `--device cyd --screen 320x480`. Books made for a Kindle also
open, but they draw more slowly.

Copy the ZIPs to a FAT32 microSD card in a folder called `manga`, and put the card in the board.

### Flashing

The easiest way is the **[web flasher](https://daniebker.github.io/Mangle/)**. In Chrome or Edge
on a desktop computer, plug the board in over USB, choose a release and your board, and click
*Connect and install*. It lists the firmware of the newest releases; add `?tag=v0.9.0` to the
address to preselect one.

Each release also has `mangle-cyd35r.bin`, `mangle-cyd35c.bin` and `mangle-freenove35.bin`.
Each is a complete image: write it at offset `0x0`, for example with esptool:

```sh
esptool.py --chip esp32 write_flash 0x0 mangle-cyd35r.bin
esptool.py --chip esp32s3 write_flash 0x0 mangle-freenove35.bin
```

The `CYD firmware` workflow also builds them on every branch push and pull request that touches
the firmware; download them from the run's artifacts to try a branch.

The flasher's source is `web/`. `.github/workflows/pages.yml` publishes it to GitHub Pages
together with the firmware of the newest releases (browsers can't download release assets from
GitHub directly). It runs after each tagged release, so new versions appear on their own.
Preview it locally with fake or downloaded images:

```sh
python web/build_site.py /tmp/site --local v0.9.0=folder/with/bins
python -m http.server -d /tmp/site
```

To build it yourself, install [PlatformIO](https://platformio.org/) (`pip install platformio`):

```sh
cd cyd
pio run -e cyd35r -t upload      # or cyd35c, freenove35
pio device monitor               # log at 115200 baud
```

or `./build.sh cyd` from the repository root. Pins and options are in each board's `board.h`
under `cyd/src/boards/`.

### Using it

The controls, settings, library and statistics are the Reader section's. Specific to the CYD:

- The resistive board asks for touch calibration on first start (press three crosses). Hold the
  screen while powering on to calibrate again. It can't pinch: hold a side to zoom.
- `Invert panel` in the settings fixes displays that come up as a negative.
- On the Freenove board, rotation is done in software (its panel can only flip 180 degrees).
- Positions, settings, statistics and cover thumbnails are kept in `manga/.mangle/` on the card.
- **Clock for reading statistics**: put a `wifi.txt` in `manga/.mangle/` with the network name
  on the first line, the password on the second, and optionally your time zone in POSIX form on
  the third (e.g. `CET-1CEST,M3.5.0,M10.5.0/3`; the default is UTC). At power-on the board
  joins the network, sets its clock and turns Wi-Fi off again.

### How it fits in 520 KB of RAM

The board has no PSRAM, so a page is never decoded whole. `common/src/pngs.c` decodes a PNG one
row at a time straight from the ZIP on the SD card. `common/src/render.c` scales each row on the
way: it averages areas when shrinking, uses bilinear sampling when enlarging, and only draws
the part of the page that is visible. It stops decoding once the panel's last row is drawn.
The manifest is parsed while it is being read (`common/src/book.c`) instead of being built as
a JSON tree. A page turn needs about 60 KB of memory. The Kindle uses the same decoder and
scaler; it just decodes the next pages ahead into memory.

### Testing on the desktop

Everything but the hardware layers is the shared app, tested on the desktop as each board
(`MANGLE_SIM_DEVICE=cyd35r|cyd35c|freenove35`):

```sh
make -C common test
printf 'tap 80 150\ntap 10 300\nquit\n' | MANGLE_SIM_DEVICE=cyd35r MANGLE_SIM_DIR=/tmp/frames common/build/mangle -v -R -l ~/manga -s ~/manga/.mangle
```

The unit tests also check the PNG decoder against stb_image for every colour type and bit depth.

## Status and open questions

- **Target model and firmware** are still open. They decide `--screen`, `kindlehf` vs
  `kindlepw2`, and whether there are page-turn buttons (the reader handles `KEY_PAGEUP`/`DOWN`
  and the arrow keys).
- **WebP input** works whenever Pillow was built with WebP support (the standard wheels are).
- **Left-to-right comics** are supported with `--direction ltr`, which flips the panel order
  and the tap zones.
- Licensing: Mangle is AGPL-3.0 (see `LICENSE`), as is the bundled Kumiko. The Kindle binary
  also links FBInk (GPLv3) statically. miniz is MIT; stb_image (tests only) is public domain.
