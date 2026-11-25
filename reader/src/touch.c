#include "touch.h"

#include <math.h>
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

static void map_contact(const Touch* t, int i, int* x, int* y)
{
	touch_map(t, t->c[i].rx, t->c[i].ry, x, y);
}

static void pinch_points(const Touch* t, int pts[4])
{
	map_contact(t, 0, &pts[0], &pts[1]);
	map_contact(t, 1, &pts[2], &pts[3]);
}

static double dist(const int p[4])
{
	double dx = p[2] - p[0], dy = p[3] - p[1];
	return sqrt(dx * dx + dy * dy);
}

// A frame is complete: work out what the fingers did.
static TouchResult frame(Touch* t, int64_t now, TouchOut* out)
{
	int n = 0, first = -1;
	for (int i = 0; i < TOUCH_MAX; i++)
		if (t->c[i].down) {
			n++;
			if (first < 0)
				first = i;
		}

	switch (t->state) {
		case TS_IDLE:
		case TS_ONE:
			if (n >= 2) {
				pinch_points(t, t->p0);
				memcpy(t->p1, t->p0, sizeof(t->p1));
				t->state = TS_PINCH;
				return TOUCH_NONE;
			}
			if (n == 1) {
				int x, y;
				map_contact(t, first, &x, &y);
				if (t->state == TS_IDLE) {
					t->sx = x, t->sy = y, t->maxmove = 0;
					t->t_down = now;
					t->state  = TS_ONE;
				}
				int m = abs(x - t->sx) + abs(y - t->sy);
				if (m > t->maxmove)
					t->maxmove = m;
				t->lx = x, t->ly = y;
				return TOUCH_NONE;
			}
			if (t->state == TS_IDLE)
				return TOUCH_NONE;
			t->state = TS_IDLE;
			*out     = (TouchOut){ t->sx, t->sy, t->lx - t->sx, t->ly - t->sy, 1.0f };
			if (t->maxmove <= (t->screen_w < t->screen_h ? t->screen_w : t->screen_h) / 20) {
				out->dx = out->dy = 0;
				return (now - t->t_down >= LONG_PRESS_MS) ? TOUCH_LONG : TOUCH_TAP;
			}
			return TOUCH_DRAG;
		case TS_PINCH: {
			if (n >= 2) {
				pinch_points(t, t->p1);
				return TOUCH_NONE;
			}
			t->state  = n == 0 ? TS_IDLE : TS_WAIT_UP;
			double d0 = dist(t->p0), d1 = dist(t->p1);
			int    mx0 = (t->p0[0] + t->p0[2]) / 2, my0 = (t->p0[1] + t->p0[3]) / 2;
			int    mx1 = (t->p1[0] + t->p1[2]) / 2, my1 = (t->p1[1] + t->p1[3]) / 2;
			*out = (TouchOut){ mx0, my0, mx1 - mx0, my1 - my0, (float) (d1 / (d0 < 1 ? 1 : d0)) };
			return TOUCH_PINCH;
		}
		case TS_WAIT_UP:
			if (n == 0)
				t->state = TS_IDLE;
			return TOUCH_NONE;
	}
	return TOUCH_NONE;
}

TouchResult touch_feed(Touch* t, const struct input_event* ev, int64_t now, TouchOut* out)
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
			return TOUCH_NONE;
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
			return TOUCH_NONE;
		case EV_SYN:
			break;
		default:
			return TOUCH_NONE;
	}
	if (ev->code == SYN_MT_REPORT) {
		// Protocol A: each contact's data ends with SYN_MT_REPORT.
		t->mt_a = true;
		if (c && c->data)
			t->slot++;
		return TOUCH_NONE;
	}
	if (ev->code != SYN_REPORT)
		return TOUCH_NONE;

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
	return frame(t, now, out);
}
