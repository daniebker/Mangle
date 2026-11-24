// Screen output. Two backends: FBInk (Kindle) and sim (desktop, writes PGM files).
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "book.h"

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
