#include "fb.h"

#include <stdlib.h>
#include <string.h>

#include "hal.h"

static uint8_t* logical;    // current rotation
static uint8_t* phys;       // as the device shows it
static int      pw, ph, rot;
static bool     night;

int fb_init(int w, int h)
{
	pw = w, ph = h;
	logical = malloc((size_t) w * h);
	phys    = malloc((size_t) w * h);
	if (!logical || !phys) {
		fb_free();
		return -1;
	}
	memset(logical, 255, (size_t) w * h);
	return 0;
}

void fb_free(void)
{
	free(logical);
	free(phys);
	logical = phys = NULL;
}

int hal_width(void)
{
	return rot & 1 ? ph : pw;
}

int hal_height(void)
{
	return rot & 1 ? pw : ph;
}

void hal_set_rotation(int r)
{
	rot = r & 3;
	memset(logical, 255, (size_t) pw * ph);
}

void hal_set_night(bool on)
{
	night = on;
}

void hal_row(int x, int y, int w, const uint8_t* g)
{
	int sw = hal_width();
	if (y < 0 || y >= hal_height())
		return;
	if (x < 0)
		g -= x, w += x, x = 0;
	if (x + w > sw)
		w = sw - x;
	if (w > 0)
		memcpy(logical + (size_t) y * sw + x, g, (size_t) w);
}

void hal_fill(int x, int y, int w, int h, uint8_t v)
{
	int sw = hal_width(), sh = hal_height();
	if (x < 0)
		w += x, x = 0;
	if (y < 0)
		h += y, y = 0;
	if (x + w > sw)
		w = sw - x;
	if (y + h > sh)
		h = sh - y;
	for (int r = y; r < y + h && w > 0; r++)
		memset(logical + (size_t) r * sw + x, v, (size_t) w);
}

// Logical (x, y) in the current rotation -> physical index. Rotation r turns
// the picture r * 90 degrees clockwise on the device.
void hal_present(bool full)
{
	int      lw = hal_width(), lh = hal_height();
	uint8_t  inv = night ? 255 : 0;
	if (rot == 0) {
		for (size_t i = 0; i < (size_t) pw * ph; i++)
			phys[i] = logical[i] ^ inv;
	} else {
		for (int y = 0; y < lh; y++) {
			const uint8_t* src = logical + (size_t) y * lw;
			for (int x = 0; x < lw; x++) {
				int px, py;
				switch (rot) {
					case 1: px = pw - 1 - y, py = x; break;
					case 2: px = pw - 1 - x, py = ph - 1 - y; break;
					default: px = y, py = ph - 1 - x; break;
				}
				phys[(size_t) py * pw + px] = src[x] ^ inv;
			}
		}
	}
	fb_show(phys, full);
}

TPoint fb_rotate_point(int px, int py)
{
	return tin_rotate(rot, pw, ph, px, py);
}
