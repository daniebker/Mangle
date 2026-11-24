# Changelog

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
