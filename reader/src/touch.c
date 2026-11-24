#include "touch.h"

#include <stdlib.h>
#include <string.h>

void touch_init(Touch* t, int min_x, int max_x, int min_y, int max_y, int screen_w, int screen_h)
{
	memset(t, 0, sizeof(*t));
	t->min_x = min_x, t->max_x = max_x > min_x ? max_x : min_x + screen_w - 1;
	t->min_y = min_y, t->max_y = max_y > min_y ? max_y : min_y + screen_h - 1;
	t->screen_w = screen_w, t->screen_h = screen_h;
}

void touch_quirks(Touch* t, const char* spec)
{
	if (!spec)
		return;
	t->swap_xy  = strstr(spec, "swap") != NULL;
	t->mirror_x = strstr(spec, "mirror_x") != NULL;
	t->mirror_y = strstr(spec, "mirror_y") != NULL;
}

static int scale(int v, int lo, int hi, int size)
{
	long long r = (long long) (v - lo) * size / (hi - lo + 1);
	return r < 0 ? 0 : r >= size ? size - 1 : (int) r;
}

void touch_map(const Touch* t, int rx, int ry, int* x, int* y)
{
	int ax = rx, ay = ry, ax0 = t->min_x, ax1 = t->max_x, ay0 = t->min_y, ay1 = t->max_y;
	if (t->swap_xy) {
		ax = ry, ay = rx;
		ax0 = t->min_y, ax1 = t->max_y, ay0 = t->min_x, ay1 = t->max_x;
	}
	*x = scale(ax, ax0, ax1, t->screen_w);
	*y = scale(ay, ay0, ay1, t->screen_h);
	if (t->mirror_x)
		*x = t->screen_w - 1 - *x;
	if (t->mirror_y)
		*y = t->screen_h - 1 - *y;
}

TouchResult touch_feed(Touch* t, const struct input_event* ev, int64_t now, int* x, int* y)
{
	switch (ev->type) {
		case EV_ABS:
			switch (ev->code) {
				case ABS_MT_SLOT:
					t->slot = ev->value;
					break;
				case ABS_MT_TRACKING_ID:
					if (t->slot == 0)
						t->down = ev->value >= 0;
					break;
				case ABS_MT_POSITION_X:
				case ABS_X:
					if (t->slot == 0)
						t->rx = ev->value, t->frame_data = true;
					break;
				case ABS_MT_POSITION_Y:
				case ABS_Y:
					if (t->slot == 0)
						t->ry = ev->value, t->frame_data = true;
					break;
			}
			return TOUCH_NONE;
		case EV_KEY:
			if (ev->code == BTN_TOUCH)
				t->down = ev->value != 0;
			return TOUCH_NONE;
		case EV_SYN:
			break;
		default:
			return TOUCH_NONE;
	}
	if (ev->code == SYN_MT_REPORT) {
		return TOUCH_NONE;
	}
	if (ev->code != SYN_REPORT)
		return TOUCH_NONE;

	if (t->proto_a)
		t->down = t->frame_data;
	t->frame_data = false;

	TouchResult res = TOUCH_NONE;
	int         cx, cy;
	touch_map(t, t->rx, t->ry, &cx, &cy);
	if (t->down && !t->was_down) {
		t->sx = cx, t->sy = cy, t->maxmove = 0;
		t->t_down = now;
	} else if (t->down) {
		int m = abs(cx - t->sx) + abs(cy - t->sy);
		if (m > t->maxmove)
			t->maxmove = m;
	} else if (t->was_down) {
		int slop = (t->screen_w < t->screen_h ? t->screen_w : t->screen_h) / 20;
		if (t->maxmove <= slop) {
			res = (now - t->t_down >= LONG_PRESS_MS) ? TOUCH_LONG : TOUCH_TAP;
			*x  = t->sx;
			*y  = t->sy;
		}
	}
	t->was_down = t->down;
	return res;
}
