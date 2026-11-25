// Tiny bitmap font for drawing text straight into gray framebuffers.
#pragma once

#include <stdint.h>

#define FONT_W 8
#define FONT_H 13

extern const unsigned char font_data[95][FONT_H];

// Draw `s` with its top-left at (x,y), each font pixel scale x scale pixels.
// Clipped to the w*h buffer. Returns the drawn width in pixels.
int font_draw(uint8_t* buf, int w, int h, int x, int y, int scale, const char* s, uint8_t color);
