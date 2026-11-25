# Changelog

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
