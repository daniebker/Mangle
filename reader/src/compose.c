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

// Show a rectangle of the image, scaled up (or down) to fit the screen with
// bilinear sampling, centred on white.
void compose_rect(const Screen* s, const Image* img, const int rect[4], uint8_t* buf)
{
	int sw = s->width, sh = s->height;
	memset(buf, 0xFF, (size_t) sw * sh);
	if (!img || !img->px)
		return;
	int rx = rect[0] < 0 ? 0 : rect[0], ry = rect[1] < 0 ? 0 : rect[1];
	int rw = rect[2], rh = rect[3];
	if (rx >= img->w || ry >= img->h)
		return;
	if (rx + rw > img->w)
		rw = img->w - rx;
	if (ry + rh > img->h)
		rh = img->h - ry;
	if (rw < 1 || rh < 1)
		return;

	int dw, dh;
	if ((long long) rw * sh > (long long) rh * sw) {
		dw = sw;
		dh = (int) ((long long) rh * sw / rw);
	} else {
		dh = sh;
		dw = (int) ((long long) rw * sh / rh);
	}
	if (dw < 1)
		dw = 1;
	if (dh < 1)
		dh = 1;
	int    x0 = (sw - dw) / 2, y0 = (sh - dh) / 2;
	double fx = (double) rw / dw, fy = (double) rh / dh;
	for (int y = 0; y < dh; y++) {
		double sy = (y + 0.5) * fy - 0.5;
		int    iy = (int) sy;
		if (sy < 0)
			sy = 0, iy = 0;
		int      iy2 = iy + 1 < rh ? iy + 1 : iy;
		int      wy  = (int) ((sy - iy) * 256);
		const uint8_t* r0 = img->px + (size_t) (ry + iy) * img->w + rx;
		const uint8_t* r1 = img->px + (size_t) (ry + iy2) * img->w + rx;
		uint8_t*       dst = buf + (size_t) (y0 + y) * sw + x0;
		for (int x = 0; x < dw; x++) {
			double sx = (x + 0.5) * fx - 0.5;
			int    ix = (int) sx;
			if (sx < 0)
				sx = 0, ix = 0;
			int ix2 = ix + 1 < rw ? ix + 1 : ix;
			int wx  = (int) ((sx - ix) * 256);
			int top = r0[ix] * (256 - wx) + r0[ix2] * wx;
			int bot = r1[ix] * (256 - wx) + r1[ix2] * wx;
			dst[x]  = (uint8_t) ((top * (256 - wy) + bot * wy) >> 16);
		}
	}
}
