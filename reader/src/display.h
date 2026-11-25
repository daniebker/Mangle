// Screen output. Two backends: FBInk (Kindle) and sim (desktop, writes PGM files).
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "book.h"
#include "zoom.h"

typedef struct {
	int width, height;    // current (rotated) view size in pixels
	int row_height;       // text row height in pixels
	int rows;             // text rows that fit on screen
	int cols;             // text columns that fit on screen
} Screen;

int  display_open(Screen* s);
void display_close(void);

// Show a full-screen 8-bit gray buffer (s->width * s->height bytes).
// flash = full flashing refresh (clears ghosting), else a fast partial one.
int display_frame(const uint8_t* buf, bool flash);

// Clear the screen and print text lines, one per row (row 0 = top).
// A NULL line is left blank.
int display_text(const char* const* lines, int n, bool flash);

// Compose an image centred on a white full-screen buffer. Images larger than
// the screen (wrong --screen at preprocessing) are shrunk, nearest-neighbour.
void compose(const Screen* s, const Image* img, uint8_t* buf);

// Put the device into standby (suspend to RAM) with the current screen left
// showing. Blocks until the device wakes. Returns 0 if it suspended and woke,
// -1 if suspending isn't possible here.
int display_suspend(void);

// Show a rectangle {x, y, w, h} of an image, scaled to fit the screen.
void compose_rect(const Screen* s, const Image* img, const int rect[4], uint8_t* buf);

// Show part of an image zoomed: the base rectangle at zoom_scale(zm, b),
// centred on (zm->cx, zm->cy), bilinear. Outside the base rectangle is white.
// zm must be clamped (zoom_clamp). Returns -1 if out of memory.
int compose_view(const Screen* s, const Image* img, const ZoomBase* b, const Zoom* zm, uint8_t* buf);
