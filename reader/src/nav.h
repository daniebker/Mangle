// Reading position and navigation (pure logic, no I/O).
#pragma once

#include <stdbool.h>

#include "book.h"

typedef enum { MODE_PAGE = 0, MODE_PANEL = 1 } Mode;

typedef struct {
	int  page;
	int  panel;    // index into the page's panels; ignored for pages without panels
	Mode mode;
} Pos;

// Image to show at a position (panel image in panel mode, else the page).
const char* nav_image(const Book* b, Pos p);

// Step forward/back. Returns false (and leaves *p alone) at either end.
bool nav_next(const Book* b, Pos* p);
bool nav_prev(const Book* b, Pos* p);

// Switch between panel and page mode, staying on the same page.
void nav_toggle(const Book* b, Pos* p);

// Clamp a (possibly stale, e.g. restored) position to the book.
void nav_clamp(const Book* b, Pos* p);
