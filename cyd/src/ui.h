// Text and simple drawing on top of the HAL (uses the reader's 8x13 font).
#pragma once

#include <stdint.h>

#define UI_BLACK 0
#define UI_WHITE 255
#define UI_GRAY 128

// Width in pixels of s at the given scale (UTF-8 sequences count as one glyph).
int ui_text_width(const char* s, int scale);
// Draw s with its top-left at (x, y) on a bg background. Returns the width.
int ui_text(int x, int y, int scale, const char* s, uint8_t fg, uint8_t bg);
// Same, centred horizontally in [x, x+w), cut short with "..." if too wide.
void ui_text_fit(int x, int y, int w, int scale, const char* s, uint8_t fg, uint8_t bg, int centre);

// Full-screen message: lines of text (wrapped), centred vertically.
void ui_message(const char* const* lines, int n);
