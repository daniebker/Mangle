// Reading position and navigation (pure logic, no I/O).
#pragma once

#include <stdbool.h>

#include "book.h"

typedef enum { MODE_PAGE = 0, MODE_PANEL = 1 } Mode;

// In panel mode, panel == PANEL_OVERVIEW shows the whole page before its first
// panel (only on pages that have panels, and only with the overview enabled).
#define PANEL_OVERVIEW (-1)

typedef struct {
	int  page;
	int  panel;    // index into the page's panels, or PANEL_OVERVIEW; ignored for pages without panels
	Mode mode;
} Pos;

// Show each paneled page whole before its panels in panel mode (default: on).
void nav_set_overview(bool on);
bool nav_overview(void);

// Where a book starts: the first page, at its overview if it has one.
Pos nav_start(const Book* b);

// Image to show at a position (panel image in panel mode, else the page).
const char* nav_image(const Book* b, Pos p);

// Part of nav_image's image to show (a panel's rectangle). Returns false when
// the whole image is shown.
bool nav_rect(const Book* b, Pos p, int rect[4]);

// Step forward/back. Returns false (and leaves *p alone) at either end.
// Panel mode, forward: [overview] -> panel 1 -> ... -> last panel -> next page.
// Back runs the same sequence in reverse.
bool nav_next(const Book* b, Pos* p);
bool nav_prev(const Book* b, Pos* p);

// Switch between panel and page mode, staying on the same page.
void nav_toggle(const Book* b, Pos* p);

// Clamp a (possibly stale, e.g. restored) position to the book.
void nav_clamp(const Book* b, Pos* p);
