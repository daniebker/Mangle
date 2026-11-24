#include <string.h>

#include "display.h"

void compose(const Screen* s, const Image* img, uint8_t* buf)
{
	int sw = s->width, sh = s->height;
	memset(buf, 0xFF, (size_t) sw * sh);
	if (!img || !img->px)
		return;

	if (img->w <= sw && img->h <= sh) {
		int x0 = (sw - img->w) / 2, y0 = (sh - img->h) / 2;
		for (int y = 0; y < img->h; y++)
			memcpy(buf + (size_t) (y0 + y) * sw + x0, img->px + (size_t) y * img->w, (size_t) img->w);
		return;
	}

	// Fit inside the screen keeping the aspect ratio (fixed point 16.16).
	int dw, dh;
	if ((long long) img->w * sh > (long long) img->h * sw) {
		dw = sw;
		dh = (int) ((long long) img->h * sw / img->w);
	} else {
		dh = sh;
		dw = (int) ((long long) img->w * sh / img->h);
	}
	if (dw < 1)
		dw = 1;
	if (dh < 1)
		dh = 1;
	int      x0 = (sw - dw) / 2, y0 = (sh - dh) / 2;
	uint32_t sx = (uint32_t) (((uint64_t) img->w << 16) / dw);
	uint32_t sy = (uint32_t) (((uint64_t) img->h << 16) / dh);
	for (int y = 0; y < dh; y++) {
		const uint8_t* src = img->px + (size_t) ((y * (uint64_t) sy) >> 16) * img->w;
		uint8_t*       dst = buf + (size_t) (y0 + y) * sw + x0;
		uint32_t       fx  = 0;
		for (int x = 0; x < dw; x++, fx += sx)
			dst[x] = src[fx >> 16];
	}
}
