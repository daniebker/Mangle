# Changelog

## Unreleased

### Changed
- The Kindle reader and the CYD firmware are now one app (`common/`) with a small hardware layer per device (`reader/src/hal_kindle.c`, `cyd/src/esp32/` and `cyd/src/boards/`), so they have the same features. The Kindle gains swipe to turn, zoom by holding a side, rotation, night mode, a brightness setting for the frontlight, sleep after 2/5/10/30 minutes, and cached library covers. The CYD gains reading statistics (with the time from Wi-Fi), the stats screen, page-turn keys, and the cover on screen before sleeping.
- On the Kindle, holding the left or right side now zooms instead of forcing a full refresh. Full refreshes still happen every 8 turns, on mode switches and after menus, and with the `R` key.
- The Kindle draws pages with the CYD's decoder and scaler (area averaging when shrinking, bilinear when enlarging); stb_image and cJSON are no longer used. The desktop simulator and all app tests are in `common/` and run as both devices.

### Added
- ESP32 boards: the BOOT button sends the device to sleep at once and wakes it again; while asleep that way, touches don't wake it.
- Firmware for the Freenove ESP32-S3 Display 3.5" (FNK0104N): ST77922 panel on QSPI, capacitive touch, SDMMC card slot. With its PSRAM it decodes pages ahead like the Kindle, and rotates in software. The panel's init sequence is fetched from Freenove at build time. In the web flasher and in releases as `mangle-freenove35.bin`.
- Firmware for the 3.5" Cheap Yellow Display (ESP32-3248S035, resistive `R` and capacitive `C` boards) in `cyd/`. It reads the same book ZIPs from an SD card: a library of covers, panel and page mode, tap and swipe to turn, zoom by holding a side (or pinching on the capacitive board), a menu bar, settings (rotation, brightness, sleep, night mode), and resume at power-on. Pages are decoded and scaled row by row, so the board needs no PSRAM. Navigation, gestures and zoom use the Kindle reader's code.
- Desktop simulator and tests for the firmware (`make -C cyd test`, part of `./build.sh test`). `./build.sh cyd` builds the firmware with PlatformIO, and releases include flashable `mangle-cyd35r.bin` and `mangle-cyd35c.bin` images.
- Web flasher for the CYD on GitHub Pages (https://daniebker.github.io/Mangle/): choose a release and a board, then install from Chrome or Edge over USB. `.github/workflows/pages.yml` republishes it with the firmware of the newest releases after every tagged release.
- `prep.py --device cyd`: 640x960 pages with no gamma or dithering, for the LCD. `--dither` turns dithering back on.

## v0.8.0

### Added
- Pinch to zoom on panels and whole pages (up to 4x), centred on the fingers. Drag with one finger to pan while zoomed; pinch back in or double-tap the centre to return to the fitted view. Turning the page resets the zoom.
- Touch input tracks two fingers (MT protocol A and B). Simulator commands `drag` and `pinch`.

## v0.7.0

### Added
- Reading statistics: each session (start and end time, pages read, book) is logged to `state/stats.log`. Idle time and sleep aren't counted.
- Stats screen, from the library's `[stats]` entry or the menu bar's `Stats` entry. Daily: today, this week, streaks, the last 14 days, time of day and day of week. Monthly: a calendar shaded by reading time and the month's totals, with previous/next month.

## v0.6.0

### Added
- Settings: "Panel mode page preview" turns off the whole-page overview shown before a page's panels. Saved with the other settings; `-O` only sets the default.

### Changed
- Books are about 4x smaller: panels are stored as rectangles of the page image (manifest version 2) instead of separate upscaled images, and the reader crops and scales them. Version 1 books still open. Panels are a little softer, since they are scaled from the screen-size page.

## v0.4.0

### Added
- Sleep: after 2 minutes idle the book's cover (page 1, or page 2 if the first is blank) is drawn and the Kindle is suspended to RAM; the power button wakes it and reading resumes. `-t SEC` changes the delay.
- Menu bar `Settings` entry with a Sleep on/off switch, saved in the state folder.

## v0.3.0

### Added
- Menu bar: tap the top edge of the screen while reading to open it; its `Library` entry returns to the library.

## v0.2.0

### Added
- Preprocessor accepts `.pdf` input: each page is rendered with pypdfium2 (`--pdf-dpi`, default 200)
  and goes through the usual panel detection and e-ink optimisation.
- Library shelf view: books are shown as a grid of covers (first page) standing on shelves, with
  title and reading progress under each cover.
- Built-in 8x13 bitmap font for drawing text into the framebuffer.

### Changed
- Library taps: tap a cover to open it, bottom strip changes shelf page, top strip exits.
- Panel mode shows each paneled page whole before its panels.

## v0.1.0

- Initial release: preprocessor, Kindle reader, KUAL extension.
