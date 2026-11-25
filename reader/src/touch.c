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

// A frame is complete: list the contacts that are down, in screen coordinates.
static int frame(const Touch* t, TPoint* pts)
{
	int n = 0;
	for (int i = 0; i < TOUCH_MAX; i++)
		if (t->c[i].down) {
			touch_map(t, t->c[i].rx, t->c[i].ry, &pts[n].x, &pts[n].y);
			n++;
		}
	return n;
}

int touch_feed(Touch* t, const struct input_event* ev, TPoint* pts)
{
	Contact* c = t->slot >= 0 && t->slot < TOUCH_MAX ? &t->c[t->slot] : NULL;
	switch (ev->type) {
		case EV_ABS:
			switch (ev->code) {
				case ABS_MT_SLOT:
					t->slot = ev->value;
					break;
				case ABS_MT_TRACKING_ID:
					t->mt_id = true;
					if (c)
						c->down = ev->value >= 0;
					break;
				case ABS_MT_POSITION_X:
					t->mt_pos = true;
					if (c)
						c->rx = ev->value, c->data = true;
					break;
				case ABS_MT_POSITION_Y:
					t->mt_pos = true;
					if (c)
						c->ry = ev->value, c->data = true;
					break;
				// Single-touch axes: only when there are no MT ones (they
				// would just repeat the first contact).
				case ABS_X:
					if (!t->mt_pos)
						t->c[0].rx = ev->value, t->c[0].data = true;
					break;
				case ABS_Y:
					if (!t->mt_pos)
						t->c[0].ry = ev->value, t->c[0].data = true;
					break;
			}
			return -1;
		case EV_KEY:
			// BTN_TOUCH is the only contact state for single-touch devices;
			// with MT tracking IDs, only its release (all fingers up) matters.
			if (ev->code == BTN_TOUCH && (!t->mt_id || ev->value == 0) && !t->proto_a && !t->mt_a) {
				if (ev->value == 0)
					for (int i = 0; i < TOUCH_MAX; i++)
						t->c[i].down = false;
				else
					t->c[0].down = true;
			}
			return -1;
		case EV_SYN:
			break;
		default:
			return -1;
	}
	if (ev->code == SYN_MT_REPORT) {
		// Protocol A: each contact's data ends with SYN_MT_REPORT.
		t->mt_a = true;
		if (c && c->data)
			t->slot++;
		return -1;
	}
	if (ev->code != SYN_REPORT)
		return -1;

	if (t->proto_a || t->mt_a) {
		// Protocol A: the frame lists every contact that is down.
		for (int i = 0; i < TOUCH_MAX; i++) {
			t->c[i].down = t->c[i].data;
			t->c[i].data = false;
		}
		t->slot = 0;
	} else {
		for (int i = 0; i < TOUCH_MAX; i++)
			t->c[i].data = false;
	}
	return frame(t, pts);
}
