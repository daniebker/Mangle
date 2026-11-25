// Streaming PNG decoder: reads from a byte source and hands out one 8-bit gray
// row at a time, so a page never has to fit in memory. Memory use is about
// 44 KiB (inflate state and window) plus two rows of the image.
//
// Supports every non-interlaced PNG: gray, gray+alpha, RGB, RGBA (alpha is
// composited over white) and palette images, 1 to 16 bits.
#pragma once

#include <stdint.h>

// Byte source: returns bytes read, 0 at the end, -1 on error.
typedef int (*PngRead)(void* ctx, uint8_t* buf, int n);

// Row sink: y is the row index; return nonzero to stop decoding early.
typedef int (*PngRow)(void* ctx, int y, const uint8_t* gray, int w);

typedef struct Png Png;

// Read the header. Returns NULL on error (with a reason in err if given).
Png* png_open(PngRead rd, void* ctx, const char** err);
void png_size(const Png* p, int* w, int* h);
// Decode rows in order, calling row() for each. Returns 0 on success (or an
// early stop), -1 on error.
int  png_decode(Png* p, PngRow row, void* ctx);
void png_close(Png* p);
